#pragma once

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/State>

#include <QDeadlineTimer>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QTextLayout>

namespace tekst
{

class TextBuffer;

// Syntax highlighting of a TextBuffer with KSyntaxHighlighting, line by
// line. It keeps the state each line ends in, computed in order from the
// top a slice at a time (advance()); after an edit, lines are highlighted
// again only until one ends in the state it ended in before.
class SyntaxHighlighter final : public KSyntaxHighlighting::AbstractHighlighter
{
public:
    // Borrows `buffer`, which must outlive the highlighter.
    explicit SyntaxHighlighter(const TextBuffer *buffer);

    // The definition for `fileName` (by its name, else by the shebang in
    // `firstLine`); invalid when none fits.
    [[nodiscard]] static KSyntaxHighlighting::Definition detect(const QString &fileName, const QString &firstLine);
    [[nodiscard]] static KSyntaxHighlighting::Definition definitionForName(const QString &name);
    // The theme named `name` (Tekst's own included), else the default one.
    [[nodiscard]] static KSyntaxHighlighting::Theme themeNamed(const QString &name);
    // The names of the themes with a dark (else light) background, sorted.
    [[nodiscard]] static QStringList themeNames(bool dark);
    // Every definition a user can pick, sorted by section then name.
    [[nodiscard]] static QList<KSyntaxHighlighting::Definition> definitions();

    void setDefinition(const KSyntaxHighlighting::Definition &definition) override;
    void setTheme(const KSyntaxHighlighting::Theme &theme) override;
    [[nodiscard]] bool isActive() const;

    // The whole text was replaced.
    void reset();
    // Lines line..line+removedLines were replaced by line..line+insertedLines.
    void linesReplaced(qsizetype line, qsizetype removedLines, qsizetype insertedLines);
    // Highlights lines until `line` can be highlighted or `deadline` passes;
    // returns whether it can.
    bool advance(qsizetype line, const QDeadlineTimer &deadline);
    [[nodiscard]] bool isReady(qsizetype line) const { return !isActive() || line <= m_valid; }
    // The state `line` starts in (it must be ready).
    [[nodiscard]] KSyntaxHighlighting::State startState(qsizetype line) const;
    // The formats of `line`, whose text is `text` (it must be ready).
    [[nodiscard]] QList<QTextLayout::FormatRange> formats(qsizetype line, QStringView text);

protected:
    void applyFormat(int offset, int length, const KSyntaxHighlighting::Format &format) override;

private:
    [[nodiscard]] QTextCharFormat charFormat(const KSyntaxHighlighting::Format &format);

    const TextBuffer *m_buffer{nullptr};
    // The state each line ends in: lines [0, m_valid) are right; while
    // resuming after an edit, lines [m_valid, m_resumeEnd) hold the states
    // from before it, and a match at or past m_checkFrom means they are right.
    QList<KSyntaxHighlighting::State> m_states;
    QHash<qint32, QTextCharFormat> m_charFormats;
    QList<QTextLayout::FormatRange> m_collected;
    qsizetype m_valid{0};
    qsizetype m_resumeEnd{0};
    qsizetype m_checkFrom{0};
    // The line whose formats formats() collects; -1 while advancing.
    qsizetype m_collecting{-1};
};

} // namespace tekst
