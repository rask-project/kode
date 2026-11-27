#pragma once

#include "TextBuffer.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <optional>

namespace KSyntaxHighlighting
{
class Definition;
} // namespace KSyntaxHighlighting

namespace kode
{

class SyntaxHighlighter;

// The text one or more EditorViews show and edit: the buffer, undo history,
// highlighting and indentation style. Views keep their own caret, scroll and
// layout; every change goes through replace() (or undo()/redo()/setText())
// and reaches them all with replaced() (or reset()).
class TextDocument : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a Document or an EditorView")
    Q_PROPERTY(qint64 length READ length NOTIFY textChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
    // The highlighting definition's name; empty for plain text.
    Q_PROPERTY(QString syntax READ syntax WRITE setSyntax NOTIFY syntaxChanged)
    Q_PROPERTY(bool indentWithTabs READ indentWithTabs WRITE setIndentWithTabs NOTIFY indentationChanged)
    Q_PROPERTY(qint32 indentWidth READ indentWidth WRITE setIndentWidth NOTIFY indentationChanged)
    // For new text and text without indentation to detect.
    Q_PROPERTY(bool defaultIndentWithTabs READ defaultIndentWithTabs WRITE setDefaultIndentWithTabs NOTIFY
                   defaultIndentationChanged)
    Q_PROPERTY(qint32 defaultIndentWidth READ defaultIndentWidth WRITE setDefaultIndentWidth NOTIFY
                   defaultIndentationChanged)
public:
    // A caret and the selection's other end.
    struct Caret {
        qsizetype cursor{0};
        qsizetype anchor{0};
    };
    // Characters [position, position + count).
    struct Range {
        qsizetype position{0};
        qsizetype count{0};
    };
    // Where a view left the document, for the next view that shows it.
    struct Place {
        qreal contentX{0.0};
        qreal contentY{0.0};
        Caret caret;
    };

    explicit TextDocument(QObject *parent = nullptr);
    ~TextDocument() override;
    Q_DISABLE_COPY_MOVE(TextDocument)

    [[nodiscard]] const TextBuffer &buffer() const noexcept { return m_buffer; }
    [[nodiscard]] SyntaxHighlighter &highlighter() const noexcept { return *m_highlighter; }
    [[nodiscard]] qint64 length() const { return m_buffer.length(); }
    [[nodiscard]] bool modified() const noexcept { return m_undoIndex != m_cleanIndex; }
    [[nodiscard]] bool canUndo() const noexcept { return m_undoIndex > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return m_undoIndex < m_undo.size(); }
    [[nodiscard]] QString syntax() const;
    void setSyntax(const QString &syntax);
    [[nodiscard]] bool indentWithTabs() const noexcept { return (m_flags & IndentWithTabs) != 0U; }
    void setIndentWithTabs(bool tabs);
    [[nodiscard]] qint32 indentWidth() const noexcept { return static_cast<qint32>(m_indentWidth); }
    void setIndentWidth(qint32 width);
    [[nodiscard]] bool defaultIndentWithTabs() const noexcept { return (m_flags & DefaultIndentWithTabs) != 0U; }
    void setDefaultIndentWithTabs(bool tabs);
    [[nodiscard]] qint32 defaultIndentWidth() const noexcept { return static_cast<qint32>(m_defaultIndentWidth); }
    void setDefaultIndentWidth(qint32 width);
    [[nodiscard]] Place place() const noexcept { return m_place; }
    void setPlace(const Place &place) noexcept { m_place = place; }

    Q_INVOKABLE [[nodiscard]] QString text() const { return m_buffer.toString(); }
    // Replaces everything: no undo history, not modified, the default
    // indentation. `keepPlace`: views keep their caret and scroll (a reload).
    void setText(const QString &text, bool keepPlace = false);
    // The same with the text already in a buffer (built off the GUI thread).
    void setBuffer(TextBuffer buffer, bool keepPlace = false);
    // Replaces `count` characters at `position` with `text`, recorded for
    // undo with the caret before it; typing merges into the previous edit
    // while `mergeable`.
    void replace(qsizetype position, qsizetype count, const QString &text, bool mergeable, Caret before);
    // Replaces each of `ranges` (by position, apart) with `text`: several
    // carets typing. One undo step; typing extends it as with one caret.
    void replaceEach(const QList<Range> &ranges, const QString &text, bool mergeable, Caret before);
    // Each returns the caret to show, or nothing when there is nothing to do.
    std::optional<Caret> undo();
    std::optional<Caret> redo();
    // The next typing starts an edit of its own (the caret moved).
    void closeMerge() noexcept { m_flags &= ~MergeOpen; }
    // The text now matches the file: no longer modified.
    Q_INVOKABLE void markSaved();
    // Picks the syntax for a file named `fileName` (or by its shebang).
    Q_INVOKABLE void detectSyntax(const QString &fileName);
    // The highlighting theme (see Syntaxes.themes).
    void setTheme(const QString &name);
    // Takes the indentation style from the text (see indentation::detect()).
    Q_INVOKABLE void detectIndentation();

signals:
    void textChanged();
    // At `position`, `removed` became `inserted`.
    void replaced(qsizetype position, const QString &removed, const QString &inserted);
    void reset(bool keepPlace);
    void modifiedChanged();
    void undoStateChanged();
    void syntaxChanged();
    // The definition or theme changed: lines look different.
    void highlightingChanged();
    void indentationChanged();
    void defaultIndentationChanged();

private:
    // At `position`, `removed` was replaced by `inserted`.
    struct Edit {
        qsizetype position{0};
        QString removed;
        QString inserted;
    };
    // One undoable change: its edits by position, each at its position before
    // the step (applied from the last), and the caret before and after.
    struct Step {
        QList<Edit> edits;
        Caret before;
        qsizetype cursorAfter{0};
    };

    static constexpr quint64 MergeOpen{1U << 0U};
    static constexpr quint64 IndentWithTabs{1U << 1U};
    static constexpr quint64 DefaultIndentWithTabs{1U << 2U};

    // Records `step`, merging typing into the last one.
    void record(Step step, bool mergeable);
    static bool merge(Step &last, const Step &next);
    void apply(qsizetype position, const QString &from, const QString &to);
    void applyDefinition(const KSyntaxHighlighting::Definition &definition);

    TextBuffer m_buffer;
    std::unique_ptr<SyntaxHighlighter> m_highlighter;
    QList<Step> m_undo;
    QString m_theme;
    Place m_place;
    qsizetype m_undoIndex{0};
    // The undo index of the saved text; -1 when no longer reachable.
    qsizetype m_cleanIndex{0};
    qsizetype m_indentWidth{4};
    qsizetype m_defaultIndentWidth{4};
    quint64 m_flags{0U};
};

} // namespace kode
