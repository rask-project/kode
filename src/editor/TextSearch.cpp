#include "TextSearch.h"

#include "TextBuffer.h"

#include <algorithm>

namespace kode
{

namespace
{

// Lines searched between deadline checks.
constexpr qsizetype linesPerCheck{64};

} // namespace

TextSearch::TextSearch(const TextBuffer *buffer) : m_buffer{buffer} {}

void TextSearch::setQuery(const QString &pattern, bool caseSensitive, bool regularExpression)
{
    m_pattern = pattern;
    m_flags = (caseSensitive ? CaseSensitive : 0U) | (regularExpression ? RegularExpression : 0U);
    QRegularExpression::PatternOptions options{QRegularExpression::UseUnicodePropertiesOption};
    if (!caseSensitive) {
        options |= QRegularExpression::CaseInsensitiveOption;
    }
    m_expression = QRegularExpression{regularExpression ? pattern : QRegularExpression::escape(pattern), options};
    m_expression.optimize();
    restart();
}

bool TextSearch::isActive() const
{
    return !m_pattern.isEmpty() && m_expression.isValid();
}

QString TextSearch::error() const
{
    return m_pattern.isEmpty() || m_expression.isValid() ? QString{} : m_expression.errorString();
}

void TextSearch::restart()
{
    m_matches.clear();
    m_nextLine = 0;
    m_flags &= ~Capped;
}

bool TextSearch::isDone() const
{
    return !isActive() || m_nextLine >= m_buffer->lineCount();
}

bool TextSearch::advance(const QDeadlineTimer &deadline)
{
    qsizetype sinceCheck{0};
    while (!isDone()) {
        if (++sinceCheck == linesPerCheck) {
            sinceCheck = 0;
            if (deadline.hasExpired()) {
                return false;
            }
        }
        const QString text{m_buffer->line(m_nextLine)};
        const qsizetype lineStart{m_buffer->lineStart(m_nextLine)};
        ++m_nextLine;
        QRegularExpressionMatchIterator found{m_expression.globalMatch(text)};
        while (found.hasNext()) {
            const QRegularExpressionMatch match{found.next()};
            if (match.capturedLength() == 0) {
                continue;
            }
            if (m_matches.size() == maxMatches) {
                m_flags |= Capped;
                m_nextLine = m_buffer->lineCount();
                return true;
            }
            m_matches.append({.start = lineStart + match.capturedStart(), .length = match.capturedLength()});
        }
    }
    return true;
}

qsizetype TextSearch::indexFrom(qsizetype position) const
{
    const auto found{std::ranges::lower_bound(m_matches, position, {}, &Match::start)};
    return found - m_matches.cbegin();
}

qsizetype TextSearch::indexOf(qsizetype start, qsizetype length) const
{
    const qsizetype index{indexFrom(start)};
    if (index < m_matches.size() && m_matches.at(index).start == start && m_matches.at(index).length == length) {
        return index;
    }
    return -1;
}

QString TextSearch::replacementFor(const Match &match, const QString &replacement) const
{
    if (!regularExpression()) {
        return replacement;
    }
    const qsizetype line{m_buffer->lineAt(match.start)};
    const qsizetype lineStart{m_buffer->lineStart(line)};
    const QRegularExpressionMatch captured{m_expression.match(
        m_buffer->line(line),
        match.start - lineStart,
        QRegularExpression::NormalMatch,
        QRegularExpression::AnchorAtOffsetMatchOption
    )};
    QString result;
    for (qsizetype i{0}; i < replacement.size(); ++i) {
        const QChar character{replacement.at(i)};
        const bool reference{
            character == QLatin1Char('$') && i + 1 < replacement.size() && replacement.at(i + 1).isDigit()
        };
        if (!reference) {
            result.append(character);
            continue;
        }
        result.append(captured.captured(replacement.at(i + 1).digitValue()));
        ++i;
    }
    return result;
}

} // namespace kode
