#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>

namespace kode::codec
{

// How a file's bytes are text: an encoding (IANA name, as in encodings())
// and the byte order mark the file starts with (empty: none).
struct Format {
    QString encoding{QStringLiteral("UTF-8")};
    QByteArray byteOrderMark;
};

// The encodings a document can be read or saved in, most used first.
[[nodiscard]] QStringList encodings();

// The format of `bytes`: by its byte order mark, else UTF-8 if it is valid
// UTF-8, else Windows-1252 (else ISO-8859-1, which decodes anything).
[[nodiscard]] Format detect(const QByteArray &bytes);

// `bytes` (without the byte order mark) as text in `encoding`; nothing if
// they are not valid in it.
[[nodiscard]] std::optional<QString> decode(const QByteArray &bytes, const QString &encoding);

// `text` in `format`; nothing if `format.encoding` cannot represent it all.
[[nodiscard]] std::optional<QByteArray> encode(const QString &text, const Format &format);

// The byte order mark of `encoding`; empty for those without one.
[[nodiscard]] QByteArray byteOrderMark(const QString &encoding);

} // namespace kode::codec
