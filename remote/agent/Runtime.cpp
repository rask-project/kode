#include "Runtime.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace kode::remote
{

namespace
{

constexpr mode_t privateMode{0700};

std::expected<sockaddr_un, std::string> addressOf(const std::string &path)
{
    sockaddr_un address{};
    if (path.size() >= sizeof(address.sun_path)) {
        return std::unexpected{"The socket's path is too long: " + path};
    }
    address.sun_family = AF_UNIX;
    path.copy(static_cast<char *>(address.sun_path), path.size());
    return address;
}

} // namespace

std::string systemError(const std::string &what)
{
    return what + ": " + std::strerror(errno);
}

bool setNonBlocking(int fd)
{
    const int flags{::fcntl(fd, F_GETFL)};
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

std::expected<std::string, std::string> runtimeFolder()
{
    const char *const runtime{std::getenv("XDG_RUNTIME_DIR")};
    const std::string folder{
        runtime != nullptr && *runtime != '\0' ? std::string{runtime} + "/kode"
                                               : "/tmp/kode-" + std::to_string(::getuid())
    };
    if (::mkdir(folder.c_str(), privateMode) != 0 && errno != EEXIST) {
        return std::unexpected{systemError("Cannot create " + folder)};
    }
    struct stat info{};
    if (::lstat(folder.c_str(), &info) != 0) {
        return std::unexpected{systemError("Cannot read " + folder)};
    }
    if (!S_ISDIR(info.st_mode) || info.st_uid != ::getuid() || (info.st_mode & 077U) != 0U) {
        return std::unexpected{folder + " is not a folder of this user's only: refused."};
    }
    return folder;
}

std::string socketPath(const std::string &folder)
{
    return folder + "/" KODE_REMOTE_VERSION ".sock";
}

std::string lockPath(const std::string &folder)
{
    return folder + "/" KODE_REMOTE_VERSION ".lock";
}

bool peerIsUser(int fd)
{
#if defined(__linux__)
    ucred credentials{};
    socklen_t length{sizeof(credentials)};
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) {
        return false;
    }
    return credentials.uid == ::getuid();
#else
    uid_t uid{0};
    gid_t gid{0};
    return ::getpeereid(fd, &uid, &gid) == 0 && uid == ::getuid();
#endif
}

std::expected<Fd, std::string> listenOn(const std::string &path)
{
    const std::expected<sockaddr_un, std::string> address{addressOf(path)};
    if (!address) {
        return std::unexpected{address.error()};
    }
    Fd socket{::socket(AF_UNIX, SOCK_STREAM, 0)};
    if (!socket.valid()) {
        return std::unexpected{systemError("Cannot make a socket")};
    }
    ::unlink(path.c_str());
    if (::bind(socket.get(), reinterpret_cast<const sockaddr *>(&*address), sizeof(sockaddr_un)) != 0) {
        return std::unexpected{systemError("Cannot listen on " + path)};
    }
    if (::chmod(path.c_str(), 0600) != 0 || ::listen(socket.get(), SOMAXCONN) != 0 || !setNonBlocking(socket.get())) {
        return std::unexpected{systemError("Cannot listen on " + path)};
    }
    ::fcntl(socket.get(), F_SETFD, FD_CLOEXEC);
    return socket;
}

std::expected<Fd, std::string> connectTo(const std::string &path)
{
    const std::expected<sockaddr_un, std::string> address{addressOf(path)};
    if (!address) {
        return std::unexpected{address.error()};
    }
    Fd socket{::socket(AF_UNIX, SOCK_STREAM, 0)};
    if (!socket.valid()) {
        return std::unexpected{systemError("Cannot make a socket")};
    }
    if (::connect(socket.get(), reinterpret_cast<const sockaddr *>(&*address), sizeof(sockaddr_un)) != 0) {
        return std::unexpected{systemError("Cannot connect to " + path)};
    }
    return socket;
}

} // namespace kode::remote
