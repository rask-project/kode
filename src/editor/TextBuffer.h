#pragma once

#include <QList>
#include <QString>
#include <QStringView>

namespace tekst
{

// The editor's text as a piece table: the text loaded from the file (the
// original buffer) and everything typed since (the added buffer, append
// only), stitched by a list of pieces. Edits never copy the text: they split
// pieces and append. Each buffer indexes its newlines, so finding a line is
// a walk over the pieces plus a binary search. Lines end with '\n' only
// (line endings are converted when loading and saving).
class TextBuffer
{
public:
    TextBuffer() = default;
    explicit TextBuffer(QString text);

    [[nodiscard]] qsizetype length() const noexcept { return m_length; }
    [[nodiscard]] qsizetype lineCount() const noexcept { return m_newlines + 1; }
    [[nodiscard]] qsizetype pieceCount() const noexcept { return m_pieces.size(); }

    // Position of the first character of `line` (0-based).
    [[nodiscard]] qsizetype lineStart(qsizetype line) const;
    // Length of `line` without its newline.
    [[nodiscard]] qsizetype lineLength(qsizetype line) const;
    [[nodiscard]] QString line(qsizetype line) const;
    // The line `position` is on.
    [[nodiscard]] qsizetype lineAt(qsizetype position) const;
    [[nodiscard]] QString text(qsizetype position, qsizetype count) const;
    [[nodiscard]] QString toString() const { return text(0, m_length); }

    void insert(qsizetype position, QStringView text);
    void remove(qsizetype position, qsizetype count);

private:
    // Which buffer a piece points into (32 bits, so a Piece has no padding).
    static constexpr quint32 Original{0U};
    static constexpr quint32 Added{1U};

    struct Piece {
        qsizetype start{0};
        qsizetype length{0};
        quint32 newlines{0};
        quint32 source{Original};
    };

    struct Buffer {
        QString text;
        // Positions of the '\n' characters in `text`, ascending.
        QList<qsizetype> newlines;
    };

    // The piece holding `position` and the offset in it; past the end:
    // {pieceCount(), 0}.
    struct Location {
        qsizetype piece{0};
        qsizetype offset{0};
    };

    [[nodiscard]] static Buffer indexed(QString text);
    [[nodiscard]] const Buffer &buffer(quint32 source) const noexcept;
    [[nodiscard]] qsizetype newlinesIn(quint32 source, qsizetype start, qsizetype count) const;
    [[nodiscard]] Location locate(qsizetype position) const;
    // Splits the piece at `position` so that a piece starts there; returns
    // its index.
    qsizetype splitAt(qsizetype position);

    Buffer m_original;
    Buffer m_added;
    QList<Piece> m_pieces;
    qsizetype m_length{0};
    qsizetype m_newlines{0};
};

} // namespace tekst
