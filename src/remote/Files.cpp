#include "Files.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QPointer>

namespace kode::remote::files
{

namespace
{

QByteArray hashOf(QByteArrayView bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

Entry entryOf(const QJsonObject &object, const QString &nameKey)
{
    return Entry{
        .name = object.value(nameKey).toString(),
        .type = object.value(QStringLiteral("type")).toString(),
        .targetType = object.value(QStringLiteral("targetType")).toString(),
        .version = versionOf(object),
    };
}

QList<Entry> entriesOf(const QJsonValue &result, const QString &nameKey)
{
    QList<Entry> entries;
    // Parentheses: braces would pick QJsonArray's initializer_list, an array holding the array.
    const QJsonArray array(result.toObject().value(QStringLiteral("entries")).toArray());
    entries.reserve(array.size());
    std::ranges::for_each(array, [&](const QJsonValue &value) {
        entries.emplace_back(entryOf(value.toObject(), nameKey));
    });
    return entries;
}

QJsonObject versionObject(const Version &version)
{
    return {
        {QStringLiteral("size"), version.size},
        {QStringLiteral("mtime"), version.seconds},
        {QStringLiteral("mtimeNs"), version.nanoseconds},
        {QStringLiteral("hash"), QString::fromLatin1(version.hash)},
    };
}

} // namespace

Version versionOf(const QJsonObject &info)
{
    if (!info.contains(QStringLiteral("size"))) {
        return {};
    }
    return Version{
        .hash = {},
        .size = info.value(QStringLiteral("size")).toInteger(-1),
        .seconds = info.value(QStringLiteral("mtime")).toInteger(),
        .nanoseconds = info.value(QStringLiteral("mtimeNs")).toInteger(),
    };
}

void read(Connection &connection, const QString &path, std::function<void(std::expected<Read, Error>)> done)
{
    const QPointer<Connection> link{&connection};
    connection.request(
        QStringLiteral("fs/read"),
        {{QStringLiteral("path"), path}},
        [link, done = std::move(done)](const Connection::Result &result) {
            if (!result) {
                done(std::unexpected{result.error()});
                return;
            }
            const QJsonObject info{result->toObject()};
            const auto channel{static_cast<quint32>(info.value(QStringLiteral("channel")).toInteger())};
            if (link == nullptr || channel == 0U) {
                done(std::unexpected{Error{
                    .message = QCoreApplication::translate("kode::remote", "The host sent no file."),
                    .data = {},
                    .code = 0
                }});
                return;
            }
            link->receive(
                channel,
                [done,
                 read = Read{.bytes = {}, .version = versionOf(info)}](QByteArrayView piece, ChannelEnd end) mutable {
                    switch (end) {
                    case ChannelEnd::More:
                        read.bytes.append(piece);
                        return;
                    case ChannelEnd::Complete:
                        read.version.hash = hashOf(read.bytes);
                        done(std::move(read));
                        return;
                    case ChannelEnd::Failed:
                        done(std::unexpected{Error{
                            .message = QCoreApplication::translate(
                                "kode::remote",
                                "The file changed while it was read, or the connection was lost."
                            ),
                            .data = {},
                            .code = 0,
                        }});
                        return;
                    }
                }
            );
        }
    );
}

void write(
    Connection &connection,
    const QString &path,
    const QByteArray &bytes,
    const std::optional<Version> &expect,
    bool makeFolders,
    std::function<void(std::expected<Version, Error>)> done
)
{
    const quint32 channel{connection.openChannel()};
    QJsonObject params{
        {QStringLiteral("path"), path},
        {QStringLiteral("channel"), static_cast<qint64>(channel)},
        {QStringLiteral("makeFolders"), makeFolders},
    };
    if (expect && expect->valid()) {
        params.insert(QStringLiteral("expect"), versionObject(*expect));
    }
    connection.request(
        QStringLiteral("fs/write"),
        params,
        [done = std::move(done), hash = hashOf(bytes)](const Connection::Result &result) {
            if (!result) {
                done(std::unexpected{result.error()});
                return;
            }
            Version written{versionOf(result->toObject())};
            written.hash = hash;
            done(written);
        }
    );
    connection.send(channel, bytes);
}

void stat(
    Connection &connection,
    const QStringList &paths,
    std::function<void(std::expected<QList<Entry>, Error>)> done
)
{
    connection.request(
        QStringLiteral("fs/stat"),
        {{QStringLiteral("paths"), QJsonArray::fromStringList(paths)}},
        [done = std::move(done)](const Connection::Result &result) {
            if (!result) {
                done(std::unexpected{result.error()});
                return;
            }
            done(entriesOf(*result, QStringLiteral("path")));
        }
    );
}

void list(Connection &connection, const QString &folder, std::function<void(std::expected<QList<Entry>, Error>)> done)
{
    connection.request(
        QStringLiteral("fs/list"),
        {{QStringLiteral("path"), folder}},
        [done = std::move(done)](const Connection::Result &result) {
            if (!result) {
                done(std::unexpected{result.error()});
                return;
            }
            done(entriesOf(*result, QStringLiteral("name")));
        }
    );
}

} // namespace kode::remote::files
