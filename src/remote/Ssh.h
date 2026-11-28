#pragma once

#include <QByteArrayView>
#include <QString>
#include <QStringList>

#include <optional>

// How Kode talks to OpenSSH: the options it adds over the user's config and
// the commands it runs on the host (POSIX sh, whatever the login shell).
namespace kode::remote::ssh
{

// A destination as ~/.ssh/config or the user names it ("box", "me@box",
// "me@box:2222" is not one: ssh takes the port with -p). Refused when it
// could be read as an option or holds spaces or control characters.
[[nodiscard]] bool validDestination(const QString &destination);

// ssh's arguments for `remoteCommand` on `destination`: one master
// connection per host (ControlMaster in `controlFolder`), no forwarding of
// the agent, X11 or ports, dead links noticed.
[[nodiscard]] QStringList
arguments(const QString &destination, const QString &controlFolder, const QString &remoteCommand);

// `text` as one word for sh.
[[nodiscard]] QString quote(const QString &text);

// Prints the host's system, architecture, home and the SHA-256 of the
// agent of `version` installed there ("none" when missing).
[[nodiscard]] QString probeCommand(const QString &version);
// Installs the agent arriving on stdin for `version` (folders 0700, other
// versions removed), then prints its SHA-256.
[[nodiscard]] QString installCommand(const QString &version);
// Runs the installed agent's bridge.
[[nodiscard]] QString bridgeCommand(const QString &home, const QString &version);

struct Probe {
    // "linux", "darwin"…
    QString os;
    // "x86_64", "arm64"
    QString arch;
    QString home;
    // Empty when no agent is installed.
    QString hash;
};
[[nodiscard]] std::optional<Probe> parseProbe(QByteArrayView output);

// ssh failed for a reason that may pass (the network), not one that will
// not (authentication, a host key, an unknown name).
[[nodiscard]] bool isTransient(const QString &error);

// The hosts named in an ssh config file (and the files it includes), without
// patterns.
[[nodiscard]] QStringList configuredHosts(const QString &configPath);

} // namespace kode::remote::ssh
