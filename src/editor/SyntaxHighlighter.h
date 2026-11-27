#pragma once

#include "Indentation.h"

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/FoldingRegion>
#include <KSyntaxHighlighting/State>

#include <QDeadlineTimer>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QTextLayout>

#include <optional>

namespace kode
{

class TextBuffer;

// Syntax highlighting of a TextBuffer with KSyntaxHighlighting, line by
// line. It keeps the state each line ends in, computed in order from the
// top a slice at a time (advance()); after an edit, lines are highlighted
// again only until one ends in the state it ended in before.
class SyntaxHighlighter final : public KSyntaxHighlighting::AbstractHighlighter
{
public:
    // The states lines start in from `first` on, guessed where highlighting
    // has not reached yet (a view far ahead of it): highlighted from a few
    // lines above with no state.
    struct Guess {
        QList<KSyntaxHighlighting::State> states;
        qsizetype first{0};

        [[nodiscard]] bool covers(qsizetype from, qsizetype end) const noexcept
        {
            return from >= first && end <= first + states.size();
        }
    };

    // Borrows `buffer`, which must outlive the highlighter.
    explicit SyntaxHighlighter(const TextBuffer *buffer);

    // The definition for `fileName` (by its name, else by the shebang in
    // `firstLine`); invalid when none fits.
    [[nodiscard]] static KSyntaxHighlighting::Definition detect(const QString &fileName, const QString &firstLine);
    [[nodiscard]] static KSyntaxHighlighting::Definition definitionForName(const QString &name);
    // The theme named `name` (Kode's own included), else the default one.
    [[nodiscard]] static KSyntaxHighlighting::Theme themeNamed(const QString &name);
    // The themes that come as a light and a dark pair ("X Light", "X Dark"), by their
    // family name ("X"), sorted.
    [[nodiscard]] static QStringList themeFamilies();
    // `name` if it is a family, the family of one of its themes, else Kode.
    [[nodiscard]] static QString themeFamily(const QString &name);
    // The light or dark theme of `name`'s family.
    [[nodiscard]] static QString themeName(const QString &name, bool dark);
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
    // The same from the state it starts in, as guessed.
    [[nodiscard]] QList<QTextLayout::FormatRange>
    formats(qsizetype line, QStringView text, const KSyntaxHighlighting::State &start);
    // Extends `guess` to lines [first, end), until `deadline` passes. One too
    // far from them starts over guessLead lines above `first` (from the
    // last line ready, when closer: its state is known).
    void guess(Guess &guess, qsizetype first, qsizetype end, const QDeadlineTimer &deadline);
    // Lines from `line` on were edited: their guessed states are dropped.
    static void guessEdited(Guess &guess, qsizetype line);
    static constexpr qsizetype guessLead{200};
    // The columns of `line` (whose text is `text`) in comments and strings;
    // none when it is not ready.
    [[nodiscard]] QList<indentation::Span> nonCode(qsizetype line, QStringView text);
    // Whether `line` starts inside a comment or string (it must be ready):
    // known from the state it starts in, so asked once per state.
    [[nodiscard]] bool startsInNonCode(qsizetype line);
    // How a semantic token's `style` (semantic::Category and mixins) looks in
    // the theme: its "semantic-styles" (Kode's themes, from Qt Creator's
    // schemes), else the nearest text styles.
    [[nodiscard]] QTextCharFormat semanticFormat(quint32 style);
    // How many folding regions the line formats() last took opens and leaves open.
    [[nodiscard]] qsizetype openedRegions() const noexcept { return m_opened; }
    // The line where the first region `line` leaves open ends; none when it
    // leaves none open, `line` is not ready or `deadline` passes first.
    [[nodiscard]] std::optional<qsizetype> regionEnd(qsizetype line, const QDeadlineTimer &deadline);
    // The closest line above `line` with a region open past it.
    [[nodiscard]] std::optional<qsizetype> regionAbove(qsizetype line, const QDeadlineTimer &deadline);

protected:
    void applyFormat(int offset, int length, const KSyntaxHighlighting::Format &format) override;
    void applyFolding(int offset, int length, KSyntaxHighlighting::FoldingRegion region) override;

private:
    [[nodiscard]] QTextCharFormat charFormat(const KSyntaxHighlighting::Format &format);
    // Qt Creator styles a Q_PROPERTY's words (READ, NOTIFY…) as keywords.
    void appendPropertyKeywords(QStringView text);
    void readSemanticStyles();

    const TextBuffer *m_buffer{nullptr};
    // The state each line ends in: lines [0, m_valid) are right; while
    // resuming after an edit, lines [m_valid, m_resumeEnd) hold the states
    // from before it, and a match at or past m_checkFrom means they are right.
    QList<KSyntaxHighlighting::State> m_states;
    QHash<qint32, QTextCharFormat> m_charFormats;
    // The theme's "semantic-styles", by Qt Creator's names; empty: it has none.
    QHash<QString, QTextCharFormat> m_semanticStyles;
    QHash<quint32, QTextCharFormat> m_semanticFormats;
    QHash<KSyntaxHighlighting::State, bool> m_startsInNonCode;
    QList<QTextLayout::FormatRange> m_collected;
    // The folding regions of the line highlighted last, in order: id + 1
    // where one begins, -(id + 1) where one ends.
    QList<qint32> m_regions;
    // The comments and strings of the line highlighted last.
    QList<indentation::Span> m_nonCode;
    qsizetype m_valid{0};
    qsizetype m_resumeEnd{0};
    qsizetype m_checkFrom{0};
    // The line whose formats formats() collects; -1 while advancing.
    qsizetype m_collecting{-1};
    qsizetype m_opened{0};
};

} // namespace kode
