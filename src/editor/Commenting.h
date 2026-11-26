#pragma once

#include <QString>
#include <QStringView>

namespace kode
{

// Commenting code out and back in, with a language's markers (from its
// KSyntaxHighlighting definition).
namespace commenting
{

struct Markers {
    // "//", "#"…; empty when the language has none.
    QString line;
    // "/*" and "*/", "<!--" and "-->"…; empty when it has none.
    QString open;
    QString close;
};

// `lines` ('\n'-separated) commented out, or back in when every one that
// is not blank already is: the line marker and a space at the least
// indentation (at column 0 when `atColumnZero`), else each line between
// the block markers. Blank lines are left alone; unchanged without markers.
[[nodiscard]] QString toggledLines(QStringView lines, const Markers &markers, bool atColumnZero);

// `text` between the block markers ("/* text */"), or out of them when it
// already is (spaces inside them aside). Unchanged without block markers.
[[nodiscard]] QString toggledBlock(QStringView text, const Markers &markers);

} // namespace commenting

} // namespace kode
