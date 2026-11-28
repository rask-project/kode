#pragma once

#include "Fd.h"

#include <expected>
#include <string>

namespace kode::remote
{

// Where the daemon's socket lives: $XDG_RUNTIME_DIR/kode, else
// /tmp/kode-<uid>; made if missing, and refused unless it is a real folder
// of this user that no one else can enter.
[[nodiscard]] std::expected<std::string, std::string> runtimeFolder();
[[nodiscard]] std::string socketPath(const std::string &folder);
[[nodiscard]] std::string lockPath(const std::string &folder);

// The user on the other end of a unix socket is this one.
[[nodiscard]] bool peerIsUser(int fd);

[[nodiscard]] std::expected<Fd, std::string> listenOn(const std::string &path);
[[nodiscard]] std::expected<Fd, std::string> connectTo(const std::string &path);

[[nodiscard]] bool setNonBlocking(int fd);
[[nodiscard]] std::string systemError(const std::string &what);

} // namespace kode::remote
