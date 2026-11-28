// kode-askpass: ssh's SSH_ASKPASS while Kode connects to a host. It asks the
// running Kode (a unix socket and a token in its environment, set by Kode for
// the ssh it started) and prints the answer; Kode shows the native prompt.
//
// To Kode: "<length>\n<token>\n<host>\n<SSH_ASKPASS_PROMPT>\n<prompt>".
// From Kode: "1\n<answer>" (accepted) or "0\n" (cancelled), then it closes.

#include "../agent/Fd.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>

namespace
{

std::string environment(const char *name)
{
    const char *const value{std::getenv(name)};
    return value != nullptr ? std::string{value} : std::string{};
}

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

std::string readAll(int fd)
{
    std::string answer;
    std::array<char, 4096> chunk{};
    constexpr std::size_t limit{std::size_t{64} * 1024U};
    while (answer.size() < limit) {
        const ssize_t count{::read(fd, chunk.data(), chunk.size())};
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        answer.append(chunk.data(), static_cast<std::size_t>(count));
    }
    return answer;
}

} // namespace

int main(int argc, char *argv[])
{
    const std::span<char *> arguments{argv, static_cast<std::size_t>(argc)};
    const std::string path{environment("KODE_ASKPASS_SOCKET")};
    sockaddr_un address{};
    if (path.empty() || path.size() >= sizeof(address.sun_path)) {
        return EXIT_FAILURE;
    }
    address.sun_family = AF_UNIX;
    path.copy(static_cast<char *>(address.sun_path), path.size());
    const kode::remote::Fd connection{::socket(AF_UNIX, SOCK_STREAM, 0)};
    const int socket{connection.get()};
    if (socket < 0 || ::connect(socket, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
        return EXIT_FAILURE;
    }
    const std::string payload{
        environment("KODE_ASKPASS_TOKEN") + '\n' + environment("KODE_ASKPASS_HOST") + '\n' +
        environment("SSH_ASKPASS_PROMPT") + '\n' + (arguments.size() > 1 ? arguments[1] : "")
    };
    const bool sent{writeAll(socket, std::to_string(payload.size()) + '\n' + payload)};
    const std::string answer{sent ? readAll(socket) : std::string{}};
    if (!answer.starts_with("1\n")) {
        return EXIT_FAILURE;
    }
    const std::string_view value{std::string_view{answer}.substr(2)};
    return writeAll(STDOUT_FILENO, value) && writeAll(STDOUT_FILENO, "\n") ? EXIT_SUCCESS : EXIT_FAILURE;
}
