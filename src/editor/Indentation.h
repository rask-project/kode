#pragma once

#include <QString>
#include <QStringView>

namespace tekst
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

// The style of the first `maxLines` lines of `buffer`: tabs if more lines
// start with a tab than with spaces, the width the most common step
// between indented lines (2, 3, 4 or 8); `fallback` when nothing is
// indented.
[[nodiscard]] Style detect(const TextBuffer &buffer, qsizetype maxLines, Style fallback);

} // namespace indentation

} // namespace tekst
