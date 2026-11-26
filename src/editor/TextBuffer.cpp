#include "TextBuffer.h"

#include <algorithm>
#include <utility>

namespace kode
{

namespace
{

// Appends the positions of the newlines of `text`, offset by `base`.
void indexNewlines(QList<qsizetype> &newlines, QStringView text, qsizetype base)
{
    for (qsizetype i{text.indexOf(QLatin1Char('\n'))}; i >= 0; i = text.indexOf(QLatin1Char('\n'), i + 1)) {
        newlines.append(base + i);
    }
}

} // namespace

TextBuffer::TextBuffer(QString text)
    : m_original(indexed(std::move(text)))
    , m_length(m_original.text.size())
    , m_newlines(m_original.newlines.size())
{
    if (m_length > 0) {
        m_pieces.append(
            {.start = 0, .length = m_length, .newlines = static_cast<quint32>(m_newlines), .source = Original}
        );
    }
}

TextBuffer::Buffer TextBuffer::indexed(QString text)
{
    Buffer buffer{.text = std::move(text), .newlines = {}};
    indexNewlines(buffer.newlines, buffer.text, 0);
    return buffer;
}

const TextBuffer::Buffer &TextBuffer::buffer(quint32 source) const noexcept
{
    return source == Original ? m_original : m_added;
}

qsizetype TextBuffer::newlinesIn(quint32 source, qsizetype start, qsizetype count) const
{
    const QList<qsizetype> &newlines{buffer(source).newlines};
    const auto first{std::ranges::lower_bound(newlines, start)};
    const auto last{std::ranges::lower_bound(first, newlines.end(), start + count)};
    return last - first;
}

TextBuffer::Location TextBuffer::locate(qsizetype position) const
{
    qsizetype pieceStart{0};
    for (qsizetype i{0}; i < m_pieces.size(); ++i) {
        const qsizetype length{m_pieces.at(i).length};
        if (position < pieceStart + length) {
            return {.piece = i, .offset = position - pieceStart};
        }
        pieceStart += length;
    }
    return {.piece = m_pieces.size(), .offset = 0};
}

qsizetype TextBuffer::splitAt(qsizetype position)
{
    const Location location{locate(position)};
    if (location.offset == 0) {
        return location.piece;
    }
    const Piece piece{m_pieces.at(location.piece)};
    const auto headNewlines{static_cast<quint32>(newlinesIn(piece.source, piece.start, location.offset))};
    m_pieces[location.piece] =
        {.start = piece.start, .length = location.offset, .newlines = headNewlines, .source = piece.source};
    m_pieces.insert(
        location.piece + 1,
        {.start = piece.start + location.offset,
         .length = piece.length - location.offset,
         .newlines = piece.newlines - headNewlines,
         .source = piece.source}
    );
    return location.piece + 1;
}

qsizetype TextBuffer::lineStart(qsizetype line) const
{
    if (line <= 0) {
        return 0;
    }
    if (line > m_newlines) {
        return m_length;
    }
    // Line `line` starts after the `line`-th newline.
    qsizetype remaining{line};
    qsizetype pieceStart{0};
    for (const Piece &piece : m_pieces) {
        if (remaining <= piece.newlines) {
            const QList<qsizetype> &newlines{buffer(piece.source).newlines};
            const qsizetype first{std::ranges::lower_bound(newlines, piece.start) - newlines.begin()};
            return pieceStart + (newlines.at(first + remaining - 1) - piece.start) + 1;
        }
        remaining -= piece.newlines;
        pieceStart += piece.length;
    }
    return m_length;
}

qsizetype TextBuffer::lineLength(qsizetype line) const
{
    const qsizetype start{lineStart(line)};
    const qsizetype end{line < m_newlines ? lineStart(line + 1) - 1 : m_length};
    return end - start;
}

QList<qsizetype> TextBuffer::lineLengths() const
{
    QList<qsizetype> lengths;
    lengths.reserve(lineCount());
    qsizetype current{0};
    for (const Piece &piece : m_pieces) {
        const QList<qsizetype> &newlines{buffer(piece.source).newlines};
        // The piece's newlines, as offsets into its buffer.
        const auto first{std::ranges::lower_bound(newlines, piece.start)};
        qsizetype from{piece.start};
        for (auto newline{first}; newline != newlines.cend() && *newline < piece.start + piece.length; ++newline) {
            lengths.append(current + (*newline - from));
            current = 0;
            from = *newline + 1;
        }
        current += piece.start + piece.length - from;
    }
    lengths.append(current);
    return lengths;
}

QString TextBuffer::line(qsizetype line) const
{
    return text(lineStart(line), lineLength(line));
}

qsizetype TextBuffer::lineAt(qsizetype position) const
{
    qsizetype line{0};
    qsizetype pieceStart{0};
    for (const Piece &piece : m_pieces) {
        if (position < pieceStart + piece.length) {
            return line + newlinesIn(piece.source, piece.start, position - pieceStart);
        }
        line += piece.newlines;
        pieceStart += piece.length;
    }
    return line;
}

QString TextBuffer::text(qsizetype position, qsizetype count) const
{
    QString result;
    if (count <= 0) {
        return result;
    }
    result.reserve(count);
    const qsizetype end{std::min(position + count, m_length)};
    qsizetype pieceStart{0};
    for (const Piece &piece : m_pieces) {
        const qsizetype pieceEnd{pieceStart + piece.length};
        if (pieceEnd > position) {
            const qsizetype from{std::max(position, pieceStart)};
            const qsizetype to{std::min(end, pieceEnd)};
            result.append(QStringView{buffer(piece.source).text}.sliced(piece.start + (from - pieceStart), to - from));
        }
        if (pieceEnd >= end) {
            break;
        }
        pieceStart = pieceEnd;
    }
    return result;
}

void TextBuffer::insert(qsizetype position, QStringView text)
{
    if (text.isEmpty()) {
        return;
    }
    position = std::clamp(position, qsizetype{0}, m_length);
    const qsizetype addedStart{m_added.text.size()};
    m_added.text.append(text);
    const qsizetype indexed{m_added.newlines.size()};
    indexNewlines(m_added.newlines, text, addedStart);
    const auto newlines{static_cast<quint32>(m_added.newlines.size() - indexed)};

    const qsizetype index{splitAt(position)};
    // Typing appends to the added buffer right after the previous insertion:
    // grow that piece instead of adding one per keystroke.
    if (index > 0) {
        Piece &previous{m_pieces[index - 1]};
        if (previous.source == Added && previous.start + previous.length == addedStart) {
            previous.length += text.size();
            previous.newlines += newlines;
            m_length += text.size();
            m_newlines += newlines;
            return;
        }
    }
    m_pieces.insert(index, {.start = addedStart, .length = text.size(), .newlines = newlines, .source = Added});
    m_length += text.size();
    m_newlines += newlines;
}

void TextBuffer::remove(qsizetype position, qsizetype count)
{
    position = std::clamp(position, qsizetype{0}, m_length);
    count = std::min(count, m_length - position);
    if (count <= 0) {
        return;
    }
    const qsizetype first{splitAt(position)};
    const qsizetype last{splitAt(position + count)};
    const auto removed{m_pieces.sliced(first, last - first)};
    const auto newlines{std::ranges::fold_left(removed, qsizetype{0}, [](qsizetype sum, const Piece &piece) {
        return sum + piece.newlines;
    })};
    m_pieces.remove(first, last - first);
    m_length -= count;
    m_newlines -= newlines;
}

} // namespace kode
