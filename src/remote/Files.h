#pragma once

#include "Connection.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <expected>
#include <functional>
#include <optional>

// The agent's fs service, as Kode asks it (docs/REMOTE.md, section 5).
namespace kode::remote::files
{

// A version of a file as the host keeps it; the hash is the SHA-256 of the
// bytes Kode read or wrote (empty when it only stat-ed it).
struct Version {
    QByteArray hash;
    qint64 size{-1};
    // The host's time: compared only with the host's.
    qint64 seconds{0};
    qint64 nanoseconds{0};

    [[nodiscard]] bool valid() const noexcept { return size >= 0; }
    // Same size and time: the same version as far as the host tells.
    [[nodiscard]] bool sameAs(const Version &other) const noexcept
    {
        return size == other.size && seconds == other.seconds && nanoseconds == other.nanoseconds;
    }
};

struct Entry {
    QString name;
    // "file", "folder", "link", "other", "missing".
    QString type;
    // A link's: what it names.
    QString targetType;
    Version version;

    [[nodiscard]] bool isFolder() const
    {
        return type == QLatin1String("folder") ||
               (type == QLatin1String("link") && targetType == QLatin1String("folder"));
    }
};

struct Read {
    QByteArray bytes;
    Version version;
};

[[nodiscard]] Version versionOf(const QJsonObject &info);

void read(Connection &connection, const QString &path, std::function<void(std::expected<Read, Error>)> done);
// `expect`: the version the bytes replace (a different one is a conflict:
// error::conflict, its data the version now). `makeFolders` for a new file's folders.
void write(
    Connection &connection,
    const QString &path,
    const QByteArray &bytes,
    const std::optional<Version> &expect,
    bool makeFolders,
    std::function<void(std::expected<Version, Error>)> done
);
// Each path's entry (named by its path), in order.
void stat(
    Connection &connection,
    const QStringList &paths,
    std::function<void(std::expected<QList<Entry>, Error>)> done
);
void list(Connection &connection, const QString &folder, std::function<void(std::expected<QList<Entry>, Error>)> done);

} // namespace kode::remote::files
