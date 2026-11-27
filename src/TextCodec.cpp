#include "TextCodec.h"

#include <CoreFoundation/CoreFoundation.h>
#include <QIODevice>
#include <QStringDecoder>
#include <QStringEncoder>

#include <algorithm>
#include <array>
#include <memory>
#include <type_traits>

namespace kode::codec
{

namespace
{

struct CFRelease {
    void operator()(CFTypeRef object) const noexcept { ::CFRelease(object); }
};
using CFString = std::unique_ptr<std::remove_pointer_t<CFStringRef>, CFRelease>;

// The encodings Qt converts itself; the rest go through CoreFoundation.
std::optional<QStringConverter::Encoding> qtEncoding(const QString &name)
{
    if (name == QLatin1StringView{"UTF-8"}) {
        return QStringConverter::Utf8;
    }
    if (name == QLatin1StringView{"UTF-16LE"}) {
        return QStringConverter::Utf16LE;
    }
    if (name == QLatin1StringView{"UTF-16BE"}) {
        return QStringConverter::Utf16BE;
    }
    if (name == QLatin1StringView{"ISO-8859-1"}) {
        return QStringConverter::Latin1;
    }
    return std::nullopt;
}

// CoreFoundation's Boolean is an unsigned char.
constexpr Boolean cfFalse{0};

// The CoreFoundation encoding named `name`; nothing if it has none.
std::optional<CFStringEncoding> cfEncoding(const QString &name)
{
    const CFString iana{name.toCFString()};
    const CFStringEncoding encoding{CFStringConvertIANACharSetNameToEncoding(iana.get())};
    if (encoding == kCFStringEncodingInvalidId || CFStringIsEncodingAvailable(encoding) == cfFalse) {
        return std::nullopt;
    }
    return encoding;
}

struct Mark {
    QLatin1StringView encoding;
    QByteArrayView bytes;
};

// Without a byte order mark, tried in turn.
const std::array<QString, 3> detected{
    QStringLiteral("UTF-8"),
    QStringLiteral("Windows-1252"),
    QStringLiteral("ISO-8859-1"),
};

const std::array<Mark, 3> marks{{
    {.encoding = QLatin1StringView{"UTF-8"}, .bytes = QByteArrayView{"\xEF\xBB\xBF", 3}},
    {.encoding = QLatin1StringView{"UTF-16LE"}, .bytes = QByteArrayView{"\xFF\xFE", 2}},
    {.encoding = QLatin1StringView{"UTF-16BE"}, .bytes = QByteArrayView{"\xFE\xFF", 2}},
}};

} // namespace

QStringList encodings()
{
    return {
        QStringLiteral("UTF-8"),        QStringLiteral("UTF-16LE"),    QStringLiteral("UTF-16BE"),
        QStringLiteral("ISO-8859-1"),   QStringLiteral("ISO-8859-15"), QStringLiteral("Windows-1252"),
        QStringLiteral("macintosh"),    QStringLiteral("ISO-8859-2"),  QStringLiteral("Windows-1250"),
        QStringLiteral("Windows-1251"), QStringLiteral("KOI8-R"),      QStringLiteral("ISO-8859-7"),
        QStringLiteral("Windows-1253"), QStringLiteral("ISO-8859-9"),  QStringLiteral("Windows-1254"),
        QStringLiteral("Shift_JIS"),    QStringLiteral("EUC-JP"),      QStringLiteral("GB18030"),
        QStringLiteral("Big5"),         QStringLiteral("EUC-KR"),
    };
}

namespace
{

constexpr qint64 readBlock{qint64{4} * 1024 * 1024};

// How much of `bytes` ends on a whole UTF-8 character: the last one left
// out when its bytes may go on past the end.
qsizetype wholeCharacters(QByteArrayView bytes)
{
    const qsizetype size{bytes.size()};
    for (qsizetype back{1}; back <= 4 && back <= size; ++back) {
        const auto byte{static_cast<quint8>(bytes.at(size - back))};
        if ((byte & 0xC0U) == 0x80U) {
            continue;
        }
        if (byte < 0x80U) {
            return size;
        }
        const qsizetype length{byte >= 0xF0U ? 4 : (byte >= 0xE0U ? 3 : 2)};
        return length > back ? size - back : size;
    }
    // Not UTF-8: the decoder tells.
    return size;
}

// The rest of `device` as UTF-8, decoded block by block into the text;
// nothing if it is not valid UTF-8 or cannot be read.
std::optional<QString> readUtf8(QIODevice &device)
{
    QStringDecoder decoder{QStringConverter::Utf8};
    const qint64 expected{device.size() - device.pos()};
    QString text(decoder.requiredSpace(expected) + (expected / readBlock) + 1, Qt::Uninitialized);
    qsizetype length{0};
    // A block's last character, cut, starts the next one.
    QByteArray block(readBlock + 4, Qt::Uninitialized);
    qsizetype carried{0};
    while (true) {
        const qint64 read{device.read(block.data() + carried, readBlock)};
        if (read < 0) {
            return std::nullopt;
        }
        const QByteArrayView bytes{block.constData(), carried + read};
        const qsizetype whole{wholeCharacters(bytes)};
        if (read == 0 && whole < bytes.size()) {
            return std::nullopt;
        }
        // The file may have grown since its size was taken.
        if (const qsizetype needed{length + decoder.requiredSpace(whole)}; needed > text.size()) {
            text.resize(needed);
        }
        length = decoder.appendToBuffer(text.data() + length, bytes.first(whole)) - text.constData();
        if (decoder.hasError()) {
            return std::nullopt;
        }
        if (read == 0) {
            break;
        }
        carried = bytes.size() - whole;
        std::ranges::copy(bytes.sliced(whole), block.begin());
    }
    text.truncate(length);
    return text;
}

} // namespace

std::expected<Decoded, QString> readFile(QIODevice &device, const QString &encoding)
{
    const QByteArray head{device.peek(4)};
    const bool marked{std::ranges::any_of(marks, [&head](const Mark &mark) { return head.startsWith(mark.bytes); })};
    if (encoding.isEmpty() && !marked) {
        if (std::optional<QString> text{readUtf8(device)}) {
            return Decoded{.text = std::move(*text), .format = {}};
        }
        device.seek(0);
    }
    return decodeFile(device.readAll(), encoding);
}

std::expected<Decoded, QString> decodeFile(const QByteArray &bytes, const QString &encoding)
{
    const auto mark{std::ranges::find_if(marks, [&bytes](const Mark &candidate) {
        return bytes.startsWith(candidate.bytes);
    })};
    const bool marked{mark != marks.end() && (encoding.isEmpty() || encoding == mark->encoding)};
    if (marked || !encoding.isEmpty()) {
        Format format{
            marked ? Format{.encoding = mark->encoding, .byteOrderMark = mark->bytes.toByteArray()}
                   : Format{.encoding = encoding, .byteOrderMark = {}}
        };
        std::optional<QString> text{decode(bytes.sliced(format.byteOrderMark.size()), format.encoding)};
        if (!text) {
            return std::unexpected{format.encoding};
        }
        return Decoded{.text = std::move(*text), .format = std::move(format)};
    }
    for (const QString &candidate : detected) {
        if (std::optional<QString> text{decode(bytes, candidate)}) {
            return Decoded{.text = std::move(*text), .format = {.encoding = candidate, .byteOrderMark = {}}};
        }
    }
    return std::unexpected{detected.back()};
}

bool isBinary(QByteArrayView head)
{
    const bool utf16{std::ranges::any_of(marks, [head](const Mark &mark) {
        return mark.encoding.startsWith(QLatin1StringView{"UTF-16"}) && head.startsWith(mark.bytes);
    })};
    return !utf16 && head.contains('\0');
}

bool isUtf8Prefix(QByteArrayView head)
{
    // Stateful: a character cut at the end waits for more instead of failing.
    QStringDecoder decoder{QStringConverter::Utf8};
    [[maybe_unused]] const QString text{decoder.decode(head)};
    return !decoder.hasError();
}

QByteArray byteOrderMark(const QString &encoding)
{
    const auto mark{std::ranges::find(marks, encoding, &Mark::encoding)};
    return mark != marks.end() ? mark->bytes.toByteArray() : QByteArray{};
}

std::optional<QString> decode(const QByteArray &bytes, const QString &encoding)
{
    if (const std::optional<QStringConverter::Encoding> qt{qtEncoding(encoding)}) {
        QStringDecoder decoder{*qt, QStringConverter::Flag::Stateless};
        QString text{decoder.decode(bytes)};
        if (decoder.hasError()) {
            return std::nullopt;
        }
        return text;
    }
    const std::optional<CFStringEncoding> cf{cfEncoding(encoding)};
    if (!cf) {
        return std::nullopt;
    }
    const CFString text{CFStringCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8 *>(bytes.constData()),
        bytes.size(),
        *cf,
        cfFalse
    )};
    if (text == nullptr) {
        return std::nullopt;
    }
    return QString::fromCFString(text.get());
}

std::optional<QByteArray> encode(const QString &text, const Format &format)
{
    QByteArray bytes{format.byteOrderMark};
    if (const std::optional<QStringConverter::Encoding> qt{qtEncoding(format.encoding)}) {
        // Qt writes '?' for what Latin-1 lacks, without reporting it.
        if (*qt == QStringConverter::Latin1 &&
            std::ranges::any_of(text, [](QChar character) { return character.unicode() > 0xFFU; })) {
            return std::nullopt;
        }
        QStringEncoder encoder{*qt, QStringConverter::Flag::Stateless};
        bytes.append(encoder.encode(text));
        if (encoder.hasError()) {
            return std::nullopt;
        }
        return bytes;
    }
    const std::optional<CFStringEncoding> cf{cfEncoding(format.encoding)};
    if (!cf) {
        return std::nullopt;
    }
    const CFString source{text.toCFString()};
    const CFRange range{CFRangeMake(0, CFStringGetLength(source.get()))};
    CFIndex length{0};
    // Measure, then convert; no loss byte, so a character it cannot
    // represent stops the conversion short.
    if (CFStringGetBytes(source.get(), range, *cf, 0, cfFalse, nullptr, 0, &length) != range.length) {
        return std::nullopt;
    }
    const qsizetype offset{bytes.size()};
    bytes.resize(offset + length);
    CFStringGetBytes(
        source.get(),
        range,
        *cf,
        0,
        cfFalse,
        reinterpret_cast<UInt8 *>(bytes.data() + offset),
        length,
        nullptr
    );
    return bytes;
}

} // namespace kode::codec
