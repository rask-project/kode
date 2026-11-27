#include "Protocol.h"

#include "TextBuffer.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QList>

#include <algorithm>

namespace kode::lsp
{

qsizetype positionIn(const TextBuffer &buffer, const QJsonValue &position)
{
    const qsizetype line{std::clamp(position[QStringLiteral("line")].toInteger(), qint64{0}, buffer.lineCount() - 1)};
    const qsizetype character{
        std::clamp(position[QStringLiteral("character")].toInteger(), qint64{0}, buffer.lineLength(line))
    };
    return buffer.lineStart(line) + character;
}

namespace
{

constexpr QByteArrayView headerEnd{"\r\n\r\n"};
constexpr QByteArrayView contentLengthField{"content-length:"};

// The Content-Length of `header` (its lines, without the blank one); -1 when it has none.
qsizetype contentLength(QByteArrayView header)
{
    const QList<QByteArray> fields{header.toByteArray().split('\n')};
    const auto field{std::ranges::find_if(fields, [](const QByteArray &line) {
        return line.trimmed().toLower().startsWith(contentLengthField);
    })};
    if (field == fields.cend()) {
        return -1;
    }
    bool valid{false};
    const qsizetype length{field->trimmed().mid(contentLengthField.size()).trimmed().toLongLong(&valid)};
    return valid && length >= 0 ? length : -1;
}

} // namespace

QByteArray frame(const QJsonObject &message)
{
    const QByteArray body{QJsonDocument{message}.toJson(QJsonDocument::Compact)};
    return QByteArrayLiteral("Content-Length: ") + QByteArray::number(body.size()) + headerEnd.toByteArray() + body;
}

void MessageReader::append(QByteArrayView bytes)
{
    m_buffer.append(bytes);
}

std::expected<std::optional<QJsonObject>, QString> MessageReader::next()
{
    if (m_bodyLength < 0) {
        const qsizetype end{m_buffer.indexOf(headerEnd)};
        if (end < 0) {
            return std::optional<QJsonObject>{};
        }
        m_bodyLength = contentLength(QByteArrayView{m_buffer}.first(end));
        m_buffer.remove(0, end + headerEnd.size());
        if (m_bodyLength < 0) {
            return std::unexpected{QStringLiteral("A message without a Content-Length")};
        }
    }
    if (m_buffer.size() < m_bodyLength) {
        return std::optional<QJsonObject>{};
    }
    QJsonParseError error{};
    const QJsonDocument document{QJsonDocument::fromJson(m_buffer.first(m_bodyLength), &error)};
    m_buffer.remove(0, m_bodyLength);
    m_bodyLength = -1;
    if (error.error != QJsonParseError::NoError) {
        return std::unexpected{error.errorString()};
    }
    if (!document.isObject()) {
        return std::unexpected{QStringLiteral("A message that is not a JSON object")};
    }
    return document.object();
}

} // namespace kode::lsp
