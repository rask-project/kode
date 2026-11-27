#pragma once

#include <QList>
#include <QString>
#include <QStringView>

#include "Indentation.h"

#include <functional>
#include <optional>

namespace kode
{

class TextBuffer;

// Text searches for the editor's marks: the bracket matching the one at the
// caret, the word at the caret and where else it is.
namespace matching
{

struct Range {
    qsizetype start{0};
    qsizetype end{0};
    friend bool operator==(const Range &, const Range &) = default;
};

struct BracketPair {
    qsizetype bracket{0};
    qsizetype match{0};
    friend bool operator==(const BracketPair &, const BracketPair &) = default;
};

// A line's comments and strings, whose brackets do not count.
using NonCode = std::function<QList<indentation::Span>(qsizetype line, QStringView text)>;

// The bracket after `position` (else before it) and the one it pairs with,
// searched for at most `maxLines` lines away.
[[nodiscard]] std::optional<BracketPair>
matchingBracket(const TextBuffer &buffer, qsizetype position, qsizetype maxLines, const NonCode &nonCode = {});

// The word around `column` in `text` (columns); empty when there is none.
[[nodiscard]] Range wordAt(QStringView text, qsizetype column);

// Where `word` is in `text` as a whole word.
[[nodiscard]] QList<Range> wholeWords(QStringView text, QStringView word);

[[nodiscard]] bool isWordCharacter(QChar character);

} // namespace matching

} // namespace kode
