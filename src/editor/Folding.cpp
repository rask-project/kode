#include "Folding.h"

#include "TextBuffer.h"

#include <limits>

namespace kode::folding
{

namespace
{

// How far ahead a line looks for the one that shows it opens a block.
constexpr qsizetype blockLookahead{64};
// Lines between checks of a deadline.
constexpr qsizetype linesPerCheck{256};

} // namespace

void FoldIndex::fold(Fold fold)
{
    if (fold.last < fold.first) {
        return;
    }
    m_folds.removeIf([&fold](const Fold &inside) { return inside.first >= fold.first && inside.last <= fold.last; });
    const auto at{std::ranges::lower_bound(m_folds, fold.first, {}, &Fold::first)};
    m_folds.insert(at, fold);
}

bool FoldIndex::unfold(qsizetype line)
{
    return m_folds.removeIf([line](const Fold &fold) { return fold.first == line + 1; }) > 0;
}

bool FoldIndex::reveal(qsizetype line)
{
    return m_folds.removeIf([line](const Fold &fold) { return line >= fold.first && line <= fold.last; }) > 0;
}

bool FoldIndex::foldedUnder(qsizetype line) const
{
    return std::ranges::binary_search(m_folds, line + 1, {}, &Fold::first);
}

std::optional<Fold> FoldIndex::hiding(qsizetype line) const
{
    // The last fold starting at or before `line`.
    const auto after{std::ranges::upper_bound(m_folds, line, {}, &Fold::first)};
    if (after == m_folds.cbegin() || std::prev(after)->last < line) {
        return std::nullopt;
    }
    return *std::prev(after);
}

qsizetype FoldIndex::nextShown(qsizetype line) const
{
    const std::optional<Fold> fold{hiding(line)};
    return fold ? fold->last + 1 : line;
}

void FoldIndex::linesReplaced(qsizetype line, qsizetype removedLines, qsizetype insertedLines)
{
    const qsizetype end{line + removedLines};
    const bool lineBreaks{removedLines > 0 || insertedLines > 0};
    m_folds.removeIf([line, end, &lineBreaks](const Fold &fold) {
        return fold.first - (lineBreaks ? 1 : 0) <= end && fold.last >= line;
    });
    const qsizetype delta{insertedLines - removedLines};
    for (Fold &fold : m_folds) {
        if (fold.first > end) {
            fold.first += delta;
            fold.last += delta;
        }
    }
}

qsizetype indentOf(QStringView text, qsizetype tabWidth)
{
    qsizetype columns{0};
    for (const QChar character : text) {
        if (character == QLatin1Char(' ')) {
            ++columns;
        } else if (character == QLatin1Char('\t')) {
            columns += tabWidth - (columns % tabWidth);
        } else {
            return character.isSpace() ? -1 : columns;
        }
    }
    return -1;
}

bool opensIndentedBlock(const TextBuffer &buffer, qsizetype line, qsizetype tabWidth)
{
    const qsizetype own{indentOf(buffer.line(line), tabWidth)};
    if (own < 0) {
        return false;
    }
    const qsizetype end{std::min(buffer.lineCount(), line + 1 + blockLookahead)};
    for (qsizetype next{line + 1}; next < end; ++next) {
        const qsizetype indent{indentOf(buffer.line(next), tabWidth)};
        if (indent >= 0) {
            return indent > own;
        }
    }
    return false;
}

std::optional<Fold>
indentedBlock(const TextBuffer &buffer, qsizetype line, qsizetype tabWidth, const QDeadlineTimer &deadline)
{
    const qsizetype own{indentOf(buffer.line(line), tabWidth)};
    if (own < 0) {
        return std::nullopt;
    }
    qsizetype last{line};
    for (qsizetype next{line + 1}; next < buffer.lineCount(); ++next) {
        if ((next - line) % linesPerCheck == 0 && deadline.hasExpired()) {
            return std::nullopt;
        }
        const qsizetype indent{indentOf(buffer.line(next), tabWidth)};
        if (indent >= 0 && indent <= own) {
            break;
        }
        if (indent > own) {
            last = next;
        }
    }
    if (last == line) {
        return std::nullopt;
    }
    return Fold{.first = line + 1, .last = last};
}

std::optional<qsizetype>
lineAbove(const TextBuffer &buffer, qsizetype line, qsizetype tabWidth, const QDeadlineTimer &deadline)
{
    qsizetype own{indentOf(buffer.line(line), tabWidth)};
    if (own < 0) {
        own = std::numeric_limits<qsizetype>::max();
    }
    for (qsizetype above{line - 1}; above >= 0; --above) {
        if ((line - above) % linesPerCheck == 0 && deadline.hasExpired()) {
            return std::nullopt;
        }
        const qsizetype indent{indentOf(buffer.line(above), tabWidth)};
        if (indent >= 0 && indent < own) {
            return above;
        }
    }
    return std::nullopt;
}

} // namespace kode::folding
