#include "Process.h"

#include "../../protocol/Methods.h"
#include "../Runtime.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>

namespace kode::remote
{

namespace
{

// stdout read ahead of the client, at most.
constexpr std::size_t outputLimit{std::size_t{1024} * 1024U};
// How often an exiting process is asked whether it is done.
constexpr std::int64_t reapMilliseconds{100};
// A stopped session's process gets this long after SIGTERM.
constexpr std::int64_t killMilliseconds{2000};
constexpr std::string_view pathMarker{"__KODE_PATH__"};

// Calls the client's function on each pull of a process's stdout.
class Pulled final : public Source
{
public:
    explicit Pulled(std::function<std::expected<std::optional<std::string>, std::string>(std::size_t)> pull)
        : m_pull{std::move(pull)}
    {}
    std::expected<std::optional<std::string>, std::string> next(std::size_t limit) override { return m_pull(limit); }

private:
    std::function<std::expected<std::optional<std::string>, std::string>(std::size_t)> m_pull;
};

// What the client sends to a process's stdin.
class Fed final : public Sink
{
public:
    Fed(std::function<void(std::string_view)> feed, std::function<void()> end)
        : m_feed{std::move(feed)}
        , m_end{std::move(end)}
    {}
    bool write(std::string_view bytes) override
    {
        m_feed(bytes);
        return true;
    }
    void end() override { m_end(); }
    void reset() override { m_end(); }

private:
    std::function<void(std::string_view)> m_feed;
    std::function<void()> m_end;
};

bool executable(const std::string &path)
{
    struct stat info{};
    return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && ::access(path.c_str(), X_OK) == 0;
}

// The login shell's PATH (what a terminal on the host has), else the agent's.
std::string loginPath()
{
    const char *const shell{std::getenv("SHELL")};
    std::array<int, 2> pipe{-1, -1};
    if (shell == nullptr || ::pipe(pipe.data()) != 0) {
        const char *const path{std::getenv("PATH")};
        return path != nullptr ? path : "/usr/bin:/bin";
    }
    const Fd read{pipe[0]};
    Fd write{pipe[1]};
    const pid_t child{::fork()};
    if (child == 0) {
        const int null{::open("/dev/null", O_RDWR)};
        ::dup2(null, STDIN_FILENO);
        ::dup2(write.get(), STDOUT_FILENO);
        ::dup2(null, STDERR_FILENO);
        ::execl(shell, shell, "-ilc", "printf '__KODE_PATH__%s' \"$PATH\"", nullptr);
        ::_exit(EXIT_FAILURE);
    }
    write.reset();
    std::string out;
    constexpr std::int32_t timeoutMs{5000};
    std::array<char, 4096> chunk{};
    pollfd fd{.fd = read.get(), .events = POLLIN, .revents = 0};
    while (child > 0 && ::poll(&fd, 1, timeoutMs) > 0) {
        const ssize_t count{::read(read.get(), chunk.data(), chunk.size())};
        if (count <= 0) {
            break;
        }
        out.append(chunk.data(), static_cast<std::size_t>(count));
    }
    if (child > 0) {
        ::kill(child, SIGKILL);
        ::waitpid(child, nullptr, 0);
    }
    const std::size_t marker{out.rfind(pathMarker)};
    if (marker == std::string::npos) {
        const char *const path{std::getenv("PATH")};
        return path != nullptr ? path : "/usr/bin:/bin";
    }
    return out.substr(marker + pathMarker.size());
}

} // namespace

Process::Process(Hooks hooks) : m_hooks{std::move(hooks)} {}

Process::~Process()
{
    std::ranges::for_each(m_children, [](const auto &entry) {
        if ((entry.second.flags & Exited) == 0U) {
            ::kill(static_cast<pid_t>(entry.second.pid), SIGTERM);
        }
    });
}

void Process::handle(
    std::uint64_t session,
    std::string_view method,
    yyjson_val *params,
    Reply reply,
    const Channels &channels
)
{
    if (method == "which") {
        which(params, std::move(reply));
    } else if (method == "spawn") {
        spawn(session, params, std::move(reply), channels);
    } else if (method == "signal") {
        signal(session, params, std::move(reply));
    } else {
        reply.error(error::methodNotFound, "No such method.");
    }
}

const std::vector<std::string> &Process::path()
{
    if (m_path.empty()) {
        const std::string joined{loginPath()};
        for (std::size_t start{0U}; start <= joined.size();) {
            const std::size_t colon{std::min(joined.find(':', start), joined.size())};
            if (colon > start) {
                m_path.emplace_back(joined.substr(start, colon - start));
            }
            start = colon + 1U;
        }
    }
    return m_path;
}

std::string Process::find(const std::string &name, const std::vector<std::string> &folders)
{
    if (name.find('/') != std::string::npos) {
        return executable(name) ? name : std::string{};
    }
    const auto in{[&name](const std::vector<std::string> &list) {
        const auto found{std::ranges::find_if(list, [&name](const std::string &folder) {
            return executable(folder + "/" + name);
        })};
        return found == list.end() ? std::string{} : *found + "/" + name;
    }};
    const std::string first{in(folders)};
    return first.empty() ? in(path()) : first;
}

void Process::which(yyjson_val *params, Reply reply)
{
    yyjson_val *const names{yyjson_obj_get(params, "names")};
    if (!yyjson_is_arr(names)) {
        reply.error(error::invalidParams, "names: a list of program names is expected.");
        return;
    }
    const char *const home{std::getenv("HOME")};
    std::vector<std::string> folders;
    std::size_t index{0U};
    std::size_t count{0U};
    yyjson_val *value{nullptr};
    yyjson_arr_foreach(yyjson_obj_get(params, "folders"), index, count, value)
    {
        if (!yyjson_is_str(value)) {
            continue;
        }
        const std::string folder{yyjson_get_str(value)};
        folders.emplace_back(folder.starts_with("~/") && home != nullptr ? home + folder.substr(1) : folder);
    }
    yyjson_mut_doc *const doc{reply.doc()};
    yyjson_mut_val *const found{yyjson_mut_obj(doc)};
    yyjson_arr_foreach(names, index, count, value)
    {
        if (!yyjson_is_str(value)) {
            continue;
        }
        const std::string name{yyjson_get_str(value)};
        if (const std::string at{find(name, folders)}; !at.empty()) {
            yyjson_mut_obj_add(found, yyjson_mut_strcpy(doc, name.c_str()), yyjson_mut_strcpy(doc, at.c_str()));
        }
    }
    yyjson_mut_val *const searched{yyjson_mut_arr(doc)};
    std::ranges::for_each(path(), [doc, searched](const std::string &folder) {
        yyjson_mut_arr_add_strcpy(doc, searched, folder.c_str());
    });
    yyjson_mut_val *const result{yyjson_mut_obj(doc)};
    yyjson_mut_obj_add_val(doc, result, "found", found);
    yyjson_mut_obj_add_val(doc, result, "path", searched);
    reply.result(result);
}

void Process::spawn(std::uint64_t session, yyjson_val *params, Reply reply, const Channels &channels)
{
    yyjson_val *const argv{yyjson_obj_get(params, "argv")};
    const std::optional<std::int64_t> input{json::integer(params, "stdin")};
    const std::string cwd{json::string(params, "cwd").value_or("/")};
    if (!yyjson_is_arr(argv) || yyjson_arr_size(argv) == 0U || !input || *input <= 0) {
        reply.error(error::invalidParams, "argv and stdin (the client's channel) are expected.");
        return;
    }
    std::vector<std::string> arguments;
    std::size_t index{0U};
    std::size_t count{0U};
    yyjson_val *value{nullptr};
    yyjson_arr_foreach(argv, index, count, value)
    {
        arguments.emplace_back(yyjson_is_str(value) ? yyjson_get_str(value) : "");
    }
    const std::string program{find(arguments.front())};
    if (program.empty()) {
        reply.error(error::notFound, arguments.front() + " is not on the host's PATH.");
        return;
    }
    std::array<int, 2> in{-1, -1};
    std::array<int, 2> out{-1, -1};
    if (::pipe(in.data()) != 0 || ::pipe(out.data()) != 0) {
        reply.error(error::failed, systemError("Cannot start " + program));
        return;
    }
    Fd childIn{in[0]};
    Fd childOut{out[1]};
    Fd parentIn{in[1]};
    Fd parentOut{out[0]};
    // Before the fork: the child must not keep its own stdin's other end (it would never see it end). dup2()
    // clears the flag on the child's 0 and 1.
    std::ranges::for_each(std::array{in[0], in[1], out[0], out[1]}, [](int fd) { ::fcntl(fd, F_SETFD, FD_CLOEXEC); });
    const std::string joinedPath{[this] {
        std::string joined;
        std::ranges::for_each(path(), [&joined](const std::string &folder) {
            joined.append(joined.empty() ? "" : ":").append(folder);
        });
        return joined;
    }()};
    const pid_t pid{::fork()};
    if (pid == 0) {
        const int null{::open("/dev/null", O_RDWR)};
        ::dup2(childIn.get(), STDIN_FILENO);
        ::dup2(childOut.get(), STDOUT_FILENO);
        ::dup2(null, STDERR_FILENO);
        if (::chdir(cwd.c_str()) != 0) {
            ::_exit(EXIT_FAILURE);
        }
        ::setenv("PATH", joinedPath.c_str(), 1);
        std::vector<char *> pointers;
        std::ranges::transform(arguments, std::back_inserter(pointers), [](std::string &argument) {
            return argument.data();
        });
        pointers.push_back(nullptr);
        ::execv(program.c_str(), pointers.data());
        ::_exit(EXIT_FAILURE);
    }
    if (pid < 0) {
        reply.error(error::failed, systemError("Cannot start " + program));
        return;
    }
    childIn.reset();
    childOut.reset();
    static_cast<void>(setNonBlocking(parentIn.get()));
    static_cast<void>(setNonBlocking(parentOut.get()));
    const std::int64_t id{m_nextId++};
    const int stdoutFd{parentOut.get()};
    m_children.emplace(
        id,
        Child{
            .output = {},
            .input = {},
            .session = session,
            .flags = 0U,
            .pid = pid,
            .status = 0,
            .stdinPipe = std::move(parentIn),
            .stdoutPipe = std::move(parentOut),
        }
    );
    const std::uint32_t stdoutChannel{
        channels.open(std::make_unique<Pulled>([this, id](std::size_t limit) { return pull(id, limit); }))
    };
    const bool accepted{channels.accept(
        static_cast<std::uint32_t>(*input),
        std::make_unique<Fed>([this, id](std::string_view bytes) { feed(id, bytes); }, [this, id] { endInput(id); })
    )};
    if (!accepted) {
        ::kill(pid, SIGTERM);
        m_children.at(id).flags |= Orphan | InputEnded;
        reap(id);
        reply.error(error::invalidParams, "stdin: a channel of the client's that is not in use is expected.");
        return;
    }
    m_hooks.watch(stdoutFd, POLLIN, [this, id] { readOutput(id); });
    yyjson_mut_doc *const doc{reply.doc()};
    yyjson_mut_val *const result{yyjson_mut_obj(doc)};
    yyjson_mut_obj_add_int(doc, result, "id", id);
    yyjson_mut_obj_add_uint(doc, result, "stdout", stdoutChannel);
    reply.result(result);
}

void Process::signal(std::uint64_t session, yyjson_val *params, Reply reply)
{
    const auto child{m_children.find(json::integer(params, "id").value_or(0))};
    if (child == m_children.end() || child->second.session != session) {
        reply.error(error::notFound, "No such process.");
        return;
    }
    const std::string_view name{json::string(params, "signal").value_or("TERM")};
    const int number{name == "KILL" ? SIGKILL : name == "INT" ? SIGINT : SIGTERM};
    if ((child->second.flags & Exited) == 0U) {
        ::kill(static_cast<pid_t>(child->second.pid), number);
    }
    reply.result(yyjson_mut_obj(reply.doc()));
}

void Process::readOutput(std::int64_t id)
{
    const auto found{m_children.find(id)};
    if (found == m_children.end()) {
        return;
    }
    Child &child{found->second};
    std::array<char, std::size_t{64} * 1024U> chunk{};
    while (child.output.size() < outputLimit) {
        const ssize_t count{::read(child.stdoutPipe.get(), chunk.data(), chunk.size())};
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            break;
        }
        if (count == 0) {
            m_hooks.unwatch(child.stdoutPipe.get());
            child.stdoutPipe.reset();
            child.flags |= OutputEnded;
            reap(id);
            break;
        }
        child.output.append(chunk.data(), static_cast<std::size_t>(count));
    }
    if (child.output.size() >= outputLimit && child.stdoutPipe.valid()) {
        m_hooks.unwatch(child.stdoutPipe.get());
        child.flags |= Paused;
    }
    m_hooks.pump(child.session);
}

std::expected<std::optional<std::string>, std::string> Process::pull(std::int64_t id, std::size_t limit)
{
    const auto found{m_children.find(id)};
    if (found == m_children.end()) {
        return std::nullopt;
    }
    Child &child{found->second};
    if (!child.output.empty()) {
        std::string piece{child.output.substr(0, limit)};
        child.output.erase(0, piece.size());
        if ((child.flags & Paused) != 0U && child.output.size() < outputLimit / 2U && child.stdoutPipe.valid()) {
            child.flags &= ~Paused;
            m_hooks.watch(child.stdoutPipe.get(), POLLIN, [this, id] { readOutput(id); });
        }
        return piece;
    }
    if ((child.flags & OutputEnded) == 0U) {
        return std::string{};
    }
    // All sent: the exit, when it came, can be told.
    finish(id);
    return std::nullopt;
}

void Process::feed(std::int64_t id, std::string_view bytes)
{
    const auto found{m_children.find(id)};
    if (found == m_children.end() || !found->second.stdinPipe.valid()) {
        return;
    }
    found->second.input.append(bytes);
    flushInput(id);
}

void Process::endInput(std::int64_t id)
{
    const auto found{m_children.find(id)};
    if (found == m_children.end()) {
        return;
    }
    found->second.flags |= InputEnded;
    flushInput(id);
}

void Process::flushInput(std::int64_t id)
{
    const auto found{m_children.find(id)};
    if (found == m_children.end() || !found->second.stdinPipe.valid()) {
        return;
    }
    Child &child{found->second};
    while (!child.input.empty()) {
        const ssize_t count{::write(child.stdinPipe.get(), child.input.data(), child.input.size())};
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        child.input.erase(0, static_cast<std::size_t>(count));
    }
    if (!child.input.empty() && errno == EAGAIN) {
        m_hooks.watch(child.stdinPipe.get(), POLLOUT, [this, id] { flushInput(id); });
        return;
    }
    m_hooks.unwatch(child.stdinPipe.get());
    // Taken, or the process stopped reading: closed once the client is done.
    if ((child.flags & InputEnded) != 0U || !child.input.empty()) {
        child.stdinPipe.reset();
    }
}

void Process::reap(std::int64_t id)
{
    const auto found{m_children.find(id)};
    if (found == m_children.end() || (found->second.flags & Exited) != 0U) {
        return;
    }
    int status{0};
    const pid_t done{::waitpid(static_cast<pid_t>(found->second.pid), &status, WNOHANG)};
    if (done == 0) {
        m_hooks.later(reapMilliseconds, [this, id] { reap(id); });
        return;
    }
    found->second.flags |= Exited;
    found->second.status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    if (found->second.output.empty() && (found->second.flags & OutputEnded) != 0U) {
        finish(id);
    }
}

void Process::finish(std::int64_t id)
{
    const auto found{m_children.find(id)};
    if (found == m_children.end() || (found->second.flags & Exited) == 0U) {
        return;
    }
    if ((found->second.flags & Orphan) == 0U) {
        m_hooks.notify(
            found->second.session,
            "process/exited",
            R"({"id":)" + std::to_string(id) + R"(,"code":)" + std::to_string(found->second.status) + "}"
        );
    }
    if (found->second.stdinPipe.valid()) {
        m_hooks.unwatch(found->second.stdinPipe.get());
    }
    m_children.erase(found);
}

void Process::sessionClosed(std::uint64_t session)
{
    std::vector<std::int64_t> ids;
    std::ranges::for_each(m_children, [session, &ids](const auto &entry) {
        if (entry.second.session == session) {
            ids.emplace_back(entry.first);
        }
    });
    std::ranges::for_each(ids, [this](std::int64_t id) {
        Child &child{m_children.at(id)};
        child.flags |= Orphan;
        if ((child.flags & Exited) != 0U) {
            finish(id);
            return;
        }
        const auto pid{static_cast<pid_t>(child.pid)};
        ::kill(pid, SIGTERM);
        if (child.stdoutPipe.valid()) {
            m_hooks.unwatch(child.stdoutPipe.get());
        }
        if (child.stdinPipe.valid()) {
            m_hooks.unwatch(child.stdinPipe.get());
        }
        child.stdinPipe.reset();
        child.stdoutPipe.reset();
        child.output.clear();
        child.flags |= OutputEnded;
        m_hooks.later(killMilliseconds, [this, id] {
            if (m_children.contains(id) && (m_children.at(id).flags & Exited) == 0U) {
                ::kill(static_cast<pid_t>(m_children.at(id).pid), SIGKILL);
            }
        });
        reap(id);
    });
}

} // namespace kode::remote
