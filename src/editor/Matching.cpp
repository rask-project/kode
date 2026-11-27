#include "Matching.h"

#include "TextBuffer.h"

#include <algorithm>
#include <array>

namespace kode::matching
{

namespace
{

constexpr std::array<std::pair<char16_t, char16_t>, 3> pairs{{{u'(', u')'}, {u'[', u']'}, {u'{', u'}'}}};

// What to look for: `same` nests, `other` closes; `step` is +1 (forward) or -1.
struct Bracket {
    QChar same;
    QChar other;
    qint16 step{1};
};

std::optional<Bracket> bracketFor(QChar character)
{
    for (const auto &[opening, closing] : pairs) {
        if (character == QChar{opening}) {
            return Bracket{.same = QChar{opening}, .other = QChar{closing}, .step = 1};
        }
        if (character == QChar{closing}) {
            return Bracket{.same = QChar{closing}, .other = QChar{opening}, .step = -1};
        }
    }
    return std::nullopt;
}

bool inside(const QList<indentation::Span> &spans, qsizetype column)
{
    return std::ranges::any_of(spans, [column](const indentation::Span &span) {
        return column >= span.start && column < span.end;
    });
}

// Asked only of lines with a bracket of the kind sought.
QList<indentation::Span> ignored(const NonCode &nonCode, qsizetype line, QStringView text, Bracket bracket)
{
    if (!nonCode || (!text.contains(bracket.same) && !text.contains(bracket.other))) {
        return {};
    }
    return nonCode(line, text);
}

// Scans from the bracket at `position` for its pair, line by line.
std::optional<qsizetype>
scan(const TextBuffer &buffer, qsizetype position, Bracket bracket, qsizetype maxLines, const NonCode &nonCode)
{
    const qsizetype firstLine{buffer.lineAt(position)};
    const qsizetype step{bracket.step};
    qsizetype depth{0};
    for (qsizetype line{firstLine}; line >= 0 && line < buffer.lineCount() && std::abs(line - firstLine) <= maxLines;
         line += step) {
        const QString text{buffer.line(line)};
        const qsizetype lineStart{buffer.lineStart(line)};
        const QList<indentation::Span> skipped{ignored(nonCode, line, text, bracket)};
        qsizetype column{line == firstLine ? position - lineStart : (step > 0 ? 0 : text.size() - 1)};
        for (; column >= 0 && column < text.size(); column += step) {
            const QChar character{text.at(column)};
            if ((character != bracket.same && character != bracket.other) || inside(skipped, column)) {
                continue;
            }
            if (character == bracket.same) {
                ++depth;
            } else if (character == bracket.other && --depth == 0) {
                return lineStart + column;
            }
        }
    }
    return std::nullopt;
}

// Whether the character at `position` is in a comment or a string.
bool isNonCode(const TextBuffer &buffer, qsizetype position, const NonCode &nonCode)
{
    if (!nonCode) {
        return false;
    }
    const qsizetype line{buffer.lineAt(position)};
    return inside(nonCode(line, buffer.line(line)), position - buffer.lineStart(line));
}

} // namespace

bool isWordCharacter(QChar character)
{
    return character.isLetterOrNumber() || character == QLatin1Char('_');
}

std::optional<BracketPair>
matchingBracket(const TextBuffer &buffer, qsizetype position, qsizetype maxLines, const NonCode &nonCode)
{
    for (const qsizetype at : {position, position - 1}) {
        if (at < 0 || at >= buffer.length()) {
            continue;
        }
        const std::optional<Bracket> bracket{bracketFor(buffer.text(at, 1).front())};
        if (!bracket || isNonCode(buffer, at, nonCode)) {
            continue;
        }
        if (const std::optional<qsizetype> match{scan(buffer, at, *bracket, maxLines, nonCode)}) {
            return BracketPair{.bracket = at, .match = *match};
        }
        return std::nullopt;
    }
    return std::nullopt;
}

Range wordAt(QStringView text, qsizetype column)
{
    qsizetype start{std::min(column, text.size())};
    qsizetype end{start};
    while (start > 0 && isWordCharacter(text.at(start - 1))) {
        --start;
    }
    while (end < text.size() && isWordCharacter(text.at(end))) {
        ++end;
    }
    return {.start = start, .end = end};
}

QList<Range> wholeWords(QStringView text, QStringView word)
{
    QList<Range> found;
    if (word.isEmpty()) {
        return found;
    }
    for (qsizetype at{text.indexOf(word)}; at >= 0; at = text.indexOf(word, at + word.size())) {
        const qsizetype end{at + word.size()};
        const bool startsWord{at == 0 || !isWordCharacter(text.at(at - 1))};
        const bool endsWord{end == text.size() || !isWordCharacter(text.at(end))};
        if (startsWord && endsWord) {
            found.append({.start = at, .end = end});
        }
    }
    return found;
}

} // namespace kode::matching
