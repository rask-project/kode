#pragma once

#include <QDeadlineTimer>
#include <QList>
#include <QRegularExpression>
#include <QString>

namespace tekst
{

class TextBuffer;

// Find in a TextBuffer, line by line (matches do not span lines), a slice at
// a time (advance()): the matches found so far stay sorted by position.
// Stops storing at maxMatches.
class TextSearch
{
public:
    struct Match {
        qsizetype start{0};
        qsizetype length{0};
    };

    static constexpr qsizetype maxMatches{100000};

    // Borrows `buffer`, which must outlive the search.
    explicit TextSearch(const TextBuffer *buffer);

    // An empty pattern clears the search; an invalid expression sets error().
    void setQuery(const QString &pattern, bool caseSensitive, bool regularExpression);
    [[nodiscard]] QString pattern() const { return m_pattern; }
    [[nodiscard]] bool caseSensitive() const noexcept { return (m_flags & CaseSensitive) != 0U; }
    [[nodiscard]] bool regularExpression() const noexcept { return (m_flags & RegularExpression) != 0U; }
    // Whether there is something to search for.
    [[nodiscard]] bool isActive() const;
    [[nodiscard]] QString error() const;
    // The text changed: search again from the top.
    void restart();
    // Searches until done or `deadline` passes; returns whether done.
    bool advance(const QDeadlineTimer &deadline);
    [[nodiscard]] bool isDone() const;
    [[nodiscard]] bool isCapped() const noexcept { return (m_flags & Capped) != 0U; }
    [[nodiscard]] const QList<Match> &matches() const noexcept { return m_matches; }
    // The first match starting at or after `position`; matches().size() if none.
    [[nodiscard]] qsizetype indexFrom(qsizetype position) const;
    // The match exactly at [start, start + length); -1 if none.
    [[nodiscard]] qsizetype indexOf(qsizetype start, qsizetype length) const;
    // What `replacement` turns `match` into: with a regular expression, $0…$9
    // stand for its captures.
    [[nodiscard]] QString replacementFor(const Match &match, const QString &replacement) const;

private:
    static constexpr quint64 CaseSensitive{1U << 0U};
    static constexpr quint64 RegularExpression{1U << 1U};
    static constexpr quint64 Capped{1U << 2U};

    const TextBuffer *m_buffer{nullptr};
    QRegularExpression m_expression;
    QString m_pattern;
    QList<Match> m_matches;
    // The next line to search; the line count once done.
    qsizetype m_nextLine{0};
    quint64 m_flags{0U};
};

} // namespace tekst
