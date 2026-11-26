#pragma once

#include <QDeadlineTimer>
#include <QList>
#include <QStringView>

#include <algorithm>
#include <optional>

namespace kode
{

class TextBuffer;

namespace folding
{

// Lines first..last hidden under the line before them.
struct Fold {
    qsizetype first{0};
    qsizetype last{0};
    friend bool operator==(const Fold &, const Fold &) = default;
};

// The folds of a view: sorted and disjoint (folding around a fold takes it
// in). Maps visual rows past them, over rows from `rowOf(line)` (a line's
// first row without folds; rowOf(lineCount) is the total) and
// `lineAt(row)`: wrapping comes from the caller.
class FoldIndex
{
public:
    [[nodiscard]] bool isEmpty() const noexcept { return m_folds.isEmpty(); }
    [[nodiscard]] const QList<Fold> &folds() const noexcept { return m_folds; }
    void clear() { m_folds.clear(); }
    void fold(Fold fold);
    // Unfolds what is hidden under `line`; false when nothing is.
    bool unfold(qsizetype line);
    // Unfolds whatever hides `line`; false when nothing does.
    bool reveal(qsizetype line);
    [[nodiscard]] bool foldedUnder(qsizetype line) const;
    // The fold hiding `line`, if any.
    [[nodiscard]] std::optional<Fold> hiding(qsizetype line) const;
    // `line`, or the first line after the fold hiding it.
    [[nodiscard]] qsizetype nextShown(qsizetype line) const;
    // Lines line..line+removedLines became line..line+insertedLines: folds
    // the edit touches open (a line break at the folded line too), later ones move.
    void linesReplaced(qsizetype line, qsizetype removedLines, qsizetype insertedLines);

    // The first row of `line`; a hidden line is on its folded line's last row.
    template <typename RowOf> [[nodiscard]] qsizetype rowOf(qsizetype line, RowOf rowOf) const
    {
        qsizetype hidden{0};
        for (const Fold &fold : m_folds) {
            if (line < fold.first) {
                break;
            }
            if (line <= fold.last) {
                return rowOf(fold.first) - 1 - hidden;
            }
            hidden += rowOf(fold.last + 1) - rowOf(fold.first);
        }
        return rowOf(line) - hidden;
    }

    // The shown line that `row` (from 0 to totalRows() - 1) is part of.
    template <typename RowOf, typename LineAt>
    [[nodiscard]] qsizetype lineAt(qsizetype row, RowOf rowOf, LineAt lineAt) const
    {
        qsizetype hidden{0};
        for (const Fold &fold : m_folds) {
            if (rowOf(fold.first) - hidden > row) {
                break;
            }
            hidden += rowOf(fold.last + 1) - rowOf(fold.first);
        }
        return lineAt(row + hidden);
    }

    template <typename RowOf> [[nodiscard]] qsizetype hiddenRows(RowOf rowOf) const
    {
        qsizetype hidden{0};
        for (const Fold &fold : m_folds) {
            hidden += rowOf(fold.last + 1) - rowOf(fold.first);
        }
        return hidden;
    }

private:
    QList<Fold> m_folds;
};

// Folding by indentation (Python, YAML…): the columns `text` is indented
// by, tabs to `tabWidth`; -1 for a blank line.
[[nodiscard]] qsizetype indentOf(QStringView text, qsizetype tabWidth);
// The next line that is not blank is indented more than `line` (looking a
// bounded way ahead).
[[nodiscard]] bool opensIndentedBlock(const TextBuffer &buffer, qsizetype line, qsizetype tabWidth);
// The lines indented under `line`, without the blank ones at the end;
// none when there are none or `deadline` passes first.
[[nodiscard]] std::optional<Fold>
indentedBlock(const TextBuffer &buffer, qsizetype line, qsizetype tabWidth, const QDeadlineTimer &deadline);
// The closest line above `line` indented less (that it may be under).
[[nodiscard]] std::optional<qsizetype>
lineAbove(const TextBuffer &buffer, qsizetype line, qsizetype tabWidth, const QDeadlineTimer &deadline);

} // namespace folding

} // namespace kode
