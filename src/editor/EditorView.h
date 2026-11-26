#pragma once

#include "Folding.h"
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
#include <QtQmlIntegration/qqmlintegration.h>

#include <KSyntaxHighlighting/State>

#include <memory>
#include <optional>
#include <vector>

namespace KSyntaxHighlighting
{
class Definition;
} // namespace KSyntaxHighlighting

namespace tekst
{

class EditorAccessible;
class EditorNode;

namespace indentation
{
struct Style;
} // namespace indentation
class SyntaxHighlighter;
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
    Q_PROPERTY(tekst::TextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
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
    Q_INVOKABLE void select(qint64 start, qint64 end);
    Q_INVOKABLE void selectAll();
    // As in VS Code and Qt Creator: selects the word at the caret, or adds the
    // next occurrence of the selection (after it, around the end) with a
    // caret of its own; typing, deleting and pasting then happen at each.
    Q_INVOKABLE void selectNextOccurrence();
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

protected:
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void keyPressEvent(QKeyEvent *event) override;
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

    // A laid out line in view. Rows are found again by their text (and
    // `variant` and highlighting state), not their line number: an edit
    // above a line moves it but keeps its layout, and its scene graph node
    // (matched by `id`).
    struct Row {
        quint64 id{0};
        qsizetype line{-1};
        // Plain, Highlighted, or the preedit generation for the line
        // showing a composition.
        quint64 variant{0};
        QString text;
        std::unique_ptr<QTextLayout> layout;
        // The highlighting state the line starts in.
        KSyntaxHighlighting::State state;
        qreal width{0.0};
        // Its top, relative to m_baseLine's (rows above it may wrap).
        qreal top{0.0};
        // The folding regions it opens and leaves open.
        qsizetype opens{0};
    };
    static constexpr quint64 Plain{0U};
    static constexpr quint64 Highlighted{1U};

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
    [[nodiscard]] Number takeNumber(qsizetype line);
    void layoutDecorations();
    void layoutMarks();
    void layoutMatches();
    void layoutInvisibles();
    void layoutGuides();
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
    void changeSelectedLines(bool outdenting);
    // Return: a newline and the current line's indentation.
    void insertNewline();
    void insertTab();
    [[nodiscard]] indentation::Style indentStyle() const;
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
    // The line at the top of the view.
    [[nodiscard]] qsizetype topLine() const;
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
    [[nodiscard]] qsizetype lastVisibleLine() const;
    void startDrag();
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
    // Set by updatePolish() for updatePaintNode(), relative to m_baseLine's
    // top (so coordinates stay small in huge files): the selection, one
    // rectangle per line, and the caret.
    QList<QRectF> m_selectionRects;
    QList<QRectF> m_otherCaretRects;
    // The carets selectNextOccurrence() and ⌥↑/↓ added besides m_cursor/m_anchor.
    QList<TextDocument::Caret> m_others;
    QList<QRectF> m_bracketRects;
    QList<QRectF> m_wordRects;
    QList<QRectF> m_matchRects;
    // Dots for spaces and dashes for tabs, when shown.
    QList<QRectF> m_invisibleRects;
    // Indent guides: 1 px wide, a rectangle per row and level.
    QList<QRectF> m_guideRects;
    QList<QRectF> m_scopeGuideRects;
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
    quint64 m_nextRowId{1};
    // Bumped when the composition changes, so its line is laid out again
    // (above Plain and Highlighted).
    quint64 m_preeditGeneration{2};
    Colors m_colors;
    quint64 m_flags{0U};
};

} // namespace tekst
