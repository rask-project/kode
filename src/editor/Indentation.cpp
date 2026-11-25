#include "Indentation.h"

#include "TextBuffer.h"

#include <QStringList>

#include <algorithm>
#include <array>

namespace tekst::indentation
{

namespace
{

bool isIndentation(QChar character)
{
    return character == QLatin1Char(' ') || character == QLatin1Char('\t');
}

// Applies `change` to each line of `lines`.
template <typename Change> QString eachLine(QStringView lines, Change change)
{
    QString result;
    result.reserve(lines.size());
    qsizetype start{0};
    while (true) {
        const qsizetype end{lines.indexOf(QLatin1Char('\n'), start)};
        result.append(change(lines.sliced(start, (end < 0 ? lines.size() : end) - start)));
        if (end < 0) {
            return result;
        }
        result.append(QLatin1Char('\n'));
        start = end + 1;
    }
}

} // namespace

QString unit(Style style)
{
    return style.kind == Kind::Tabs ? QStringLiteral("\t") : QString(style.width, QLatin1Char(' '));
}

QString tabAt(Style style, qsizetype column)
{
    return style.kind == Kind::Tabs ? QStringLiteral("\t")
                                    : QString(style.width - (column % style.width), QLatin1Char(' '));
}

QStringView leadingWhitespace(QStringView line)
{
    const auto end{std::ranges::find_if_not(line, isIndentation)};
    return line.first(end - line.begin());
}

QString indented(QStringView lines, Style style)
{
    const QString level{unit(style)};
    return eachLine(lines, [&level](QStringView line) { return line.isEmpty() ? QString{} : level + line; });
}

QString outdented(QStringView lines, Style style)
{
    return eachLine(lines, [style](QStringView line) {
        if (line.startsWith(QLatin1Char('\t'))) {
            return line.sliced(1).toString();
        }
        const auto pastSpaces{std::ranges::find_if(line, [](QChar character) { return character != QLatin1Char(' '); })
        };
        const qsizetype spaces{pastSpaces - line.begin()};
        return line.sliced(std::min(spaces, qsizetype{style.width})).toString();
    });
}

Style detect(const TextBuffer &buffer, qsizetype maxLines, Style fallback)
{
    qsizetype tabLines{0};
    qsizetype spaceLines{0};
    std::array<qsizetype, 9> steps{};
    qsizetype previous{0};
    const qsizetype count{std::min(maxLines, buffer.lineCount())};
    for (qsizetype line{0}; line < count; ++line) {
        const QString text{buffer.line(line)};
        if (text.trimmed().isEmpty()) {
            continue;
        }
        const QStringView leading{leadingWhitespace(text)};
        if (leading.startsWith(QLatin1Char('\t'))) {
            ++tabLines;
            continue;
        }
        const qsizetype spaces{leading.size()};
        if (spaces > 0) {
            ++spaceLines;
        }
        const qsizetype step{spaces - previous};
        if (step > 0 && step < static_cast<qsizetype>(steps.size())) {
            ++steps.at(static_cast<std::size_t>(step));
        }
        previous = spaces;
    }
    if (tabLines == 0 && spaceLines == 0) {
        return fallback;
    }
    if (tabLines > spaceLines) {
        return {.width = fallback.width, .kind = Kind::Tabs};
    }
    constexpr std::array<quint8, 4> widths{2, 3, 4, 8};
    const quint8 width{*std::ranges::max_element(widths, {}, [&steps](quint8 candidate) {
        return steps.at(static_cast<std::size_t>(candidate));
    })};
    return {.width = steps.at(static_cast<std::size_t>(width)) > 0 ? width : fallback.width, .kind = Kind::Spaces};
}

} // namespace tekst::indentation
