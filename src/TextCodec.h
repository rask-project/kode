#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringList>

#include <expected>
#include <optional>

class QIODevice;

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

// A file's text and the format it was read in.
struct Decoded {
    QString text;
    Format format;
};

// A file's `bytes` as text in `encoding` (its byte order mark kept when it
// has that encoding's), else in the format detected: by the byte order
// mark, else UTF-8 if valid, else Windows-1252 (else ISO-8859-1, which
// decodes anything). Each candidate is decoded once, not checked first. The
// error is the encoding they are not valid in.
[[nodiscard]] std::expected<Decoded, QString> decodeFile(const QByteArray &bytes, const QString &encoding = {});

// The text of the file open in `device`, as decodeFile() reads it. UTF-8
// without a byte order mark is decoded as it is read, a block at a time:
// a large file's bytes are never all in memory.
[[nodiscard]] std::expected<Decoded, QString> readFile(QIODevice &device, const QString &encoding = {});

// Whether a file starting with `head` is not text: a NUL byte, as git tells,
// unless a UTF-16 byte order mark explains it.
[[nodiscard]] bool isBinary(QByteArrayView head);

// Whether `head` (a file's first bytes, maybe cut inside a character) is valid UTF-8.
[[nodiscard]] bool isUtf8Prefix(QByteArrayView head);

// `bytes` (without the byte order mark) as text in `encoding`; nothing if
// they are not valid in it.
[[nodiscard]] std::optional<QString> decode(const QByteArray &bytes, const QString &encoding);

// `text` in `format`; nothing if `format.encoding` cannot represent it all.
[[nodiscard]] std::optional<QByteArray> encode(const QString &text, const Format &format);

// The byte order mark of `encoding`; empty for those without one.
[[nodiscard]] QByteArray byteOrderMark(const QString &encoding);

} // namespace kode::codec
