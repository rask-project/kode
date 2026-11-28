#include "Bridge.h"

#include "Runtime.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace kode::remote
{

namespace
{

// The daemon, detached from this session (its own, stdio closed): it
// outlives the bridge and the SSH connection.
void startDaemon(const char *self)
{
    const pid_t child{::fork()};
    if (child < 0) {
        return;
    }
    if (child > 0) {
        int status{0};
        ::waitpid(child, &status, 0);
        return;
    }
    ::setsid();
    if (::fork() != 0) {
        ::_exit(0);
    }
    const int null{::open("/dev/null", O_RDWR)};
    ::dup2(null, STDIN_FILENO);
    ::dup2(null, STDOUT_FILENO);
    ::dup2(null, STDERR_FILENO);
    ::execl(self, self, "daemon", nullptr);
    ::_exit(EXIT_FAILURE);
}

std::expected<Fd, std::string> connectStarting(const char *self, const std::string &path)
{
    std::expected<Fd, std::string> socket{connectTo(path)};
    if (socket) {
        return socket;
    }
    // None, or a stale socket: a daemon that finds another running exits.
    startDaemon(self);
    constexpr std::chrono::milliseconds pause{20};
    constexpr std::int32_t attempts{250};
    for (std::int32_t attempt{0}; attempt < attempts && !socket; ++attempt) {
        std::this_thread::sleep_for(pause);
        socket = connectTo(path);
    }
    return socket;
}

// All of `bytes` to `fd` (blocking).
bool writeAll(int fd, std::string_view bytes)
{
    while (!bytes.empty()) {
        const ssize_t count{::write(fd, bytes.data(), bytes.size())};
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        bytes.remove_prefix(static_cast<std::size_t>(count));
    }
    return true;
}

// Copies what `from` has to `to`; false at its end or on an error.
bool pass(int from, int to)
{
    constexpr std::size_t chunkSize{std::size_t{64} * 1024U};
    std::array<char, chunkSize> chunk{};
    const ssize_t count{::read(from, chunk.data(), chunk.size())};
    if (count < 0 && errno == EINTR) {
        return true;
    }
    return count > 0 && writeAll(to, std::string_view{chunk.data(), static_cast<std::size_t>(count)});
}

} // namespace

int bridge(const char *self)
{
    std::signal(SIGPIPE, SIG_IGN);
    const std::expected<std::string, std::string> folder{runtimeFolder()};
    if (!folder) {
        std::fprintf(stderr, "kode-remote: %s\n", folder.error().c_str());
        return EXIT_FAILURE;
    }
    const std::expected<Fd, std::string> socket{connectStarting(self, socketPath(*folder))};
    if (!socket) {
        std::fprintf(stderr, "kode-remote: %s\n", socket.error().c_str());
        return EXIT_FAILURE;
    }
    if (!peerIsUser(socket->get())) {
        std::fprintf(stderr, "kode-remote: the daemon's socket belongs to another user: refused.\n");
        return EXIT_FAILURE;
    }
    std::array<pollfd, 2> fds{
        pollfd{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0},
        pollfd{.fd = socket->get(), .events = POLLIN, .revents = 0},
    };
    while (true) {
        if (::poll(fds.data(), fds.size(), -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            return EXIT_FAILURE;
        }
        if (fds[0].revents != 0 && !pass(STDIN_FILENO, socket->get())) {
            return EXIT_SUCCESS;
        }
        if (fds[1].revents != 0 && !pass(socket->get(), STDOUT_FILENO)) {
            return EXIT_SUCCESS;
        }
    }
}

} // namespace kode::remote
