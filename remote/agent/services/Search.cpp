#include "Search.h"

#include "../../protocol/Methods.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace kode::remote
{

namespace
{

// At most this many files, as FileSearch.
constexpr std::size_t fileLimit{200000U};

struct DirDeleter {
    void operator()(DIR *dir) const noexcept { ::closedir(dir); }
};
using Dir = std::unique_ptr<DIR, DirDeleter>;

bool skipped(std::string_view name) noexcept
{
    constexpr std::array<std::string_view, 3> names{".git", "node_modules", "build"};
    return std::ranges::contains(names, name) || name.starts_with("cmake-build-");
}

// The folder walked without recursion: a stack of open folders.
class Walk final : public Source
{
public:
    Walk(std::string root, bool hidden) : m_root{std::move(root)}, m_hidden{hidden ? 1U : 0U}
    {
        if (Dir dir{::opendir(m_root.c_str())}) {
            m_stack.emplace_back(Level{.dir = std::move(dir), .relative = {}});
        }
    }

    std::expected<std::optional<std::string>, std::string> next(std::size_t limit) override
    {
        std::string out;
        while (out.size() < limit && !m_stack.empty() && m_found < fileLimit) {
            const dirent *const entry{::readdir(m_stack.back().dir.get())};
            if (entry == nullptr) {
                m_stack.pop_back();
                continue;
            }
            if (std::optional<std::string> file{visit(*entry)}) {
                out.append(*file).push_back('\n');
                ++m_found;
            }
        }
        if (out.empty()) {
            return std::nullopt;
        }
        return out;
    }

private:
    // An entry of the folder on top: its path when it is a file to list; a
    // folder goes on the stack.
    std::optional<std::string> visit(const dirent &entry)
    {
        const std::string_view name{static_cast<const char *>(entry.d_name)};
        if (name == "." || name == ".." || (m_hidden == 0U && name.starts_with('.'))) {
            return std::nullopt;
        }
        const Level &level{m_stack.back()};
        std::string relative{level.relative.empty() ? std::string{name} : level.relative + "/" + std::string{name}};
        struct stat info{};
        if (::fstatat(::dirfd(level.dir.get()), entry.d_name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
            return std::nullopt;
        }
        if (S_ISDIR(info.st_mode)) {
            if (Dir dir{skipped(name) ? nullptr : ::opendir((m_root + "/" + relative).c_str())}) {
                m_stack.emplace_back(Level{.dir = std::move(dir), .relative = std::move(relative)});
            }
            return std::nullopt;
        }
        // A link to a folder is not followed; to a file, it is one.
        struct stat target{};
        const bool file{
            S_ISREG(info.st_mode) ||
            (S_ISLNK(info.st_mode) && ::stat((m_root + "/" + relative).c_str(), &target) == 0 && S_ISREG(target.st_mode)
            )
        };
        return file ? std::optional{std::move(relative)} : std::nullopt;
    }

    struct Level {
        Dir dir;
        std::string relative;
    };

    std::vector<Level> m_stack;
    std::string m_root;
    std::size_t m_found{0U};
    std::uint64_t m_hidden{0U};
};

} // namespace

void Search::handle(
    std::uint64_t session,
    std::string_view method,
    yyjson_val *params,
    Reply reply,
    const Channels &channels
)
{
    static_cast<void>(session);
    if (method != "files") {
        reply.error(error::methodNotFound, "No such method.");
        return;
    }
    const std::optional<std::string_view> root{json::string(params, "path")};
    if (!root || !root->starts_with('/')) {
        reply.error(error::invalidParams, "An absolute path is expected.");
        return;
    }
    const std::uint32_t channel{
        channels.open(std::make_unique<Walk>(std::string{*root}, yyjson_get_bool(yyjson_obj_get(params, "showHidden"))))
    };
    yyjson_mut_val *const result{yyjson_mut_obj(reply.doc())};
    yyjson_mut_obj_add_uint(reply.doc(), result, "channel", channel);
    reply.result(result);
}

} // namespace kode::remote
