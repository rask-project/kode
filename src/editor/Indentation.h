#pragma once

#include <QList>
#include <QString>
#include <QStringView>

namespace kode
{

class TextBuffer;

// How lines are indented, and the text changes that indent them.
namespace indentation
{

enum class Kind : quint8 {
    Spaces,
    Tabs
};

struct Style {
    // Columns per level (and per tab stop), 1–16.
    quint8 width{4};
    Kind kind{Kind::Spaces};
    friend bool operator==(const Style &, const Style &) = default;
};

// One level: a tab, or `width` spaces.
[[nodiscard]] QString unit(Style style);

// What Tab inserts at `column`: a tab, or the spaces to the next stop.
[[nodiscard]] QString tabAt(Style style, qsizetype column);

// The spaces and tabs `line` starts with.
[[nodiscard]] QStringView leadingWhitespace(QStringView line);

// `lines` ('\n'-separated) one level in (empty lines left alone) or out.
[[nodiscard]] QString indented(QStringView lines, Style style);
[[nodiscard]] QString outdented(QStringView lines, Style style);

// Columns [start, end) of a line.
struct Span {
    qsizetype start{0};
    qsizetype end{0};
    friend bool operator==(const Span &, const Span &) = default;
};

// What the brackets ( (, [, { ) of a line do, those in `ignored` spans
// (comments, strings) aside; for indenting without knowing the language.
struct Brackets {
    // Closing ones the line starts with ("}", "})").
    qint32 leading{0};
    // Opened on the line and left open.
    qint32 open{0};
    // Closing ones after something else, opened above (a call's last line).
    qint32 closed{0};
    // 1 when the last thing on the line is a ':' (Python's blocks).
    qint32 colon{0};
    friend bool operator==(const Brackets &, const Brackets &) = default;
};

[[nodiscard]] Brackets brackets(QStringView line, const QList<Span> &ignored);

// The columns `whitespace` (spaces and tabs) takes, tabs to `width`.
[[nodiscard]] qsizetype columns(QStringView whitespace, qsizetype width);

// `count` columns of indentation in `style`: tabs, then spaces for the rest.
[[nodiscard]] QString whitespace(qsizetype count, Style style);

// The columns a line starting with `leading` closing brackets is indented
// by after one indented `previous` columns with `before`'s brackets: a
// level more after brackets left open (or a ':' with `colonBlocks`), a
// level less after closing ones opened above, and for its own.
[[nodiscard]] qsizetype
indentAfter(qsizetype previous, const Brackets &before, qint32 leading, Style style, bool colonBlocks);

// The style of the first `maxLines` lines of `buffer`: tabs if more lines
// start with a tab than with spaces, the width the most common step
// between indented lines (2, 3, 4 or 8); `fallback` when nothing is
// indented.
[[nodiscard]] Style detect(const TextBuffer &buffer, qsizetype maxLines, Style fallback);

} // namespace indentation

} // namespace kode
