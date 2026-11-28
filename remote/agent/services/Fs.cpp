#include "Fs.h"

#include "../../protocol/Methods.h"
#include "../Fd.h"
#include "../Runtime.h"
#include "../Sha256.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>

namespace kode::remote
{

namespace
{

// A folder's entries in one answer, at most.
constexpr std::size_t listLimit{50000U};

struct DirDeleter {
    void operator()(DIR *dir) const noexcept { ::closedir(dir); }
};
using Dir = std::unique_ptr<DIR, DirDeleter>;

timespec modifiedOf(const struct stat &info) noexcept
{
#if defined(__APPLE__)
    return info.st_mtimespec;
#else
    return info.st_mtim;
#endif
}

const char *typeOf(mode_t mode) noexcept
{
    if (S_ISREG(mode)) {
        return "file";
    }
    if (S_ISDIR(mode)) {
        return "folder";
    }
    if (S_ISLNK(mode)) {
        return "link";
    }
    return "other";
}

std::int32_t codeOf(int number) noexcept
{
    switch (number) {
    case ENOENT:
    case ENOTDIR:
        return error::notFound;
    case EACCES:
    case EPERM:
    case EROFS:
        return error::permission;
    default:
        return error::failed;
    }
}

void failWithErrno(Reply &reply, const std::string &what)
{
    const int number{errno};
    reply.error(codeOf(number), what + ": " + std::strerror(number));
}

// type, size, mtime (seconds), mtimeNs, mode.
void addInfo(yyjson_mut_doc *doc, yyjson_mut_val *object, const struct stat &info)
{
    const timespec modified{modifiedOf(info)};
    yyjson_mut_obj_add_str(doc, object, "type", typeOf(info.st_mode));
    yyjson_mut_obj_add_int(doc, object, "size", static_cast<std::int64_t>(info.st_size));
    yyjson_mut_obj_add_int(doc, object, "mtime", static_cast<std::int64_t>(modified.tv_sec));
    yyjson_mut_obj_add_int(doc, object, "mtimeNs", static_cast<std::int64_t>(modified.tv_nsec));
    yyjson_mut_obj_add_int(doc, object, "mode", static_cast<std::int64_t>(info.st_mode & 07777U));
}

// A link's target and what it is ("missing" when it leads nowhere).
void addTarget(yyjson_mut_doc *doc, yyjson_mut_val *object, const std::string &path)
{
    std::array<char, PATH_MAX> target{};
    const ssize_t length{::readlink(path.c_str(), target.data(), target.size() - 1U)};
    if (length >= 0) {
        yyjson_mut_obj_add_strncpy(doc, object, "target", target.data(), static_cast<std::size_t>(length));
    }
    struct stat info{};
    yyjson_mut_obj_add_str(
        doc,
        object,
        "targetType",
        ::stat(path.c_str(), &info) == 0 ? typeOf(info.st_mode) : "missing"
    );
}

std::optional<std::string> pathParam(yyjson_val *params, Reply &reply)
{
    const std::optional<std::string_view> path{json::string(params, "path")};
    if (!path || !path->starts_with('/')) {
        reply.error(error::invalidParams, "An absolute path is expected.");
        return std::nullopt;
    }
    return std::string{*path};
}

// The bytes of an open file, checked at the end against the version they started from.
class FileSource final : public Source
{
public:
    FileSource(Fd file, const struct stat &info) : m_info{info}, m_file{std::move(file)} {}

    std::expected<std::optional<std::string>, std::string> next(std::size_t limit) override
    {
        std::string piece(limit, '\0');
        while (true) {
            const ssize_t count{::read(m_file.get(), piece.data(), piece.size())};
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count < 0) {
                return std::unexpected{systemError("read")};
            }
            if (count > 0) {
                piece.resize(static_cast<std::size_t>(count));
                return piece;
            }
            break;
        }
        struct stat now{};
        const timespec before{modifiedOf(m_info)};
        if (::fstat(m_file.get(), &now) != 0 || now.st_size != m_info.st_size ||
            modifiedOf(now).tv_sec != before.tv_sec || modifiedOf(now).tv_nsec != before.tv_nsec) {
            return std::unexpected{std::string{"The file changed while it was read."}};
        }
        return std::nullopt;
    }

private:
    struct stat m_info{};
    Fd m_file;
};

struct Expected {
    std::string hash;
    std::int64_t size{0};
    std::int64_t seconds{0};
    std::int64_t nanoseconds{0};
};

std::optional<Expected> expectedOf(yyjson_val *params)
{
    yyjson_val *const expect{yyjson_obj_get(params, "expect")};
    if (!yyjson_is_obj(expect)) {
        return std::nullopt;
    }
    return Expected{
        .hash = std::string{json::string(expect, "hash").value_or("")},
        .size = json::integer(expect, "size").value_or(-1),
        .seconds = json::integer(expect, "mtime").value_or(0),
        .nanoseconds = json::integer(expect, "mtimeNs").value_or(0),
    };
}

// The target is still the version the client read: same size and time, or same size and bytes.
bool matches(const std::string &target, const Expected &expected, struct stat &now)
{
    if (::stat(target.c_str(), &now) != 0) {
        return false;
    }
    if (now.st_size != expected.size) {
        return false;
    }
    const timespec modified{modifiedOf(now)};
    if (modified.tv_sec == expected.seconds && modified.tv_nsec == expected.nanoseconds) {
        return true;
    }
    if (expected.hash.empty()) {
        return false;
    }
    const std::expected<std::string, std::string> hash{hashFile(target)};
    return hash && *hash == expected.hash;
}

std::string folderOf(const std::string &path)
{
    const std::size_t slash{path.rfind('/')};
    return slash == 0U ? std::string{"/"} : path.substr(0, slash);
}

bool makeFolders(const std::string &folder)
{
    for (std::size_t slash{folder.find('/', 1U)}; true; slash = folder.find('/', slash + 1U)) {
        const std::string part{slash == std::string::npos ? folder : folder.substr(0, slash)};
        if (::mkdir(part.c_str(), 0777) != 0 && errno != EEXIST) {
            return false;
        }
        if (slash == std::string::npos) {
            return true;
        }
    }
}

// What the client sends for fs/write: a temporary file beside the target,
// renamed over it once checked; written in place where the folder takes no
// new file.
class WriteSink final : public Sink
{
public:
    WriteSink(Reply reply, std::string target, std::optional<Expected> expected)
        : m_reply{std::move(reply)}
        , m_expected{std::move(expected)}
        , m_target{std::move(target)}
    {}
    WriteSink(const WriteSink &) = delete;
    WriteSink &operator=(const WriteSink &) = delete;
    WriteSink(WriteSink &&) = delete;
    WriteSink &operator=(WriteSink &&) = delete;
    ~WriteSink() override { discard(); }

    // False when it cannot start: answered.
    bool open()
    {
        struct stat info{};
        if (::stat(m_target.c_str(), &info) == 0) {
            m_mode = info.st_mode & 07777U;
            m_owner = info.st_uid;
            m_group = info.st_gid;
            m_flags |= Existed;
        } else {
            const mode_t mask{::umask(0)};
            ::umask(mask);
            m_mode = 0666U & ~static_cast<std::uint32_t>(mask);
        }
        const std::size_t slash{m_target.rfind('/')};
        std::string temporary{m_target.substr(0, slash + 1U) + "." + m_target.substr(slash + 1U) + ".kode-XXXXXX"};
        m_file = Fd{::mkstemp(temporary.data())};
        if (m_file.valid()) {
            m_temporary = std::move(temporary);
            return true;
        }
        // A folder that takes no new file, but the file itself may be written.
        if (errno == EACCES && (m_flags & Existed) != 0U) {
            return true;
        }
        failWithErrno(m_reply, "Cannot write in " + folderOf(m_target));
        return false;
    }

    bool write(std::string_view bytes) override
    {
        if (!m_file.valid()) {
            m_inPlace.append(bytes);
            return true;
        }
        while (!bytes.empty()) {
            const ssize_t count{::write(m_file.get(), bytes.data(), bytes.size())};
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count <= 0) {
                failWithErrno(m_reply, "Cannot write " + m_target);
                discard();
                return false;
            }
            bytes.remove_prefix(static_cast<std::size_t>(count));
        }
        return true;
    }

    void end() override
    {
        if (m_file.valid() && ::fsync(m_file.get()) != 0) {
            failWithErrno(m_reply, "Cannot write " + m_target);
            discard();
            return;
        }
        if (struct stat now{}; m_expected && !matches(m_target, *m_expected, now)) {
            conflict(now);
            return;
        }
        if (!(m_file.valid() ? replace() : writeInPlace())) {
            return;
        }
        struct stat written{};
        if (::stat(m_target.c_str(), &written) != 0) {
            failWithErrno(m_reply, "Cannot read " + m_target);
            return;
        }
        yyjson_mut_val *const result{yyjson_mut_obj(m_reply.doc())};
        addInfo(m_reply.doc(), result, written);
        m_reply.result(result);
    }

    void reset() override { discard(); }

private:
    static constexpr std::uint32_t Existed{1U};

    void conflict(const struct stat &now)
    {
        discard();
        yyjson_mut_doc *const doc{m_reply.doc()};
        yyjson_mut_val *const data{yyjson_mut_obj(doc)};
        if (now.st_mode != 0U) {
            addInfo(doc, data, now);
        } else {
            yyjson_mut_obj_add_str(doc, data, "type", "missing");
        }
        m_reply.error(error::conflict, "The file changed on the host since it was read.", data);
    }

    bool replace()
    {
        if ((m_flags & Existed) != 0U) {
            // Not always allowed (another user's file in a shared folder): the mode still holds.
            static_cast<void>(::fchown(m_file.get(), m_owner, m_group));
        }
        if (::fchmod(m_file.get(), static_cast<mode_t>(m_mode)) != 0 ||
            ::rename(m_temporary.c_str(), m_target.c_str()) != 0) {
            failWithErrno(m_reply, "Cannot save " + m_target);
            discard();
            return false;
        }
        m_temporary.clear();
        m_file.reset();
        return true;
    }

    bool writeInPlace()
    {
        const Fd file{::open(m_target.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC)};
        std::string_view bytes{m_inPlace};
        while (file.valid() && !bytes.empty()) {
            const ssize_t count{::write(file.get(), bytes.data(), bytes.size())};
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count <= 0) {
                break;
            }
            bytes.remove_prefix(static_cast<std::size_t>(count));
        }
        if (!file.valid() || !bytes.empty() || ::fsync(file.get()) != 0) {
            failWithErrno(m_reply, "Cannot save " + m_target);
            return false;
        }
        return true;
    }

    void discard() noexcept
    {
        m_file.reset();
        if (!m_temporary.empty()) {
            ::unlink(m_temporary.c_str());
            m_temporary.clear();
        }
    }

    Reply m_reply;
    std::optional<Expected> m_expected;
    std::string m_target;
    std::string m_temporary;
    // Where the folder takes no new file.
    std::string m_inPlace;
    Fd m_file;
    std::uint32_t m_flags{0U};
    // mode_t is 16 bits on macOS, 32 on Linux.
    std::uint32_t m_mode{0U};
    uid_t m_owner{0};
    gid_t m_group{0};
};

} // namespace

void Fs::handle(
    std::uint64_t session,
    std::string_view method,
    yyjson_val *params,
    Reply reply,
    const Channels &channels
)
{
    static_cast<void>(session);
    if (method == "stat") {
        stat(params, std::move(reply));
    } else if (method == "list") {
        list(params, std::move(reply));
    } else if (method == "read") {
        read(params, std::move(reply), channels);
    } else if (method == "write") {
        write(params, std::move(reply), channels);
    } else {
        reply.error(error::methodNotFound, "No such method.");
    }
}

void Fs::stat(yyjson_val *params, Reply reply)
{
    yyjson_val *const paths{yyjson_obj_get(params, "paths")};
    if (!yyjson_is_arr(paths)) {
        reply.error(error::invalidParams, "paths: a list of paths is expected.");
        return;
    }
    yyjson_mut_doc *const doc{reply.doc()};
    yyjson_mut_val *const entries{yyjson_mut_arr(doc)};
    std::size_t index{0U};
    std::size_t count{0U};
    yyjson_val *value{nullptr};
    yyjson_arr_foreach(paths, index, count, value)
    {
        yyjson_mut_val *const entry{yyjson_mut_arr_add_obj(doc, entries)};
        const std::string path{yyjson_is_str(value) ? yyjson_get_str(value) : ""};
        yyjson_mut_obj_add_strcpy(doc, entry, "path", path.c_str());
        struct stat info{};
        if (::lstat(path.c_str(), &info) != 0) {
            yyjson_mut_obj_add_str(doc, entry, "type", errno == ENOENT || errno == ENOTDIR ? "missing" : "error");
            continue;
        }
        addInfo(doc, entry, info);
        if (S_ISLNK(info.st_mode)) {
            addTarget(doc, entry, path);
        }
    }
    yyjson_mut_val *const result{yyjson_mut_obj(doc)};
    yyjson_mut_obj_add_val(doc, result, "entries", entries);
    reply.result(result);
}

void Fs::list(yyjson_val *params, Reply reply)
{
    const std::optional<std::string> folder{pathParam(params, reply)};
    if (!folder) {
        return;
    }
    const Dir dir{::opendir(folder->c_str())};
    if (!dir) {
        failWithErrno(reply, "Cannot list " + *folder);
        return;
    }
    yyjson_mut_doc *const doc{reply.doc()};
    yyjson_mut_val *const entries{yyjson_mut_arr(doc)};
    std::size_t listed{0U};
    bool truncated{false};
    for (const dirent *entry{::readdir(dir.get())}; entry != nullptr; entry = ::readdir(dir.get())) {
        const std::string_view name{static_cast<const char *>(entry->d_name)};
        if (name == "." || name == "..") {
            continue;
        }
        if (listed == listLimit) {
            truncated = true;
            break;
        }
        struct stat info{};
        if (::fstatat(::dirfd(dir.get()), entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
            continue;
        }
        yyjson_mut_val *const object{yyjson_mut_arr_add_obj(doc, entries)};
        yyjson_mut_obj_add_strncpy(doc, object, "name", name.data(), name.size());
        addInfo(doc, object, info);
        if (S_ISLNK(info.st_mode)) {
            addTarget(doc, object, *folder + "/" + std::string{name});
        }
        ++listed;
    }
    yyjson_mut_val *const result{yyjson_mut_obj(doc)};
    yyjson_mut_obj_add_val(doc, result, "entries", entries);
    yyjson_mut_obj_add_bool(doc, result, "truncated", truncated);
    reply.result(result);
}

void Fs::read(yyjson_val *params, Reply reply, const Channels &channels)
{
    const std::optional<std::string> path{pathParam(params, reply)};
    if (!path) {
        return;
    }
    Fd file{::open(path->c_str(), O_RDONLY | O_CLOEXEC)};
    struct stat info{};
    if (!file.valid() || ::fstat(file.get(), &info) != 0) {
        failWithErrno(reply, "Cannot open " + *path);
        return;
    }
    if (!S_ISREG(info.st_mode)) {
        reply.error(error::invalidParams, *path + " is not a file.");
        return;
    }
    const std::uint32_t channel{channels.open(std::make_unique<FileSource>(std::move(file), info))};
    yyjson_mut_doc *const doc{reply.doc()};
    yyjson_mut_val *const result{yyjson_mut_obj(doc)};
    addInfo(doc, result, info);
    yyjson_mut_obj_add_uint(doc, result, "channel", channel);
    reply.result(result);
}

void Fs::write(yyjson_val *params, Reply reply, const Channels &channels)
{
    const std::optional<std::string> path{pathParam(params, reply)};
    const std::optional<std::int64_t> channel{json::integer(params, "channel")};
    if (!path) {
        return;
    }
    if (!channel || *channel <= 0 || *channel > std::int64_t{UINT32_MAX}) {
        reply.error(error::invalidParams, "channel: the client's channel with the bytes is expected.");
        return;
    }
    if (yyjson_get_bool(yyjson_obj_get(params, "makeFolders")) && !makeFolders(folderOf(*path))) {
        failWithErrno(reply, "Cannot make " + folderOf(*path));
        return;
    }
    // Written through a link, to what it names.
    std::string target{*path};
    struct stat info{};
    if (::lstat(path->c_str(), &info) == 0 && S_ISLNK(info.st_mode)) {
        std::array<char, PATH_MAX> real{};
        if (::realpath(path->c_str(), real.data()) == nullptr) {
            failWithErrno(reply, *path + " is a link to nothing");
            return;
        }
        target = real.data();
    }
    auto sink{std::make_unique<WriteSink>(std::move(reply), std::move(target), expectedOf(params))};
    if (!sink->open()) {
        return;
    }
    // A channel taken already: the sink goes, answering with an internal error.
    static_cast<void>(channels.accept(static_cast<std::uint32_t>(*channel), std::move(sink)));
}

} // namespace kode::remote
