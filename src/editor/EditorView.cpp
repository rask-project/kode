#include "EditorView.h"

#include "EditorAccessible.h"
#include "Matching.h"
#include "SyntaxHighlighter.h"

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Theme>

#include <QClipboard>
#include <QDeadlineTimer>
#include <QDrag>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMatrix4x4>
#include <QMimeData>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGRectangleNode>
#include <QSGTextNode>
#include <QStyleHints>
#include <QTextLayout>
#include <QTextOption>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace tekst
{

namespace
{

constexpr qreal padding{12.0};
constexpr qreal topPadding{6.0};
constexpr qreal caretWidth{2.0};
// Lines laid out above and below the view, so short scrolls need no layout.
constexpr qsizetype overscan{4};
// No wrapping: lines are laid out this wide.
constexpr qreal noWrapWidth{1.0e7};
// Highlighting: how long a layout may wait for it (shown plain past that),
// a slice of it ahead of the view, and how far ahead it runs.
constexpr std::chrono::milliseconds highlightInLayout{4};
constexpr std::chrono::milliseconds highlightSlice{3};
constexpr qsizetype highlightLookahead{1000};

using matching::isWordCharacter;

// How far the bracket matching the one at the caret is searched for.
constexpr qsizetype bracketSearchLines{2000};

// What a text node shows: refilled only when its color changes (the layout
// behind a key never changes).
struct Shown {
    QSGTextNode *node{nullptr};
    QColor color;
};

// The scene graph side. Children, bottom to top: the current-line band, the
// clip of the text area (with `content`: marks, selection, rows, caret) and the
// line numbers (`gutter`). Both transforms carry the scroll, relative to the
// first line laid out; each text node sits at its line inside them. All
// owned by this node.
class EditorNode : public QSGNode
{
public:
    QSGRectangleNode *currentLine{nullptr};
    QSGClipNode *textClip{nullptr};
    QSGTransformNode *content{nullptr};
    QSGNode *marksLayer{nullptr};
    QSGNode *selectionLayer{nullptr};
    QSGNode *textLayer{nullptr};
    QSGRectangleNode *caret{nullptr};
    QSGTransformNode *gutter{nullptr};
    QList<QSGRectangleNode *> selection;
    QList<QSGRectangleNode *> brackets;
    QList<QSGRectangleNode *> words;
    QHash<quint64, Shown> rows;
    QHash<qsizetype, Shown> numbers;
};

std::unique_ptr<QSGClipNode> makeClip()
{
    auto clip{std::make_unique<QSGClipNode>()};
    auto geometry{std::make_unique<QSGGeometry>(QSGGeometry::defaultAttributes_Point2D(), 4)};
    clip->setGeometry(geometry.release());
    clip->setFlag(QSGNode::OwnsGeometry);
    clip->setIsRectangular(true);
    return clip;
}

void setClipRect(QSGClipNode *clip, const QRectF &rect)
{
    if (clip->clipRect() == rect) {
        return;
    }
    QSGGeometry::updateRectGeometry(clip->geometry(), rect);
    clip->setClipRect(rect);
    clip->markDirty(QSGNode::DirtyGeometry);
}

void setTranslation(QSGTransformNode *node, QPointF position)
{
    QMatrix4x4 matrix;
    matrix.translate(static_cast<float>(position.x()), static_cast<float>(position.y()));
    if (node->matrix() != matrix) {
        node->setMatrix(matrix);
    }
}

// Places the text node showing `key`: the one from `previous` (filled again
// only if `color` changed), else a new one under `parent`; then moves it to
// `position` and keeps it in `current`.
template <typename Key>
void placeText(
    QQuickWindow *window,
    QSGNode *parent,
    QHash<Key, Shown> &previous,
    QHash<Key, Shown> &current,
    Key key,
    QTextLayout *layout,
    const QColor &color,
    QPointF position
)
{
    Shown shown{previous.take(key)};
    if (shown.node == nullptr) {
        auto created{std::unique_ptr<QSGTextNode>{window->createTextNode()}};
        created->setRenderType(QSGTextNode::NativeRendering);
        shown.node = created.get();
        parent->appendChildNode(created.release());
    }
    if (shown.color != color) {
        shown.node->clear();
        shown.node->setColor(color);
        shown.node->addTextLayout({}, layout);
        shown.color = color;
    }
    setTranslation(shown.node, position);
    current.insert(key, shown);
}

// Deletes the text nodes left in `nodes` (lines no longer in view).
template <typename Key> void removeText(QSGNode *parent, QHash<Key, Shown> &nodes)
{
    for (const Shown &shown : std::as_const(nodes)) {
        // Removed from its parent, which owned it: ours to delete.
        const std::unique_ptr<QSGTextNode> node{shown.node};
        parent->removeChildNode(node.get());
    }
    nodes.clear();
}

// Grows or shrinks `nodes` to `count` rectangles under `parent`.
void resizeRectangles(QQuickWindow *window, QSGNode *parent, QList<QSGRectangleNode *> &nodes, qsizetype count)
{
    while (nodes.size() < count) {
        auto rectangle{std::unique_ptr<QSGRectangleNode>{window->createRectangleNode()}};
        nodes.append(rectangle.get());
        parent->appendChildNode(rectangle.release());
    }
    while (nodes.size() > count) {
        const std::unique_ptr<QSGRectangleNode> rectangle{nodes.takeLast()};
        parent->removeChildNode(rectangle.get());
    }
}
// Shows `rects` in `color` with the rectangles of `nodes` under `parent`.
void syncRectangles(
    QQuickWindow *window,
    QSGNode *parent,
    QList<QSGRectangleNode *> &nodes,
    const QList<QRectF> &rects,
    const QColor &color
)
{
    resizeRectangles(window, parent, nodes, rects.size());
    for (qsizetype i{0}; i < rects.size(); ++i) {
        nodes.at(i)->setRect(rects.at(i));
        nodes.at(i)->setColor(color);
    }
}
} // namespace

EditorView::EditorView(QQuickItem *parent)
    : QQuickItem(parent)
    , m_highlighter{std::make_unique<SyntaxHighlighter>(&m_buffer)}
    , m_font(QStringLiteral("Menlo"), 13)
{
    m_highlighter->setTheme(SyntaxHighlighter::defaultTheme(false));
    m_highlightTimer.setSingleShot(true);
    m_highlightTimer.setInterval(0);
    connect(&m_highlightTimer, &QTimer::timeout, this, &EditorView::highlightAhead);
    setFlag(ItemHasContents);
    setFlag(ItemAcceptsInputMethod);
    setFlag(ItemIsFocusScope);
    setAcceptedMouseButtons(Qt::LeftButton);
    setFlag(ItemAcceptsDrops);
    setActiveFocusOnTab(true);
    setCursor(Qt::IBeamCursor);
    EditorAccessible::installFactory();
    updateMetrics();
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &EditorView::canPasteChanged);
}

EditorView::~EditorView() = default;

// --- properties -----------------------------------------------------------

void EditorView::setFont(const QFont &font)
{
    if (m_font == font) {
        return;
    }
    m_font = font;
    updateMetrics();
    m_widestLine = 0.0;
    m_rows.clear();
    m_numbers.clear();
    emit fontChanged();
    emit contentSizeChanged();
    emit cursorChanged();
    polish();
}

QString EditorView::syntax() const
{
    return m_highlighter->isActive() ? m_highlighter->definition().name() : QString{};
}

void EditorView::setSyntax(const QString &syntax)
{
    applyDefinition(SyntaxHighlighter::definitionForName(syntax));
}

void EditorView::detectSyntax(const QString &fileName)
{
    applyDefinition(SyntaxHighlighter::detect(fileName, m_buffer.lineCount() > 0 ? m_buffer.line(0) : QString{}));
}

void EditorView::applyDefinition(const KSyntaxHighlighting::Definition &definition)
{
    if (m_highlighter->definition() == definition) {
        return;
    }
    m_highlighter->setDefinition(definition);
    m_rows.clear();
    emit syntaxChanged();
    polish();
}

void EditorView::setDarkTheme(bool dark)
{
    if (darkTheme() == dark) {
        return;
    }
    m_flags = dark ? (m_flags | DarkTheme) : (m_flags & ~DarkTheme);
    m_highlighter->setTheme(SyntaxHighlighter::defaultTheme(dark));
    m_rows.clear();
    emit darkThemeChanged();
    polish();
}

void EditorView::textReplaced(qsizetype position, const QString &removed, const QString &inserted)
{
    m_highlighter
        ->linesReplaced(m_buffer.lineAt(position), removed.count(QLatin1Char('\n')), inserted.count(QLatin1Char('\n')));
    notifyTextChanged(position, removed, inserted);
}

void EditorView::setColor(QRgb &field, const QColor &color)
{
    if (field == color.rgba()) {
        return;
    }
    field = color.rgba();
    emit colorsChanged();
    update();
}

void EditorView::setTextColor(const QColor &color)
{
    setColor(m_colors.text, color);
}

void EditorView::setSelectionColor(const QColor &color)
{
    setColor(m_colors.selection, color);
}

void EditorView::setCaretColor(const QColor &color)
{
    setColor(m_colors.caret, color);
}

void EditorView::setCurrentLineColor(const QColor &color)
{
    setColor(m_colors.currentLine, color);
}

void EditorView::setLineNumberColor(const QColor &color)
{
    setColor(m_colors.lineNumber, color);
}

void EditorView::setBracketColor(const QColor &color)
{
    setColor(m_colors.bracket, color);
}

void EditorView::setWordColor(const QColor &color)
{
    setColor(m_colors.word, color);
}

void EditorView::setContentX(qreal contentX)
{
    if (qFuzzyCompare(m_contentX, contentX)) {
        return;
    }
    m_contentX = contentX;
    emit contentXChanged();
    update();
}

void EditorView::setContentY(qreal contentY)
{
    if (qFuzzyCompare(m_contentY, contentY)) {
        return;
    }
    m_contentY = contentY;
    emit contentYChanged();
    // Scrolling within the lines already laid out only moves the scene.
    if (firstVisibleLine() != m_baseLine) {
        polish();
    } else {
        update();
    }
}

qreal EditorView::gutterWidth() const
{
    const auto digits{std::max(3, static_cast<qint32>(std::log10(static_cast<qreal>(m_buffer.lineCount()))) + 1)};
    return (static_cast<qreal>(digits) * m_charWidth) + (2.0 * padding);
}

qreal EditorView::textLeft() const
{
    return gutterWidth() + padding;
}

qreal EditorView::lineTop(qsizetype line) const
{
    return topPadding + (static_cast<qreal>(line) * m_lineHeight);
}

void EditorView::notifyTextChanged(qsizetype position, const QString &removed, const QString &inserted)
{
    if (!QAccessible::isActive()) {
        return;
    }
    QAccessibleTextUpdateEvent event{this, static_cast<int>(position), removed, inserted};
    QAccessible::updateAccessibility(&event);
}

void EditorView::notifyCursorChanged()
{
    if (!QAccessible::isActive()) {
        return;
    }
    if (hasSelection()) {
        QAccessibleTextSelectionEvent event{this, static_cast<int>(selectionStart()), static_cast<int>(selectionEnd())};
        event.setCursorPosition(static_cast<int>(m_cursor));
        QAccessible::updateAccessibility(&event);
        return;
    }
    QAccessibleTextCursorEvent event{this, static_cast<int>(m_cursor)};
    QAccessible::updateAccessibility(&event);
}

qreal EditorView::contentWidth() const
{
    return textLeft() + m_widestLine + m_charWidth + padding;
}

qreal EditorView::contentHeight() const
{
    return topPadding + (static_cast<qreal>(m_buffer.lineCount()) * m_lineHeight) + padding;
}

void EditorView::setCursorPosition(qint64 position)
{
    moveCaret(position, Select::Collapse);
}

qint32 EditorView::cursorLine() const
{
    return static_cast<qint32>(m_buffer.lineAt(m_cursor));
}

qint32 EditorView::cursorColumn() const
{
    return static_cast<qint32>(m_cursor - m_buffer.lineStart(m_buffer.lineAt(m_cursor)));
}

QRectF EditorView::cursorRectangle() const
{
    const qsizetype line{m_buffer.lineAt(m_cursor)};
    return {
        textLeft() + xForPosition(m_cursor),
        topPadding + (static_cast<qreal>(line) * m_lineHeight),
        caretWidth,
        m_lineHeight
    };
}

bool EditorView::canPaste()
{
    return !QGuiApplication::clipboard()->text().isEmpty();
}

// --- text and edits ---------------------------------------------------------

void EditorView::setText(const QString &text)
{
    m_buffer = TextBuffer{text};
    m_highlighter->reset();
    m_undo.clear();
    m_undoIndex = 0;
    m_cleanIndex = 0;
    m_cursor = 0;
    m_anchor = 0;
    m_preedit.clear();
    m_preferredX = -1.0;
    m_widestLine = 0.0;
    m_flags &= DarkTheme;
    m_contentY = 0.0;
    m_contentX = 0.0;
    emit contentXChanged();
    emit contentYChanged();
    emit undoStateChanged();
    emit modifiedChanged();
    emit selectionChanged();
    textEdited();
}

QString EditorView::selectedText() const
{
    return m_buffer.text(selectionStart(), selectionEnd() - selectionStart());
}

void EditorView::replace(qsizetype position, qsizetype count, const QString &text, bool mergeable)
{
    const bool wasModified{modified()};
    const bool couldUndo{canUndo()};
    const bool couldRedo{canRedo()};
    const Edit edit{
        .position = position,
        .removed = m_buffer.text(position, count),
        .inserted = text,
        .cursorBefore = m_cursor,
        .anchorBefore = m_anchor,
        .cursorAfter = position + text.size()
    };

    // Forget what was undone, and the clean state if it was there.
    if (m_undoIndex < m_undo.size()) {
        m_undo.resize(m_undoIndex);
        if (m_cleanIndex > m_undoIndex) {
            m_cleanIndex = -1;
        }
    }

    // Typing and deleting one character at a time extend the last edit; a
    // new line closes the group, so a line typed after it is its own edit.
    mergeable = mergeable && !text.contains(QLatin1Char('\n'));
    bool merged{false};
    const bool canMerge{mergeable && (m_flags & MergeOpen) != 0U && !m_undo.isEmpty() && m_cleanIndex != m_undoIndex};
    if (canMerge) {
        Edit &last{m_undo.last()};
        if (count == 0 && last.removed.isEmpty() && last.position + last.inserted.size() == position) {
            last.inserted.append(text);
            last.cursorAfter = edit.cursorAfter;
            merged = true;
        } else if (text.isEmpty() && last.inserted.isEmpty() && position + count == last.position) {
            last.removed.prepend(edit.removed);
            last.position = position;
            last.cursorAfter = position;
            merged = true;
        } else if (text.isEmpty() && last.inserted.isEmpty() && position == last.position) {
            last.removed.append(edit.removed);
            merged = true;
        }
    }
    if (!merged) {
        m_undo.append(edit);
        ++m_undoIndex;
    }

    m_buffer.remove(position, count);
    m_buffer.insert(position, text);
    textReplaced(position, edit.removed, text);
    m_cursor = edit.cursorAfter;
    m_anchor = m_cursor;
    m_preferredX = -1.0;
    m_flags = mergeable ? (m_flags | MergeOpen) : (m_flags & ~MergeOpen);
    textEdited();
    emit selectionChanged();
    if (couldUndo != canUndo() || couldRedo != canRedo()) {
        emit undoStateChanged();
    }
    if (wasModified != modified()) {
        emit modifiedChanged();
    }
}

void EditorView::insert(const QString &text)
{
    replace(selectionStart(), selectionEnd() - selectionStart(), text, !hasSelection() && text.size() == 1);
}

void EditorView::deleteSelection()
{
    if (hasSelection()) {
        replace(selectionStart(), selectionEnd() - selectionStart(), {}, false);
    }
}

void EditorView::applyEdit(const Edit &edit, bool forward)
{
    const QString &from{forward ? edit.removed : edit.inserted};
    const QString &to{forward ? edit.inserted : edit.removed};
    m_buffer.remove(edit.position, from.size());
    m_buffer.insert(edit.position, to);
    textReplaced(edit.position, from, to);
    m_cursor = forward ? edit.cursorAfter : edit.cursorBefore;
    m_anchor = forward ? edit.cursorAfter : edit.anchorBefore;
}

void EditorView::undo()
{
    if (!canUndo()) {
        return;
    }
    const bool wasModified{modified()};
    --m_undoIndex;
    applyEdit(m_undo.at(m_undoIndex), false);
    m_flags &= ~MergeOpen;
    m_preferredX = -1.0;
    textEdited();
    emit selectionChanged();
    emit undoStateChanged();
    if (wasModified != modified()) {
        emit modifiedChanged();
    }
}

void EditorView::redo()
{
    if (!canRedo()) {
        return;
    }
    const bool wasModified{modified()};
    applyEdit(m_undo.at(m_undoIndex), true);
    ++m_undoIndex;
    m_flags &= ~MergeOpen;
    m_preferredX = -1.0;
    textEdited();
    emit selectionChanged();
    emit undoStateChanged();
    if (wasModified != modified()) {
        emit modifiedChanged();
    }
}

void EditorView::markSaved()
{
    if (m_cleanIndex == m_undoIndex) {
        return;
    }
    m_cleanIndex = m_undoIndex;
    m_flags &= ~MergeOpen;
    emit modifiedChanged();
}

void EditorView::select(qint64 start, qint64 end)
{
    start = std::clamp(start, qint64{0}, m_buffer.length());
    end = std::clamp(end, qint64{0}, m_buffer.length());
    m_anchor = start;
    moveCaret(end, Select::Extend);
    // Even when the caret did not move, the anchor may have.
    emit selectionChanged();
    polish();
}

void EditorView::selectAll()
{
    select(0, m_buffer.length());
}

void EditorView::cut()
{
    if (hasSelection()) {
        copy();
        deleteSelection();
    }
}

void EditorView::copy() const
{
    if (hasSelection()) {
        QGuiApplication::clipboard()->setText(selectedText());
    }
}

void EditorView::paste()
{
    QString text{QGuiApplication::clipboard()->text()};
    if (text.isEmpty()) {
        return;
    }
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    replace(selectionStart(), selectionEnd() - selectionStart(), text, false);
}

void EditorView::textEdited()
{
    emit textChanged();
    emit contentSizeChanged();
    emit cursorChanged();
    polish();
}

// --- caret and selection -------------------------------------------------------

void EditorView::moveCaret(qsizetype position, Select select)
{
    position = std::clamp(position, qsizetype{0}, m_buffer.length());
    const qsizetype anchor{select == Select::Extend ? m_anchor : position};
    m_flags &= ~MergeOpen;
    m_preferredX = -1.0;
    if (position == m_cursor && anchor == m_anchor) {
        return;
    }
    const bool selectionWasVisible{hasSelection()};
    m_cursor = position;
    m_anchor = anchor;
    emit cursorChanged();
    if (selectionWasVisible || hasSelection()) {
        emit selectionChanged();
    }
    notifyCursorChanged();
    polish();
}

void EditorView::moveVertically(qsizetype lines, Select select)
{
    const qsizetype line{m_buffer.lineAt(m_cursor)};
    const qsizetype target{std::clamp(line + lines, qsizetype{0}, m_buffer.lineCount() - 1)};
    const qreal preferredX{m_preferredX >= 0.0 ? m_preferredX : xForPosition(m_cursor)};
    const std::unique_ptr<QTextLayout> layout{layoutLine(target)};
    moveCaret(m_buffer.lineStart(target) + layout->lineAt(0).xToCursor(preferredX), select);
    m_preferredX = preferredX;
}

qsizetype EditorView::wordBoundary(qsizetype position, bool forward) const
{
    const qsizetype line{m_buffer.lineAt(position)};
    const qsizetype lineStart{m_buffer.lineStart(line)};
    const QString text{m_buffer.line(line)};
    qsizetype column{position - lineStart};
    if (forward) {
        if (column >= text.size()) {
            return std::min(position + 1, m_buffer.length());
        }
        // Past the spaces and punctuation, then past the word.
        while (column < text.size() && !isWordCharacter(text.at(column))) {
            ++column;
        }
        while (column < text.size() && isWordCharacter(text.at(column))) {
            ++column;
        }
        return lineStart + column;
    }
    if (column == 0) {
        return std::max(position - 1, qsizetype{0});
    }
    while (column > 0 && !isWordCharacter(text.at(column - 1))) {
        --column;
    }
    while (column > 0 && isWordCharacter(text.at(column - 1))) {
        --column;
    }
    return lineStart + column;
}

// --- layout and drawing -----------------------------------------------------

void EditorView::updateMetrics()
{
    const QFontMetricsF metrics{m_font};
    m_lineHeight = std::ceil(metrics.lineSpacing());
    m_charWidth = metrics.horizontalAdvance(QLatin1Char('x'));
}

qsizetype EditorView::firstVisibleLine() const
{
    const auto line{static_cast<qsizetype>(std::floor((m_contentY - topPadding) / m_lineHeight))};
    return std::clamp(line - overscan, qsizetype{0}, m_buffer.lineCount() - 1);
}

qsizetype EditorView::lastVisibleLine() const
{
    return std::min(firstVisibleLine() + visibleLineCount(), m_buffer.lineCount());
}

qsizetype EditorView::visibleLineCount() const
{
    return static_cast<qsizetype>(std::ceil(height() / m_lineHeight)) + (2 * overscan) + 1;
}

QString EditorView::displayText(qsizetype line) const
{
    QString text{m_buffer.line(line)};
    if (!m_preedit.isEmpty() && line == m_buffer.lineAt(m_cursor)) {
        text.insert(m_cursor - m_buffer.lineStart(line), m_preedit);
    }
    return text;
}

std::unique_ptr<QTextLayout> EditorView::layoutLine(qsizetype line) const
{
    const bool composing{!m_preedit.isEmpty() && line == m_buffer.lineAt(m_cursor)};
    return layoutText(displayText(line), composing ? m_cursor - m_buffer.lineStart(line) : -1);
}

std::unique_ptr<QTextLayout>
EditorView::layoutText(const QString &text, qsizetype preeditColumn, const QList<QTextLayout::FormatRange> &formats)
    const
{
    auto layout{std::make_unique<QTextLayout>(text, m_font)};
    QTextOption option;
    option.setWrapMode(QTextOption::NoWrap);
    option.setTabStopDistance(4 * m_charWidth);
    layout->setTextOption(option);
    // The input method's composition, shown in the text, underlined.
    if (preeditColumn >= 0) {
        QTextCharFormat underline;
        underline.setFontUnderline(true);
        layout->setFormats(
            {{.start = static_cast<int>(preeditColumn),
              .length = static_cast<int>(m_preedit.size()),
              .format = underline}}
        );
    } else if (!formats.isEmpty()) {
        layout->setFormats(formats);
    }
    layout->setCacheEnabled(true);
    layout->beginLayout();
    QTextLine textLine{layout->createLine()};
    if (textLine.isValid()) {
        textLine.setLineWidth(noWrapWidth);
        textLine.setPosition({0.0, 0.0});
    }
    layout->endLayout();
    return layout;
}

std::unique_ptr<QTextLayout> EditorView::layoutNumber(qsizetype line) const
{
    return layoutText(QString::number(line + 1), -1);
}

qreal EditorView::xForPosition(qsizetype position) const
{
    const qsizetype line{m_buffer.lineAt(position)};
    qsizetype column{position - m_buffer.lineStart(line)};
    if (!m_preedit.isEmpty() && position == m_cursor) {
        column += m_preeditCursor;
    }
    const auto row{std::ranges::find_if(m_rows, [line](const Row &candidate) { return candidate.line == line; })};
    if (row != m_rows.end()) {
        return row->layout->lineAt(0).cursorToX(static_cast<int>(column));
    }
    return layoutLine(line)->lineAt(0).cursorToX(static_cast<int>(column));
}

qsizetype EditorView::positionAt(const QPointF &point) const
{
    const auto line{static_cast<qsizetype>(std::floor((point.y() + m_contentY - topPadding) / m_lineHeight))};
    if (line < 0) {
        return 0;
    }
    if (line >= m_buffer.lineCount()) {
        return m_buffer.length();
    }
    const qreal x{point.x() + m_contentX - textLeft()};
    const std::unique_ptr<QTextLayout> layout{layoutLine(line)};
    return m_buffer.lineStart(line) + layout->lineAt(0).xToCursor(x, QTextLine::CursorBetweenCharacters);
}

EditorView::Row EditorView::takeRow(qsizetype line, qsizetype cursorLine)
{
    // A row already laid out with the same text, variant and starting state
    // (wherever its line was), else a new one.
    const bool composing{!m_preedit.isEmpty() && line == cursorLine};
    const bool highlighted{!composing && m_highlighter->isActive() && m_highlighter->isReady(line)};
    const quint64 variant{composing ? m_preeditGeneration : (highlighted ? Highlighted : Plain)};
    KSyntaxHighlighting::State state{highlighted ? m_highlighter->startState(line) : KSyntaxHighlighting::State{}};
    QString text{displayText(line)};
    const auto reusable{std::ranges::find_if(m_rows, [&text, &state, variant](const Row &row) {
        return row.layout != nullptr && row.variant == variant && row.text == text && row.state == state;
    })};
    if (reusable != m_rows.end()) {
        Row row{std::move(*reusable)};
        row.line = line;
        return row;
    }
    Row row{.id = m_nextRowId++, .line = line, .variant = variant, .text = {}, .layout = {}, .state = std::move(state)};
    row.layout = layoutText(
        text,
        composing ? m_cursor - m_buffer.lineStart(line) : -1,
        highlighted ? m_highlighter->formats(line, text) : QList<QTextLayout::FormatRange>{}
    );
    row.width = row.layout->lineAt(0).naturalTextWidth();
    row.text = std::move(text);
    m_widestLine = std::max(m_widestLine, row.width);
    return row;
}

EditorView::Number EditorView::takeNumber(qsizetype line)
{
    const auto number{std::ranges::find_if(m_numbers, [line](const Number &candidate) {
        return candidate.line == line && candidate.layout != nullptr;
    })};
    if (number != m_numbers.end()) {
        return std::move(*number);
    }
    Number laidOut{.line = line, .layout = layoutNumber(line), .width = 0.0};
    laidOut.width = laidOut.layout->lineAt(0).naturalTextWidth();
    return laidOut;
}

void EditorView::layoutRows()
{
    // Lay out the lines in view, reusing the rows (and numbers) already laid
    // out: a row is found again by its text, wherever its line moved.
    const qsizetype first{firstVisibleLine()};
    const qsizetype count{std::min(visibleLineCount(), m_buffer.lineCount() - first)};
    const qsizetype cursorLine{m_buffer.lineAt(m_cursor)};
    // Highlight up to the view, unless that takes long: the rest comes later.
    if (!m_highlighter->advance(first + count, QDeadlineTimer{highlightInLayout}) ||
        !m_highlighter->isReady(std::min(first + count + highlightLookahead, m_buffer.lineCount()))) {
        m_highlightTimer.start();
    }
    std::vector<Row> rows;
    std::vector<Number> numbers;
    rows.reserve(static_cast<std::size_t>(count));
    numbers.reserve(static_cast<std::size_t>(count));
    for (qsizetype line{first}; line < first + count; ++line) {
        rows.push_back(takeRow(line, cursorLine));
        numbers.push_back(takeNumber(line));
    }
    m_rows = std::move(rows);
    m_numbers = std::move(numbers);
    m_baseLine = first;
}

void EditorView::highlightAhead()
{
    const qsizetype last{lastVisibleLine()};
    const bool wasReady{m_highlighter->isReady(last)};
    const bool done{m_highlighter->advance(
        std::min(last + highlightLookahead, m_buffer.lineCount()),
        QDeadlineTimer{highlightSlice}
    )};
    if (!wasReady && m_highlighter->isReady(last)) {
        polish();
    }
    if (!done) {
        m_highlightTimer.start();
    }
}

void EditorView::layoutDecorations()
{
    // The selection, one rectangle per line in view, past the text when the
    // line's end is selected too; and the caret.
    m_selectionRects.clear();
    m_caretRect = {};
    // While text is dragged over the view, the caret shows where it would go.
    const qsizetype caret{m_dropPosition >= 0 ? m_dropPosition : m_cursor};
    const qsizetype cursorLine{m_buffer.lineAt(caret)};
    for (const Row &row : m_rows) {
        const qreal y{static_cast<qreal>(row.line - m_baseLine) * m_lineHeight};
        const qsizetype lineStart{m_buffer.lineStart(row.line)};
        const qsizetype lineEnd{lineStart + m_buffer.lineLength(row.line)};
        const QTextLine textLine{row.layout->lineAt(0)};
        if (row.line == cursorLine) {
            qsizetype column{caret - lineStart};
            if (!m_preedit.isEmpty() && m_dropPosition < 0) {
                column += m_preeditCursor;
            }
            m_caretRect = {textLine.cursorToX(static_cast<int>(column)), y, caretWidth, m_lineHeight};
        }
        if (!hasSelection() || selectionEnd() <= lineStart || selectionStart() > lineEnd) {
            continue;
        }
        const qreal from{textLine.cursorToX(static_cast<int>(std::max(selectionStart(), lineStart) - lineStart))};
        qreal to{textLine.cursorToX(static_cast<int>(std::min(selectionEnd(), lineEnd) - lineStart))};
        if (selectionEnd() > lineEnd) {
            to += m_charWidth / 2.0;
        }
        if (to > from) {
            m_selectionRects.append({from, y, to - from, m_lineHeight});
        }
    }
}

QRectF EditorView::rangeRect(const Row &row, qsizetype start, qsizetype end) const
{
    const QTextLine textLine{row.layout->lineAt(0)};
    const qreal from{textLine.cursorToX(static_cast<int>(start))};
    const qreal to{textLine.cursorToX(static_cast<int>(end))};
    return {from, static_cast<qreal>(row.line - m_baseLine) * m_lineHeight, to - from, m_lineHeight};
}

void EditorView::layoutMarks()
{
    // The bracket at the caret and its pair, and the word at the caret with
    // its other occurrences, in the rows laid out.
    m_bracketRects.clear();
    m_wordRects.clear();
    if (hasSelection() || !m_preedit.isEmpty() || m_dropPosition >= 0) {
        return;
    }
    const auto rowAt{[this](qsizetype line) {
        return std::ranges::find_if(m_rows, [line](const Row &row) { return row.line == line; });
    }};
    if (const std::optional<matching::BracketPair> pair{
            matching::matchingBracket(m_buffer, m_cursor, bracketSearchLines)
        }) {
        for (const qsizetype position : {pair->bracket, pair->match}) {
            const qsizetype line{m_buffer.lineAt(position)};
            const auto row{rowAt(line)};
            if (row != m_rows.end()) {
                const qsizetype column{position - m_buffer.lineStart(line)};
                m_bracketRects.append(rangeRect(*row, column, column + 1));
            }
        }
    }
    const auto cursorRow{rowAt(m_buffer.lineAt(m_cursor))};
    if (cursorRow == m_rows.end()) {
        return;
    }
    const matching::Range word{matching::wordAt(cursorRow->text, m_cursor - m_buffer.lineStart(cursorRow->line))};
    if (word.end == word.start) {
        return;
    }
    const QStringView wordText{QStringView{cursorRow->text}.sliced(word.start, word.end - word.start)};
    for (const Row &row : m_rows) {
        for (const matching::Range &found : matching::wholeWords(row.text, wordText)) {
            m_wordRects.append(rangeRect(row, found.start, found.end));
        }
    }
}

void EditorView::updatePolish()
{
    const qreal widestBefore{m_widestLine};
    layoutRows();
    layoutDecorations();
    layoutMarks();
    if (m_widestLine > widestBefore) {
        emit contentSizeChanged();
    }
    update();
}

QSGNode *EditorView::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData * /*data*/)
{
    QQuickWindow *const window{this->window()};
    auto *node{static_cast<EditorNode *>(oldNode)};
    if (node == nullptr) {
        auto created{std::make_unique<EditorNode>()};
        auto currentLine{std::unique_ptr<QSGRectangleNode>{window->createRectangleNode()}};
        auto clip{makeClip()};
        auto content{std::make_unique<QSGTransformNode>()};
        auto marksLayer{std::make_unique<QSGNode>()};
        auto selectionLayer{std::make_unique<QSGNode>()};
        auto textLayer{std::make_unique<QSGNode>()};
        auto caret{std::unique_ptr<QSGRectangleNode>{window->createRectangleNode()}};
        auto gutter{std::make_unique<QSGTransformNode>()};
        created->currentLine = currentLine.get();
        created->textClip = clip.get();
        created->content = content.get();
        created->marksLayer = marksLayer.get();
        created->selectionLayer = selectionLayer.get();
        created->textLayer = textLayer.get();
        created->caret = caret.get();
        created->gutter = gutter.get();
        content->appendChildNode(marksLayer.release());
        content->appendChildNode(selectionLayer.release());
        content->appendChildNode(textLayer.release());
        content->appendChildNode(caret.release());
        clip->appendChildNode(content.release());
        created->appendChildNode(currentLine.release());
        created->appendChildNode(clip.release());
        created->appendChildNode(gutter.release());
        node = created.release();
    }

    // Scrolling moves the two transforms; the nodes inside move only when
    // their line does, and are filled only when they first show.
    const qreal gutter{gutterWidth()};
    const qreal baseY{topPadding + (static_cast<qreal>(m_baseLine) * m_lineHeight) - m_contentY};
    setClipRect(node->textClip, {gutter, 0.0, std::max(0.0, width() - gutter), height()});
    setTranslation(node->content, {textLeft() - m_contentX, baseY});
    setTranslation(node->gutter, {0.0, baseY});

    const QColor text{textColor()};
    QHash<quint64, Shown> rows;
    for (const Row &row : m_rows) {
        const qreal y{static_cast<qreal>(row.line - m_baseLine) * m_lineHeight};
        placeText(window, node->textLayer, node->rows, rows, row.id, row.layout.get(), text, {0.0, y});
    }
    removeText(node->textLayer, node->rows);
    node->rows = std::move(rows);

    const qsizetype cursorLine{m_buffer.lineAt(m_cursor)};
    const QColor numberColor{lineNumberColor()};
    QHash<qsizetype, Shown> numbers;
    for (const Number &number : m_numbers) {
        const qreal y{static_cast<qreal>(number.line - m_baseLine) * m_lineHeight};
        const QColor color{number.line == cursorLine ? text : numberColor};
        placeText(
            window,
            node->gutter,
            node->numbers,
            numbers,
            number.line,
            number.layout.get(),
            color,
            {gutter - padding - number.width, y}
        );
    }
    removeText(node->gutter, node->numbers);
    node->numbers = std::move(numbers);

    syncRectangles(window, node->marksLayer, node->words, m_wordRects, wordColor());
    syncRectangles(window, node->marksLayer, node->brackets, m_bracketRects, bracketColor());
    syncRectangles(window, node->selectionLayer, node->selection, m_selectionRects, selectionColor());

    const bool caretShown{m_caretRect.isValid()};
    const bool dropping{m_dropPosition >= 0};
    node->caret->setRect(
        caretShown && (dropping || (hasActiveFocus() && m_preedit.isEmpty())) ? m_caretRect : QRectF{}
    );
    node->caret->setColor(caretColor());
    node->currentLine->setRect(
        caretShown && !hasSelection() ? QRectF{0.0, baseY + m_caretRect.y(), width(), m_lineHeight} : QRectF{}
    );
    node->currentLine->setColor(currentLineColor());
    return node;
}

void EditorView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        polish();
    }
}

// --- input -------------------------------------------------------------------

bool EditorView::handleNavigation(const QKeyEvent *event)
{
    // On macOS, ControlModifier is ⌘, AltModifier ⌥ and MetaModifier ⌃.
    const Qt::KeyboardModifiers modifiers{event->modifiers()};
    const bool command{modifiers.testFlag(Qt::ControlModifier)};
    const bool option{modifiers.testFlag(Qt::AltModifier)};
    const Select select{modifiers.testFlag(Qt::ShiftModifier) ? Select::Extend : Select::Collapse};
    const qsizetype line{m_buffer.lineAt(m_cursor)};
    const qsizetype lineStart{m_buffer.lineStart(line)};
    const qsizetype lineEnd{lineStart + m_buffer.lineLength(line)};
    const qsizetype page{std::max(qsizetype{1}, static_cast<qsizetype>(height() / m_lineHeight) - 1)};
    const bool collapsing{hasSelection() && select == Select::Collapse};

    switch (event->key()) {
    case Qt::Key_Left:
        if (command) {
            moveCaret(lineStart, select);
        } else if (option) {
            moveCaret(wordBoundary(m_cursor, false), select);
        } else {
            moveCaret(collapsing ? selectionStart() : m_cursor - 1, select);
        }
        return true;
    case Qt::Key_Right:
        if (command) {
            moveCaret(lineEnd, select);
        } else if (option) {
            moveCaret(wordBoundary(m_cursor, true), select);
        } else {
            moveCaret(collapsing ? selectionEnd() : m_cursor + 1, select);
        }
        return true;
    case Qt::Key_Up:
        if (command) {
            moveCaret(0, select);
        } else {
            moveVertically(-1, select);
        }
        return true;
    case Qt::Key_Down:
        if (command) {
            moveCaret(m_buffer.length(), select);
        } else {
            moveVertically(1, select);
        }
        return true;
    case Qt::Key_Home:
        moveCaret(lineStart, select);
        return true;
    case Qt::Key_End:
        moveCaret(lineEnd, select);
        return true;
    case Qt::Key_PageUp:
        moveVertically(-page, select);
        return true;
    case Qt::Key_PageDown:
        moveVertically(page, select);
        return true;
    default:
        return false;
    }
}

bool EditorView::handleDeletion(const QKeyEvent *event)
{
    const bool backspace{event->key() == Qt::Key_Backspace};
    if (!backspace && event->key() != Qt::Key_Delete) {
        return false;
    }
    if (hasSelection()) {
        deleteSelection();
        return true;
    }
    const Qt::KeyboardModifiers modifiers{event->modifiers()};
    const bool command{modifiers.testFlag(Qt::ControlModifier)};
    const bool option{modifiers.testFlag(Qt::AltModifier)};
    const qsizetype line{m_buffer.lineAt(m_cursor)};
    const qsizetype lineStart{m_buffer.lineStart(line)};
    // ⌘ to the line's start or end, ⌥ to the word's, otherwise one character.
    qsizetype target{std::clamp(m_cursor + (backspace ? -1 : 1), qsizetype{0}, m_buffer.length())};
    if (command) {
        target = backspace ? lineStart : lineStart + m_buffer.lineLength(line);
    } else if (option) {
        target = wordBoundary(m_cursor, !backspace);
    }
    const qsizetype from{std::min(m_cursor, target)};
    const qsizetype to{std::max(m_cursor, target)};
    if (from != to) {
        replace(from, to - from, {}, !command && !option);
    }
    return true;
}

void EditorView::keyPressEvent(QKeyEvent *event)
{
    if (handleNavigation(event) || handleDeletion(event)) {
        return;
    }
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        insert(QStringLiteral("\n"));
        return;
    case Qt::Key_Tab:
        insert(QStringLiteral("\t"));
        return;
    default:
        break;
    }
    const QString text{event->text()};
    const Qt::KeyboardModifiers modifiers{event->modifiers()};
    const bool printable{!text.isEmpty() && text.front().isPrint()};
    if (printable && !modifiers.testFlag(Qt::ControlModifier) && !modifiers.testFlag(Qt::MetaModifier)) {
        insert(text);
        return;
    }
    event->ignore();
}

void EditorView::inputMethodEvent(QInputMethodEvent *event)
{
    // A replacement (e.g. an accent chosen by press and hold) is relative to
    // the caret; otherwise the commit replaces the selection.
    const QString commit{event->commitString()};
    if (event->replacementLength() > 0) {
        const qsizetype from{std::max(qsizetype{0}, m_cursor + event->replacementStart())};
        replace(from, std::min<qsizetype>(event->replacementLength(), m_buffer.length() - from), commit, false);
    } else if (!commit.isEmpty()) {
        replace(selectionStart(), selectionEnd() - selectionStart(), commit, !hasSelection() && commit.size() == 1);
    }

    const QString preedit{event->preeditString()};
    qsizetype preeditCursor{preedit.size()};
    for (const QInputMethodEvent::Attribute &attribute : event->attributes()) {
        if (attribute.type == QInputMethodEvent::Cursor) {
            preeditCursor = attribute.start;
        }
    }
    if (preedit != m_preedit || preeditCursor != m_preeditCursor) {
        m_preedit = preedit;
        m_preeditCursor = preeditCursor;
        ++m_preeditGeneration;
        emit cursorChanged();
        polish();
    }
    event->accept();
}

QVariant EditorView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    const qsizetype line{m_buffer.lineAt(m_cursor)};
    const qsizetype lineStart{m_buffer.lineStart(line)};
    switch (query) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImHints:
        return static_cast<qint32>(Qt::ImhMultiLine | Qt::ImhNoAutoUppercase);
    case Qt::ImFont:
        return m_font;
    case Qt::ImCursorRectangle:
        return cursorRectangle().translated(-m_contentX, -m_contentY);
    case Qt::ImAnchorRectangle:
        return QRectF{
            textLeft() + xForPosition(m_anchor) - m_contentX,
            topPadding + (static_cast<qreal>(m_buffer.lineAt(m_anchor)) * m_lineHeight) - m_contentY,
            caretWidth,
            m_lineHeight
        };
    case Qt::ImSurroundingText:
        return m_buffer.line(line);
    case Qt::ImCursorPosition:
        return static_cast<qint32>(m_cursor - lineStart);
    case Qt::ImAnchorPosition:
        return static_cast<qint32>(m_buffer.lineAt(m_anchor) == line ? m_anchor - lineStart : m_cursor - lineStart);
    case Qt::ImAbsolutePosition:
        return static_cast<qint64>(m_cursor);
    case Qt::ImCurrentSelection:
        return selectedText();
    case Qt::ImTextBeforeCursor:
        return m_buffer.text(lineStart, m_cursor - lineStart);
    case Qt::ImTextAfterCursor:
        return m_buffer.text(m_cursor, lineStart + m_buffer.lineLength(line) - m_cursor);
    default:
        return QQuickItem::inputMethodQuery(query);
    }
}

void EditorView::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    const qsizetype position{positionAt(event->position())};
    // A press right after a double click: the third click selects the line.
    const qint32 interval{QGuiApplication::styleHints()->mouseDoubleClickInterval()};
    if (m_lastDoubleClick.isValid() && m_lastDoubleClick.elapsed() < interval) {
        m_lastDoubleClick.invalidate();
        const qsizetype line{m_buffer.lineAt(position)};
        const qsizetype next{line + 1 < m_buffer.lineCount() ? m_buffer.lineStart(line + 1) : m_buffer.length()};
        select(m_buffer.lineStart(line), next);
        event->accept();
        return;
    }
    const bool extend{event->modifiers().testFlag(Qt::ShiftModifier)};
    // A press on the selection may start dragging it: decide on move or release.
    if (!extend && hasSelection() && position >= selectionStart() && position < selectionEnd()) {
        m_flags |= DragPending;
        m_pressPoint = event->position();
        event->accept();
        return;
    }
    moveCaret(position, extend ? Select::Extend : Select::Collapse);
    event->accept();
}

void EditorView::mouseMoveEvent(QMouseEvent *event)
{
    if ((m_flags & DragPending) != 0U) {
        const qreal distance{(event->position() - m_pressPoint).manhattanLength()};
        if (distance >= QGuiApplication::styleHints()->startDragDistance()) {
            m_flags &= ~DragPending;
            startDrag();
        }
        event->accept();
        return;
    }
    moveCaret(positionAt(event->position()), Select::Extend);
    event->accept();
}

void EditorView::mouseReleaseEvent(QMouseEvent *event)
{
    // A click on the selection that did not become a drag places the caret.
    if ((m_flags & DragPending) != 0U) {
        m_flags &= ~DragPending;
        moveCaret(positionAt(event->position()), Select::Collapse);
    }
    event->accept();
}

void EditorView::startDrag()
{
    // Owned here; QDrag::exec returns once the drop is done.
    const auto drag{std::make_unique<QDrag>(this)};
    auto mimeData{std::make_unique<QMimeData>()};
    mimeData->setText(selectedText());
    drag->setMimeData(mimeData.release());
    m_flags &= ~DroppedHere;
    const Qt::DropAction action{drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::MoveAction)};
    // Moved to another app: the text leaves this view. Moved within it,
    // dropEvent() already did the move.
    if (action == Qt::MoveAction && (m_flags & DroppedHere) == 0U) {
        deleteSelection();
    }
    m_flags &= ~DroppedHere;
}

void EditorView::setDropPosition(qsizetype position)
{
    if (m_dropPosition == position) {
        return;
    }
    m_dropPosition = position;
    polish();
}

void EditorView::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasText()) {
        event->ignore();
        return;
    }
    setDropPosition(positionAt(event->position()));
    event->acceptProposedAction();
}

void EditorView::dragMoveEvent(QDragMoveEvent *event)
{
    const qsizetype position{positionAt(event->position())};
    // Not onto the dragged selection itself.
    const bool ontoItself{event->source() == this && position > selectionStart() && position < selectionEnd()};
    setDropPosition(ontoItself ? -1 : position);
    if (ontoItself) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void EditorView::dragLeaveEvent(QDragLeaveEvent *event)
{
    setDropPosition(-1);
    event->accept();
}

void EditorView::dropEvent(QDropEvent *event)
{
    qsizetype position{positionAt(event->position())};
    setDropPosition(-1);
    QString text{event->mimeData()->text()};
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    const bool fromHere{event->source() == this};
    const bool move{fromHere && event->dropAction() == Qt::MoveAction};
    if (move) {
        // Take the text out first; past it, the drop position shifts back.
        const qsizetype start{selectionStart()};
        const qsizetype length{selectionEnd() - start};
        if (position >= start && position <= start + length) {
            event->ignore();
            return;
        }
        replace(start, length, {}, false);
        if (position > start) {
            position -= length;
        }
        m_flags |= DroppedHere;
    }
    replace(position, 0, text, false);
    select(position, position + text.size());
    forceActiveFocus(Qt::OtherFocusReason);
    event->acceptProposedAction();
}

void EditorView::mouseDoubleClickEvent(QMouseEvent *event)
{
    const qsizetype position{positionAt(event->position())};
    const qsizetype start{wordBoundary(std::min(position + 1, m_buffer.length()), false)};
    const qsizetype end{wordBoundary(start, true)};
    select(start, std::max(end, position));
    m_lastDoubleClick.start();
    event->accept();
}

void EditorView::focusInEvent(QFocusEvent *event)
{
    QQuickItem::focusInEvent(event);
    update();
}

void EditorView::focusOutEvent(QFocusEvent *event)
{
    QQuickItem::focusOutEvent(event);
    update();
}

} // namespace tekst
