#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QJsonObject>
#include <QString>

#include <expected>
#include <optional>

namespace kode::lsp
{

// `message` as the Language Server Protocol sends it: a Content-Length
// header, then the JSON.
[[nodiscard]] QByteArray frame(const QJsonObject &message);

// The messages in a stream of bytes that arrives in pieces.
class MessageReader
{
public:
    void append(QByteArrayView bytes);
    // The next whole message; none until more bytes come; an error for a
    // malformed one, which is skipped.
    [[nodiscard]] std::expected<std::optional<QJsonObject>, QString> next();

private:
    QByteArray m_buffer;
    // The length of the body whose header was read; -1 before the header.
    qsizetype m_bodyLength{-1};
};

} // namespace kode::lsp
