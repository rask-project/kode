#pragma once

#include <QList>

namespace kode
{

class TextBuffer;

// How many visual rows each logical line takes when lines wrap: estimated
// from its length (the font is monospaced), then set exactly once a line
// is laid out. Kept in chunks of lines with their sums, so finding a
// line's first row or the line at a row, and inserting or removing lines,
// never walk every line.
class WrapIndex
{
public:
    // Every line of `buffer` with its estimate at `columns` per row.
    void reset(const TextBuffer &buffer, qsizetype columns);
    // Lines line..line+removedLines became line..line+insertedLines of
    // `buffer` (estimated again).
    void linesReplaced(const TextBuffer &buffer, qsizetype line, qsizetype removedLines, qsizetype insertedLines);
    // `line` was laid out in `rows` rows.
    void setRows(qsizetype line, qsizetype rows);

    [[nodiscard]] qsizetype rows(qsizetype line) const;
    // The first row of `line`.
    [[nodiscard]] qsizetype rowOf(qsizetype line) const;
    // The line that `row` is part of (clamped to the lines).
    [[nodiscard]] qsizetype lineAt(qsizetype row) const;
    [[nodiscard]] qsizetype lineCount() const noexcept { return m_lineCount; }
    [[nodiscard]] qsizetype totalRows() const noexcept { return m_totalRows; }

private:
    struct Chunk {
        QList<qint32> rows;
        qsizetype total{0};
    };

    [[nodiscard]] qint32 estimate(const TextBuffer &buffer, qsizetype line) const;
    // The chunk holding `line` and the line's index in it.
    [[nodiscard]] std::pair<qsizetype, qsizetype> locate(qsizetype line) const;
    void splitIfLarge(qsizetype chunk);

    QList<Chunk> m_chunks;
    qsizetype m_columns{1};
    qsizetype m_lineCount{0};
    qsizetype m_totalRows{0};
};

} // namespace kode
