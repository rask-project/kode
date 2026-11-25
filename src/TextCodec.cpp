#include "TextCodec.h"

#include <CoreFoundation/CoreFoundation.h>
#include <QStringDecoder>
#include <QStringEncoder>

#include <array>
#include <memory>
#include <type_traits>

namespace tekst::codec
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

Format detect(const QByteArray &bytes)
{
    for (const Mark &mark : marks) {
        if (bytes.startsWith(mark.bytes)) {
            return {.encoding = mark.encoding, .byteOrderMark = mark.bytes.toByteArray()};
        }
    }
    if (decode(bytes, QStringLiteral("UTF-8"))) {
        return {};
    }
    if (decode(bytes, QStringLiteral("Windows-1252"))) {
        return {.encoding = QStringLiteral("Windows-1252"), .byteOrderMark = {}};
    }
    return {.encoding = QStringLiteral("ISO-8859-1"), .byteOrderMark = {}};
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

} // namespace tekst::codec
