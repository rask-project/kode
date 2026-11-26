#include "WrapIndex.h"

#include "TextBuffer.h"

#include <algorithm>
#include <numeric>

namespace kode
{

namespace
{

// Lines per chunk after a reset; a chunk splits past twice that.
constexpr qsizetype chunkLines{1024};

} // namespace

qint32 WrapIndex::estimate(const TextBuffer &buffer, qsizetype line) const
{
    const qsizetype length{buffer.lineLength(line)};
    return static_cast<qint32>(std::max(qsizetype{1}, (length + m_columns - 1) / m_columns));
}

void WrapIndex::reset(const TextBuffer &buffer, qsizetype columns)
{
    m_columns = std::max(qsizetype{1}, columns);
    m_lineCount = buffer.lineCount();
    m_totalRows = 0;
    m_chunks.clear();
    m_chunks.reserve((m_lineCount / chunkLines) + 1);
    const QList<qsizetype> lengths{buffer.lineLengths()};
    for (qsizetype first{0}; first < m_lineCount; first += chunkLines) {
        Chunk chunk;
        const qsizetype count{std::min(chunkLines, m_lineCount - first)};
        chunk.rows.reserve(count);
        for (const qsizetype length : lengths.sliced(first, count)) {
            chunk.rows.append(static_cast<qint32>(std::max(qsizetype{1}, (length + m_columns - 1) / m_columns)));
        }
        chunk.total = std::accumulate(chunk.rows.cbegin(), chunk.rows.cend(), qsizetype{0});
        m_totalRows += chunk.total;
        m_chunks.append(std::move(chunk));
    }
}

std::pair<qsizetype, qsizetype> WrapIndex::locate(qsizetype line) const
{
    qsizetype chunk{0};
    while (chunk + 1 < m_chunks.size() && line >= m_chunks.at(chunk).rows.size()) {
        line -= m_chunks.at(chunk).rows.size();
        ++chunk;
    }
    return {chunk, line};
}

void WrapIndex::splitIfLarge(qsizetype chunk)
{
    Chunk &large{m_chunks[chunk]};
    if (large.rows.size() <= 2 * chunkLines) {
        return;
    }
    Chunk second;
    second.rows = large.rows.sliced(chunkLines);
    large.rows.resize(chunkLines);
    second.total = std::accumulate(second.rows.cbegin(), second.rows.cend(), qsizetype{0});
    large.total -= second.total;
    m_chunks.insert(chunk + 1, std::move(second));
}

void WrapIndex::linesReplaced(const TextBuffer &buffer, qsizetype line, qsizetype removedLines, qsizetype insertedLines)
{
    if (m_chunks.isEmpty()) {
        reset(buffer, m_columns);
        return;
    }
    // Remove the old lines (the edited one included) chunk by chunk...
    auto [chunk, index]{locate(line)};
    qsizetype toRemove{removedLines + 1};
    qsizetype at{chunk};
    qsizetype from{index};
    while (toRemove > 0 && at < m_chunks.size()) {
        Chunk &current{m_chunks[at]};
        const qsizetype count{std::min(toRemove, current.rows.size() - from)};
        const qsizetype removed{
            std::accumulate(current.rows.cbegin() + from, current.rows.cbegin() + from + count, qsizetype{0})
        };
        current.rows.remove(from, count);
        current.total -= removed;
        m_totalRows -= removed;
        m_lineCount -= count;
        toRemove -= count;
        if (current.rows.isEmpty() && m_chunks.size() > 1) {
            m_chunks.removeAt(at);
        } else {
            ++at;
        }
        from = 0;
    }
    // ...and insert the new ones where they were.
    if (chunk >= m_chunks.size()) {
        chunk = m_chunks.size() - 1;
        index = m_chunks.at(chunk).rows.size();
    }
    index = std::min(index, m_chunks.at(chunk).rows.size());
    Chunk &target{m_chunks[chunk]};
    for (qsizetype offset{0}; offset <= insertedLines; ++offset) {
        const qint32 rows{estimate(buffer, line + offset)};
        target.rows.insert(index + offset, rows);
        target.total += rows;
        m_totalRows += rows;
        ++m_lineCount;
    }
    splitIfLarge(chunk);
}

void WrapIndex::setRows(qsizetype line, qsizetype rows)
{
    if (line < 0 || line >= m_lineCount) {
        return;
    }
    const auto [chunk, index]{locate(line)};
    Chunk &target{m_chunks[chunk]};
    const qsizetype delta{rows - target.rows.at(index)};
    target.rows[index] = static_cast<qint32>(rows);
    target.total += delta;
    m_totalRows += delta;
}

qsizetype WrapIndex::rows(qsizetype line) const
{
    if (line < 0 || line >= m_lineCount) {
        return 1;
    }
    const auto [chunk, index]{locate(line)};
    return m_chunks.at(chunk).rows.at(index);
}

qsizetype WrapIndex::rowOf(qsizetype line) const
{
    qsizetype row{0};
    for (const Chunk &chunk : m_chunks) {
        if (line < chunk.rows.size()) {
            return row + std::accumulate(
                             chunk.rows.cbegin(),
                             chunk.rows.cbegin() + std::max(line, qsizetype{0}),
                             qsizetype{0}
                         );
        }
        line -= chunk.rows.size();
        row += chunk.total;
    }
    return row;
}

qsizetype WrapIndex::lineAt(qsizetype row) const
{
    qsizetype line{0};
    for (const Chunk &chunk : m_chunks) {
        if (row < chunk.total) {
            for (const qint32 rows : chunk.rows) {
                if (row < rows) {
                    return line;
                }
                row -= rows;
                ++line;
            }
        }
        row -= chunk.total;
        line += chunk.rows.size();
    }
    return std::max(qsizetype{0}, m_lineCount - 1);
}

} // namespace kode
