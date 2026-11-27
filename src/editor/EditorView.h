#pragma once

#include "Commenting.h"
#include "Folding.h"
#include "SyntaxHighlighter.h"
#include "TextBuffer.h"
#include "TextDocument.h"
#include "WrapIndex.h"

#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QPointer>
#include <QQuickItem>
#include <QRectF>
#include <QString>
#include <QTextLayout>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <KSyntaxHighlighting/State>

#include <memory>
#include <optional>
#include <vector>

class QSGGeometryNode;

namespace KSyntaxHighlighting
{
class Definition;
} // namespace KSyntaxHighlighting

namespace kode
{

class EditorAccessible;
class EditorNode;

namespace indentation
{
struct Style;
struct Span;
} // namespace indentation
class TextSearch;

// The editing surface: a TextDocument drawn by laying out and rendering only
// the lines in view (one QSGTextNode per line and per line number), with
// caret, selection, the clipboard and input methods. Several views may show
// one document (`document`), each with its own caret, scroll and layout;
// without one a view edits a document of its own. Monospaced layout. It sits in a Flickable, filling its viewport:
// `contentWidth`/`contentHeight` size the Flickable, `contentX`/`contentY`
// say which part is in view; `cursorRectangle` is what to keep visible.
class EditorView : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    // The text shown; null: the view's own.
    Q_PROPERTY(kode::TextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor caretColor READ caretColor WRITE setCaretColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor currentLineColor READ currentLineColor WRITE setCurrentLineColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor lineNumberColor READ lineNumberColor WRITE setLineNumberColor NOTIFY colorsChanged)
    // Behind the bracket at the caret and the one it pairs with.
    Q_PROPERTY(QColor bracketColor READ bracketColor WRITE setBracketColor NOTIFY colorsChanged)
    // Behind the word at the caret and its other occurrences in view.
    Q_PROPERTY(QColor wordColor READ wordColor WRITE setWordColor NOTIFY colorsChanged)
    // Behind the matches of the search.
    Q_PROPERTY(QColor matchColor READ matchColor WRITE setMatchColor NOTIFY colorsChanged)
    // The indent guides, and the one of the caret's block.
    Q_PROPERTY(QColor guideColor READ guideColor WRITE setGuideColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor scopeColor READ scopeColor WRITE setScopeColor NOTIFY colorsChanged)
    // The underlines of the document's diagnostics, by severity.
    Q_PROPERTY(QColor errorColor READ errorColor WRITE setErrorColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor warningColor READ warningColor WRITE setWarningColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor informationColor READ informationColor WRITE setInformationColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor hintColor READ hintColor WRITE setHintColor NOTIFY colorsChanged)
    // The diagnostics in view, for their help tags: {x, y, width, height,
    // message} in content coordinates, one per line each covers.
    Q_PROPERTY(QVariantList diagnosticAreas READ diagnosticAreas NOTIFY diagnosticAreasChanged)
    Q_PROPERTY(qreal contentX READ contentX WRITE setContentX NOTIFY contentXChanged)
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(qreal contentWidth READ contentWidth NOTIFY contentSizeChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentSizeChanged)
    // Width of the line-number gutter, which does not scroll horizontally.
    Q_PROPERTY(qreal gutterWidth READ gutterWidth NOTIFY contentSizeChanged)
    Q_PROPERTY(qint32 lineCount READ lineCount NOTIFY contentSizeChanged)
    Q_PROPERTY(qint64 length READ length NOTIFY textChanged)
    Q_PROPERTY(qint64 cursorPosition READ cursorPosition WRITE setCursorPosition NOTIFY cursorChanged)
    Q_PROPERTY(qint32 cursorLine READ cursorLine NOTIFY cursorChanged)
    Q_PROPERTY(qint32 cursorColumn READ cursorColumn NOTIFY cursorChanged)
    // The caret, in content coordinates (gutter included).
    Q_PROPERTY(QRectF cursorRectangle READ cursorRectangle NOTIFY cursorChanged)
    Q_PROPERTY(qint64 selectionStart READ selectionStart NOTIFY selectionChanged)
    Q_PROPERTY(qint64 selectionEnd READ selectionEnd NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    // The caret and the others added by selectNextOccurrence().
    Q_PROPERTY(qint32 caretCount READ caretCount NOTIFY selectionChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY canPasteChanged)
    // Changed since setText() or markSaved(); undoing back clears it.
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    // The highlighting definition's name; empty: plain text.
    Q_PROPERTY(QString syntax READ syntax WRITE setSyntax NOTIFY syntaxChanged)
    // Find: what to search for (empty: no search) and how; the matches are
    // marked and findNext()/findPrevious() select them.
    Q_PROPERTY(QString searchPattern READ searchPattern WRITE setSearchPattern NOTIFY searchChanged)
    Q_PROPERTY(bool searchCaseSensitive READ searchCaseSensitive WRITE setSearchCaseSensitive NOTIFY searchChanged)
    Q_PROPERTY(bool searchRegularExpression READ searchRegularExpression WRITE setSearchRegularExpression NOTIFY
                   searchChanged)
    Q_PROPERTY(qint32 searchMatchCount READ searchMatchCount NOTIFY searchChanged)
    // The selected match, from 0; -1: the selection is not a match.
    Q_PROPERTY(qint32 searchCurrent READ searchCurrent NOTIFY searchChanged)
    // The count is final: the search is done (and not capped).
    Q_PROPERTY(bool searchComplete READ searchComplete NOTIFY searchChanged)
    Q_PROPERTY(QString searchError READ searchError NOTIFY searchChanged)
    // Lines wrap at the view's width (no horizontal scrolling).
    Q_PROPERTY(bool wrap READ wrap WRITE setWrap NOTIFY layoutOptionsChanged)
    // Spaces and tabs drawn as dots and arrows.
    Q_PROPERTY(bool showInvisibles READ showInvisibles WRITE setShowInvisibles NOTIFY layoutOptionsChanged)
    // A line at each indentation level, the one of the block at the caret in the caret's color.
    Q_PROPERTY(bool showIndentGuides READ showIndentGuides WRITE setShowIndentGuides NOTIFY layoutOptionsChanged)
    // Indent with tabs (else spaces), `indentWidth` columns per level.
    Q_PROPERTY(bool indentWithTabs READ indentWithTabs WRITE setIndentWithTabs NOTIFY indentationChanged)
    Q_PROPERTY(qint32 indentWidth READ indentWidth WRITE setIndentWidth NOTIFY indentationChanged)
    // The indentation new text starts with, and detection falls back to.
    Q_PROPERTY(bool defaultIndentWithTabs READ defaultIndentWithTabs WRITE setDefaultIndentWithTabs NOTIFY
                   defaultIndentationChanged)
    Q_PROPERTY(qint32 defaultIndentWidth READ defaultIndentWidth WRITE setDefaultIndentWidth NOTIFY
                   defaultIndentationChanged)
    // The highlighting theme's name (see Syntaxes.themes); plain text keeps
    // textColor.
    Q_PROPERTY(QString syntaxTheme READ syntaxTheme WRITE setSyntaxTheme NOTIFY syntaxThemeChanged)
public:
    explicit EditorView(QQuickItem *parent = nullptr);
    ~EditorView() override;
    Q_DISABLE_COPY_MOVE(EditorView)

    [[nodiscard]] TextDocument *document() const { return m_document; }
    void setDocument(TextDocument *document);
    [[nodiscard]] QFont font() const { return m_font; }
    void setFont(const QFont &font);
    [[nodiscard]] QColor textColor() const { return QColor::fromRgba(m_colors.text); }
    void setTextColor(const QColor &color);
    [[nodiscard]] QColor selectionColor() const { return QColor::fromRgba(m_colors.selection); }
    void setSelectionColor(const QColor &color);
    [[nodiscard]] QColor caretColor() const { return QColor::fromRgba(m_colors.caret); }
    void setCaretColor(const QColor &color);
    [[nodiscard]] QColor currentLineColor() const { return QColor::fromRgba(m_colors.currentLine); }
    void setCurrentLineColor(const QColor &color);
    [[nodiscard]] QColor lineNumberColor() const { return QColor::fromRgba(m_colors.lineNumber); }
    void setLineNumberColor(const QColor &color);
    [[nodiscard]] QColor bracketColor() const { return QColor::fromRgba(m_colors.bracket); }
    void setBracketColor(const QColor &color);
    [[nodiscard]] QColor wordColor() const { return QColor::fromRgba(m_colors.word); }
    void setWordColor(const QColor &color);
    [[nodiscard]] QColor matchColor() const { return QColor::fromRgba(m_colors.match); }
    void setMatchColor(const QColor &color);
    [[nodiscard]] QColor guideColor() const { return QColor::fromRgba(m_colors.guide); }
    void setGuideColor(const QColor &color);
    [[nodiscard]] QColor scopeColor() const { return QColor::fromRgba(m_colors.scope); }
    void setScopeColor(const QColor &color);
    [[nodiscard]] QColor errorColor() const { return QColor::fromRgba(m_colors.error); }
    void setErrorColor(const QColor &color) { setColor(m_colors.error, color); }
    [[nodiscard]] QColor warningColor() const { return QColor::fromRgba(m_colors.warning); }
    void setWarningColor(const QColor &color) { setColor(m_colors.warning, color); }
    [[nodiscard]] QColor informationColor() const { return QColor::fromRgba(m_colors.information); }
    void setInformationColor(const QColor &color) { setColor(m_colors.information, color); }
    [[nodiscard]] QColor hintColor() const { return QColor::fromRgba(m_colors.hint); }
    void setHintColor(const QColor &color) { setColor(m_colors.hint, color); }
    [[nodiscard]] QVariantList diagnosticAreas() const { return m_diagnosticAreas; }
    [[nodiscard]] QString searchPattern() const;
    void setSearchPattern(const QString &pattern);
    [[nodiscard]] bool searchCaseSensitive() const;
    void setSearchCaseSensitive(bool caseSensitive);
    [[nodiscard]] bool searchRegularExpression() const;
    void setSearchRegularExpression(bool regularExpression);
    [[nodiscard]] qint32 searchMatchCount() const;
    [[nodiscard]] qint32 searchCurrent() const;
    [[nodiscard]] bool searchComplete() const;
    [[nodiscard]] QString searchError() const;
    [[nodiscard]] bool wrap() const noexcept { return (m_flags & Wrap) != 0U; }
    void setWrap(bool wrap);
    [[nodiscard]] bool showInvisibles() const noexcept { return (m_flags & ShowInvisibles) != 0U; }
    void setShowInvisibles(bool show);
    [[nodiscard]] bool showIndentGuides() const noexcept { return (m_flags & ShowIndentGuides) != 0U; }
    void setShowIndentGuides(bool show);
    [[nodiscard]] bool indentWithTabs() const noexcept { return m_document->indentWithTabs(); }
    void setIndentWithTabs(bool tabs) { m_document->setIndentWithTabs(tabs); }
    [[nodiscard]] qint32 indentWidth() const noexcept { return m_document->indentWidth(); }
    void setIndentWidth(qint32 width) { m_document->setIndentWidth(width); }
    [[nodiscard]] bool defaultIndentWithTabs() const noexcept { return m_document->defaultIndentWithTabs(); }
    void setDefaultIndentWithTabs(bool tabs) { m_document->setDefaultIndentWithTabs(tabs); }
    [[nodiscard]] qint32 defaultIndentWidth() const noexcept { return m_document->defaultIndentWidth(); }
    void setDefaultIndentWidth(qint32 width) { m_document->setDefaultIndentWidth(width); }
    [[nodiscard]] qreal contentX() const noexcept { return m_contentX; }
    void setContentX(qreal contentX);
    [[nodiscard]] qreal contentY() const noexcept { return m_contentY; }
    void setContentY(qreal contentY);
    [[nodiscard]] qreal contentWidth() const;
    [[nodiscard]] qreal contentHeight() const;
    [[nodiscard]] qreal gutterWidth() const;
    [[nodiscard]] qint32 lineCount() const { return static_cast<qint32>(buffer().lineCount()); }
    [[nodiscard]] qint64 length() const { return buffer().length(); }
    [[nodiscard]] qint64 cursorPosition() const noexcept { return m_cursor; }
    void setCursorPosition(qint64 position);
    [[nodiscard]] qint32 cursorLine() const;
    [[nodiscard]] qint32 cursorColumn() const;
    [[nodiscard]] QRectF cursorRectangle() const;
    [[nodiscard]] qint64 selectionStart() const noexcept { return std::min(m_anchor, m_cursor); }
    [[nodiscard]] qint64 selectionEnd() const noexcept { return std::max(m_anchor, m_cursor); }
    [[nodiscard]] bool hasSelection() const noexcept { return m_anchor != m_cursor; }
    [[nodiscard]] qint32 caretCount() const noexcept { return static_cast<qint32>(m_others.size() + 1); }
    [[nodiscard]] bool canUndo() const noexcept { return m_document->canUndo(); }
    [[nodiscard]] bool canRedo() const noexcept { return m_document->canRedo(); }
    [[nodiscard]] static bool canPaste();
    [[nodiscard]] bool modified() const noexcept { return m_document->modified(); }
    [[nodiscard]] QString syntax() const { return m_document->syntax(); }
    void setSyntax(const QString &syntax) { m_document->setSyntax(syntax); }
    [[nodiscard]] QString syntaxTheme() const { return m_syntaxTheme; }
    void setSyntaxTheme(const QString &name);

    // The whole text, lines ending with '\n'.
    Q_INVOKABLE [[nodiscard]] QString text() const { return buffer().toString(); }
    // Replaces everything: no undo history, not modified, caret at the start.
    Q_INVOKABLE void setText(const QString &text) { m_document->setText(text); }
    Q_INVOKABLE [[nodiscard]] QString selectedText() const;
    // Replaces the selection (or inserts at the caret), as typing does.
    Q_INVOKABLE void insert(const QString &text);
    // Makes `changes` (by position, apart) as one undo step, the caret left
    // after the text of `changes[main]` (a completion and its other edits).
    void applyChanges(const QList<TextDocument::Change> &changes, qsizetype main);
    Q_INVOKABLE void select(qint64 start, qint64 end);
    Q_INVOKABLE void selectAll();
    // As in VS Code and Qt Creator: selects the word at the caret, or adds the
    // next occurrence of the selection (after it, around the end) with a
    // caret of its own; typing, deleting and pasting then happen at each.
    Q_INVOKABLE void selectNextOccurrence();
    // As Qt Creator's Select Block Up: the selection grows to the next
    // enclosing block (its lines inside, then with its first and last),
    // by the folding blocks, and at last to all the text.
    Q_INVOKABLE void selectBlock();
    // As Select Block Down: back to the selection before the last selectBlock() (while still selected).
    Q_INVOKABLE void unselectBlock();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void copy() const;
    Q_INVOKABLE void paste();
    // The text now matches the file: no longer modified.
    Q_INVOKABLE void markSaved() { m_document->markSaved(); }
    // Picks the syntax for a file named `fileName` (or by its shebang).
    Q_INVOKABLE void detectSyntax(const QString &fileName) { m_document->detectSyntax(fileName); }
    // The selected lines (or the caret's) one level in or out, as one edit.
    Q_INVOKABLE void indent();
    Q_INVOKABLE void outdent();
    // Indents the selected lines (or the caret's) again by their brackets,
    // without knowing the language: a level inside each one left open.
    // Nothing for languages indentation means something in (Python).
    Q_INVOKABLE void reindent();
    // Comments the selected lines (or the caret's) out with the language's
    // marker, or back in when all are (commenting::toggledLines()).
    Q_INVOKABLE void toggleLineComment();
    // Puts the selection between the language's block markers, or takes it
    // out of them; the caret's line without a selection. Line comments for a
    // language without block ones (Python).
    Q_INVOKABLE void toggleBlockComment();
    // The same for the lines edited since the text was set or saved
    // (before saving), changing only what moves: one undo step.
    Q_INVOKABLE void reindentEdited();
    // Takes the indentation style from the text (see indentation::detect()).
    Q_INVOKABLE void detectIndentation() { m_document->detectIndentation(); }
    // Moves the caret to "line" or "line:column" (from 1, clamped); false
    // when `target` is neither.
    Q_INVOKABLE bool goToLine(const QString &target);
    // Select the next (previous) match after (before) the caret, wrapping;
    // false when there is none.
    Q_INVOKABLE bool findNext();
    Q_INVOKABLE bool findPrevious();
    // Replaces the selected match (else finds the next one first) and finds
    // the next; with a regular expression, $0…$9 stand for its captures.
    Q_INVOKABLE void replaceCurrent(const QString &replacement);
    // Replaces every match, as one undoable edit; returns how many.
    Q_INVOKABLE qint32 replaceAll(const QString &replacement);
    // Folds the block that starts on the caret's line, else the one around
    // it (the caret goes to the folded line); the gutter's markers and the
    // placeholder after a folded line fold and unfold too.
    Q_INVOKABLE void fold();
    // Unfolds what is folded under the caret's line.
    Q_INVOKABLE void unfold();
    Q_INVOKABLE void unfoldAll();

signals:
    void documentChanged();
    void fontChanged();
    void colorsChanged();
    void contentXChanged();
    void contentYChanged();
    void contentSizeChanged();
    void textChanged();
    void cursorChanged();
    // The caret moved (navigation, an edit, new text): unlike cursorChanged,
    // not for a relayout under it. What to keep in view.
    void caretMoved();
    // A relayout (width, font, wrapping) moved the lines: scroll to
    // `contentY` to keep the line that was at the top there.
    void scrollRequested(qreal contentY);
    void selectionChanged();
    void undoStateChanged();
    void canPasteChanged();
    void modifiedChanged();
    void syntaxChanged();
    void syntaxThemeChanged();
    void searchChanged();
    void indentationChanged();
    void defaultIndentationChanged();
    void layoutOptionsChanged();
    // A right click (or ⌃click), the caret moved there unless on the selection.
    void contextMenuRequested();
    // Text typed at the caret (a key, an input method's commit).
    void textTyped(const QString &text);
    // A ⌘click: the caret moved to `position`, whose definition is wanted.
    void definitionRequested(qint64 position);
    void diagnosticAreasChanged();
    // Highlighting went further down the text (a slice ahead of the view).
    void highlightingAdvanced();

protected:
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    [[nodiscard]] QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    friend class EditorAccessible;
    friend class Minimap;

    // A laid out line in view. Rows are found again by their text (and
    // `variant` and highlighting state), not their line number: an edit
    // above a line moves it but keeps its layout, and its scene graph node
    // (matched by `id`).
    struct Row {
        quint64 id{0};
        qsizetype line{-1};
        // Plain, Highlighted, Guessed (from a guessed state), or the preedit
        // generation for the line showing a composition.
        quint64 variant{0};
        QString text;
        std::unique_ptr<QTextLayout> layout;
        // The highlighting state the line starts in.
        KSyntaxHighlighting::State state;
        // The language server's tokens it was laid out with.
        QList<TextDocument::SemanticToken> semantic;
        qreal width{0.0};
        // Its top, relative to m_baseLine's (rows above it may wrap).
        qreal top{0.0};
        // The folding regions it opens and leaves open.
        qsizetype opens{0};
    };
    static constexpr quint64 Plain{0U};
    static constexpr quint64 Highlighted{1U};
    static constexpr quint64 Guessed{2U};

    // A laid out line number in view, found again by its line; its scene
    // graph node is matched by `id` (a new layout, e.g. in another font,
    // gets a new one).
    struct Number {
        quint64 id{0};
        qsizetype line{-1};
        std::unique_ptr<QTextLayout> layout;
        qreal width{0.0};
    };

    // A fold marker in the gutter: its line * 2 (+ 1 when folded), and top
    // relative to m_baseLine's.
    struct FoldMark {
        qsizetype key{0};
        qreal top{0.0};
    };

    // A diagnostic's underline on one visual line (relative to m_baseLine).
    struct Squiggle {
        QRectF rect;
        qint64 severity{0};
        friend bool operator==(const Squiggle &, const Squiggle &) = default;
    };

    // What follows a folded line (relative to m_baseLine), unfolding it when clicked.
    struct Placeholder {
        QRectF rect;
        qsizetype line{0};
    };

    // How a move treats the selection.
    enum class Select : quint8 {
        Collapse,
        Extend
    };

    struct Colors {
        QRgb text{0xff1d1d1fU};
        QRgb selection{0x553d8bffU};
        QRgb caret{0xff3d8bffU};
        QRgb currentLine{0x0a000000U};
        QRgb lineNumber{0x66000000U};
        QRgb bracket{0x403d8bffU};
        QRgb word{0x1a000000U};
        QRgb match{0x66ffd60aU};
        QRgb guide{0x1f000000U};
        QRgb scope{0xb33d8bffU};
        QRgb error{0xffff3b30U};
        QRgb warning{0xffff9500U};
        QRgb information{0xff007affU};
        QRgb hint{0x66000000U};
    };

    // This view is editing: replaced() is its own change.
    static constexpr quint64 Editing{1U << 0U};
    // The press was on the selection: a drag may start from it.
    static constexpr quint64 DragPending{1U << 1U};
    // A drag from this view was dropped back into it (and moved there).
    static constexpr quint64 DroppedHere{1U << 2U};
    static constexpr quint64 Wrap{1U << 4U};
    static constexpr quint64 ShowInvisibles{1U << 5U};
    // m_invisibleRects changed since the scene last took them.
    static constexpr quint64 InvisiblesChanged{1U << 6U};
    // selectNextOccurrence() started from a caret (a word): it adds whole words only.
    static constexpr quint64 WholeWords{1U << 7U};
    // The fold markers' glyphs changed (the font did): their nodes are filled again.
    static constexpr quint64 FoldGlyphsChanged{1U << 8U};
    static constexpr quint64 ShowIndentGuides{1U << 9U};
    // m_guideRects/m_scopeGuideRects changed since the scene last took them.
    static constexpr quint64 GuidesChanged{1U << 10U};
    // The blinking caret is in its hidden half.
    static constexpr quint64 CaretOff{1U << 11U};
    // m_squiggles changed since the scene last took them.
    static constexpr quint64 DiagnosticsChanged{1U << 12U};

    [[nodiscard]] const TextBuffer &buffer() const noexcept { return m_document->buffer(); }
    [[nodiscard]] SyntaxHighlighter &highlighter() const noexcept { return m_document->highlighter(); }
    // Follows `document`'s changes; the view starts over on it.
    void attach(TextDocument *document);
    // Another view's (or this view's) change to the text.
    void documentReplaced(qsizetype position, const QString &removed, const QString &inserted);
    void documentReset(bool keepPlace);
    void documentIndentationChanged();
    void documentHighlightingChanged();
    // Back to the start of the text, or to `place`.
    void startOver(const TextDocument::Place &place);
    // After an edit or undo by this view: the caret goes to `caret`.
    void editedTo(TextDocument::Caret caret);
    void updateMetrics();
    // The one way this view changes the text: replaces `count` characters
    // at `position` with `text` in the document, recorded for undo; typing
    // merges into the previous edit while `mergeable`.
    void replace(qsizetype position, qsizetype count, const QString &text, bool mergeable);
    // Several carets: replaces at each the range `rangeOf(caret)` gives with
    // `text`, as one undo step; the carets end up after it.
    template <typename RangeOf> void replaceEach(RangeOf rangeOf, const QString &text, bool mergeable);
    // How a horizontal key moves each caret: by word (⌥), to the line's edge
    // (⌘), extending its selection (⇧).
    struct Step {
        bool forward;
        bool word;
        bool line;
        bool extend;
    };
    // Several carets: each moves as `step` says (a selection collapsing without ⇧).
    void moveEach(Step step);
    // A caret on the row above (-1) or below (1) the outermost one that way,
    // at the main caret's column; with `block`, at its selection's columns too.
    void addCaretVertically(qint32 lines, bool block);
    // The visual row `position` is on.
    [[nodiscard]] qsizetype rowAt(qsizetype position) const;
    // Drops the other carets; false when there were none.
    bool clearOthers();
    // The next occurrence of `needle` (one line) at or after `from`, around
    // the end, that no caret's selection covers (a whole word with WholeWords).
    [[nodiscard]] std::optional<TextDocument::Range> nextOccurrence(const QString &needle, qsizetype from) const;
    // A caret's selection overlaps [start, end).
    [[nodiscard]] bool covered(qsizetype start, qsizetype end) const;
    // Where Backspace (Delete) from `cursor` deletes to, with ⌘ or ⌥.
    [[nodiscard]] qsizetype deletionTarget(qsizetype cursor, bool backspace, bool command, bool option) const;
    void deleteSelection();
    void moveCaret(qsizetype position, Select select);
    void moveVertically(qsizetype lines, Select select);
    [[nodiscard]] qsizetype wordBoundary(qsizetype position, bool forward) const;
    // Where a block starting on `line` folds; none when nothing does.
    [[nodiscard]] std::optional<folding::Fold> foldUnder(qsizetype line);
    // The block around `line` (starting above it).
    [[nodiscard]] std::optional<folding::Fold> foldAround(qsizetype line);
    void applyFold(folding::Fold fold);
    void toggleFold(qsizetype line);
    void foldsChanged();
    // The line whose fold marker or placeholder is at `point`.
    [[nodiscard]] std::optional<qsizetype> foldTargetAt(const QPointF &point) const;
    // ⌘ over a word whose definition can be asked for: a link (pointing hand, underline).
    void updateLink(Qt::KeyboardModifiers modifiers);
    void setLink(qsizetype start, qsizetype end);
    void layoutLink();
    // A move by one character into folded lines goes past them.
    [[nodiscard]] qsizetype skipFolded(qsizetype position, bool forward) const;
    [[nodiscard]] qreal foldColumnWidth() const;
    void layoutFoldMarks();
    void placeFoldMarks(QQuickWindow *window, EditorNode *node);
    [[nodiscard]] bool handleMoveEach(const QKeyEvent *event);
    [[nodiscard]] bool handleNavigation(const QKeyEvent *event);
    [[nodiscard]] bool handleDeletion(const QKeyEvent *event);
    [[nodiscard]] qsizetype firstVisibleLine() const;
    [[nodiscard]] qsizetype visibleLineCount() const;
    [[nodiscard]] QString displayText(qsizetype line) const;
    [[nodiscard]] std::unique_ptr<QTextLayout> layoutLine(qsizetype line) const;
    [[nodiscard]] std::unique_ptr<QTextLayout>
    layoutText(const QString &text, qsizetype preeditColumn, const QList<QTextLayout::FormatRange> &formats = {}) const;
    void layoutRows();
    [[nodiscard]] Row takeRow(qsizetype line, qsizetype cursorLine);
    // The variant of a row for `line` (not composing) and the state it starts in.
    [[nodiscard]] std::pair<quint64, KSyntaxHighlighting::State> rowStart(qsizetype line) const;
    [[nodiscard]] Number takeNumber(qsizetype line);
    void layoutDecorations();
    void layoutMarks();
    void layoutMatches();
    void layoutInvisibles();
    void layoutGuides();
    void layoutDiagnostics();
    // The underlines of `diagnostic` on the rows laid out, and their areas.
    void
    appendDiagnostic(const TextDocument::Diagnostic &diagnostic, QList<Squiggle> &squiggles, QVariantList &areas) const;
    [[nodiscard]] QRgb severityColor(qint64 severity) const noexcept;
    void fillSquiggles(QSGGeometryNode *node) const;
    // The columns `row`'s line is indented by; a blank line's, as its
    // neighbours (the lesser), so guides go through it.
    [[nodiscard]] qsizetype guideIndent(const Row &row) const;
    // The indented block the caret is in (or that starts on its line).
    [[nodiscard]] std::optional<folding::Fold> caretBlock() const;
    void setSearch(const QString &pattern, bool caseSensitive, bool regularExpression);
    // Searches a slice, then again from the event loop until done.
    void searchAhead();
    void selectMatch(qsizetype index);
    // Replaces the lines the selection (or caret) touches with
    // `change(their text)` and selects them.
    // Replaces the selected lines (or the caret's) by `change` of their text, as one edit.
    template <typename Change> void changeSelectedLines(Change change);
    // Return: a newline indented as the line before, a level more after a
    // bracket left open (between brackets, the closing one on a line of its own).
    void insertNewline();
    // Typing an opening bracket or quote adds its closing one after the
    // caret; typing a closing one before the same steps over it. False when
    // `text` is typed as it is.
    bool typePair(const QString &text);
    // Return after an opening bracket left open at a line's end, with no
    // block under it yet: the closing one two lines down.
    [[nodiscard]] std::optional<QChar> closerToAdd(qsizetype line, QStringView before, QStringView rest) const;
    // After a closing bracket typed where only indentation was: the line
    // indented as the one the bracket it closes is on.
    void alignClosing();
    // The indentation lines firstLine..lastLine get indented again (see
    // reindent()), in columns, -1 where it stays as it is; none when that
    // cannot be told.
    [[nodiscard]] std::optional<QList<qint32>> reindented(qsizetype firstLine, qsizetype lastLine);
    // Lines firstLine..lastLine with that indentation (`columns` from line `base` on).
    // Gives `moved`'s lines (in order) their indentation, as one undo step.
    void applyIndentation(const QList<std::pair<qsizetype, qint32>> &moved);
    [[nodiscard]] QString
    reindentedText(qsizetype firstLine, qsizetype lastLine, const QList<qint32> &columns, qsizetype base) const;
    // The comments and strings of `line` (whose text is `text`), where brackets don't count.
    [[nodiscard]] QList<indentation::Span> nonCode(qsizetype line, QStringView text);
    // A ':' ending a line opens a block (indentation-based languages).
    [[nodiscard]] bool colonBlocks() const;
    void insertTab();
    [[nodiscard]] indentation::Style indentStyle() const;
    // The language's comment markers; none without highlighting.
    [[nodiscard]] commenting::Markers commentMarkers() const;
    // The rectangles of columns [start, end) of `row`, one per visual line
    // they cross (relative to m_baseLine), appended to `rects`.
    void appendRangeRects(QList<QRectF> &rects, const Row &row, qsizetype start, qsizetype end) const;
    void appendSelectionRects(
        const Row &row,
        qsizetype lineStart,
        qsizetype lineEnd,
        qsizetype selectedFrom,
        qsizetype selectedTo
    );
    // The caret's rectangle at `column` of `row` (relative to m_baseLine).
    [[nodiscard]] QRectF caretRect(const Row &row, qsizetype column) const;
    // The other carets and their selections on `row`.
    void layoutOthers(const Row &row, qsizetype lineStart, qsizetype lineEnd);
    [[nodiscard]] std::unique_ptr<QTextLayout> layoutNumber(qsizetype line) const;
    [[nodiscard]] qsizetype positionAt(const QPointF &point) const;
    [[nodiscard]] qreal textLeft() const;
    // Visual rows: the same as lines unless they wrap or fold.
    [[nodiscard]] qsizetype rowOfLine(qsizetype line) const;
    // The same without folds.
    [[nodiscard]] qsizetype unfoldedRow(qsizetype line) const;
    [[nodiscard]] qsizetype unfoldedLine(qsizetype row) const;
    [[nodiscard]] qsizetype lineAtRow(qsizetype row) const;
    [[nodiscard]] qsizetype totalRows() const;
    // How wide wrapped lines are laid out.
    [[nodiscard]] qreal wrapWidth() const;
    // The wrap index estimated again (width, font or wrapping changed),
    // keeping `anchorLine` at the top of the view.
    void resetWrap(qsizetype anchorLine);
    // The line at the top of the view, and at its bottom.
    [[nodiscard]] qsizetype topLine() const;
    [[nodiscard]] qsizetype bottomLine() const;
    // The top of `line`, in content coordinates.
    [[nodiscard]] qreal lineTop(qsizetype line) const;
    // The layout of `line` if it is laid out in view, else nullptr.
    [[nodiscard]] const QTextLayout *laidOut(qsizetype line) const;
    // The caret's rectangle at `position`, in content coordinates.
    [[nodiscard]] QRectF positionRect(qsizetype position) const;
    // The position on visual row `row` closest to `x` (text coordinates).
    [[nodiscard]] qsizetype positionAtRow(qsizetype row, qreal x) const;
    // Tells assistive technologies about a change of text, caret or selection.
    void notifyTextChanged(qsizetype position, const QString &removed, const QString &inserted);
    void notifyCursorChanged();
    // Highlights ahead of the view, a slice per event loop pass.
    void highlightAhead();
    // selectBlock()'s step to `span`, remembering the selection before it.
    void growSelection(const std::pair<qint64, qint64> &span);
    // Shows the caret and starts its blinking over (it blinks only with focus).
    void restartBlink();
    [[nodiscard]] qsizetype lastVisibleLine() const;
    void startDrag();
    void pressForContextMenu(const QPointF &point);
    void setDropPosition(qsizetype position);
    void setColor(QRgb &field, const QColor &color);

    // The view's own document, shown while `document` is null.
    std::unique_ptr<TextDocument> m_ownDocument;
    QPointer<TextDocument> m_document;
    WrapIndex m_wrapIndex;
    folding::FoldIndex m_folds;
    std::unique_ptr<TextSearch> m_search;
    // The fold markers' glyphs (made when first needed) and the placeholder's.
    std::unique_ptr<QTextLayout> m_foldableGlyph;
    std::unique_ptr<QTextLayout> m_foldedGlyph;
    std::unique_ptr<QTextLayout> m_placeholderGlyph;
    // Move-only rows: std::vector (QList needs copyable elements).
    std::vector<Row> m_rows;
    std::vector<Number> m_numbers;
    // Highlighting in view where the highlighter has not reached yet.
    SyntaxHighlighter::Guess m_guess;
    // Set by updatePolish() for updatePaintNode(), relative to m_baseLine's
    // top (so coordinates stay small in huge files): the selection, one
    // rectangle per line, and the caret.
    QList<QRectF> m_selectionRects;
    QList<QRectF> m_otherCaretRects;
    // The carets selectNextOccurrence() and ⌥↑/↓ added besides m_cursor/m_anchor.
    QList<TextDocument::Caret> m_others;
    // The selections selectBlock() grew from, for unselectBlock(), and the one it made.
    QList<std::pair<qint64, qint64>> m_blockSteps;
    std::pair<qint64, qint64> m_blockSelection{-1, -1};
    QList<QRectF> m_bracketRects;
    QList<QRectF> m_wordRects;
    QList<QRectF> m_matchRects;
    // Dots for spaces and dashes for tabs, when shown.
    QList<QRectF> m_invisibleRects;
    // Indent guides: 1 px wide, a rectangle per row and level.
    QList<QRectF> m_guideRects;
    QList<QRectF> m_scopeGuideRects;
    QList<Squiggle> m_squiggles;
    // The link's underline, a rectangle per row.
    QList<QRectF> m_linkRects;
    QVariantList m_diagnosticAreas;
    QList<FoldMark> m_foldMarks;
    QList<Placeholder> m_placeholders;
    QRectF m_caretRect;
    QString m_preedit;
    QString m_syntaxTheme;
    QFont m_font;
    QElapsedTimer m_lastDoubleClick;
    // Where a press on the selection happened, for starting a drag.
    QPointF m_pressPoint;
    QTimer m_highlightTimer;
    QTimer m_searchTimer;
    QTimer m_blinkTimer;
    qreal m_contentX{0.0};
    qreal m_contentY{0.0};
    qreal m_lineHeight{16.0};
    qreal m_charWidth{8.0};
    // The widest line laid out so far: the horizontal scroll range grows as
    // lines come into view (measuring every line would walk the whole text).
    qreal m_widestLine{0.0};
    // The x vertical moves aim for, kept across short lines.
    qreal m_preferredX{-1.0};
    // The same for the anchor, as carets added with ⌥⇧↑/↓ keep their columns.
    qreal m_preferredAnchorX{-1.0};
    // The width lines were wrapped at (to notice it change).
    qreal m_wrapWidth{0.0};
    // m_baseLine's top in content coordinates.
    qreal m_baseTop{0.0};
    qsizetype m_cursor{0};
    qsizetype m_anchor{0};
    qsizetype m_preeditCursor{0};
    // The first line laid out; the scene is placed relative to it.
    qsizetype m_baseLine{0};
    // Where dragged text would be dropped (shown as the caret); -1: no drag.
    qsizetype m_dropPosition{-1};
    // The word shown as a link while ⌘ is held over it; -1: none.
    qsizetype m_linkStart{-1};
    qsizetype m_linkEnd{-1};
    quint64 m_nextRowId{1};
    // Bumped when the composition changes, so its line is laid out again
    // (above Plain, Highlighted and Guessed).
    quint64 m_preeditGeneration{3};
    quint64 m_flags{0U};
    Colors m_colors;
};

} // namespace kode
