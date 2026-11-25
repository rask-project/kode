#include "EditorView.h"

#include "EditorAccessible.h"
#include "Indentation.h"
#include "Matching.h"
#include "SyntaxHighlighter.h"
#include "TextSearch.h"

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
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
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
// Find: a slice of the search.
constexpr std::chrono::milliseconds searchSlice{3};

using matching::isWordCharacter;

// Where `at` is after `removed` characters at `position` became `inserted`
// ones: inside the change, at its start.
qsizetype shifted(qsizetype at, qsizetype position, qsizetype removed, qsizetype inserted)
{
    if (at < position) {
        return at;
    }
    return at >= position + removed ? at - removed + inserted : position;
}

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
    QSGGeometryNode *invisibles{nullptr};
    QSGNode *selectionLayer{nullptr};
    QSGNode *textLayer{nullptr};
    QSGRectangleNode *caret{nullptr};
    QSGTransformNode *gutter{nullptr};
    QList<QSGRectangleNode *> selection;
    QList<QSGRectangleNode *> brackets;
    QList<QSGRectangleNode *> words;
    QList<QSGRectangleNode *> matches;
    QHash<quint64, Shown> rows;
    QHash<quint64, Shown> numbers;
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
// A geometry node drawing rectangles in one color: one draw call for many.
std::unique_ptr<QSGGeometryNode> makeRectanglesNode()
{
    auto node{std::make_unique<QSGGeometryNode>()};
    auto geometry{std::make_unique<QSGGeometry>(QSGGeometry::defaultAttributes_Point2D(), 0)};
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    node->setGeometry(geometry.release());
    node->setFlag(QSGNode::OwnsGeometry);
    auto material{std::make_unique<QSGFlatColorMaterial>()};
    node->setMaterial(material.release());
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

// Two triangles per rectangle.
void setRectangles(QSGGeometryNode *node, const QList<QRectF> &rects, const QColor &color)
{
    QSGGeometry *const geometry{node->geometry()};
    geometry->allocate(static_cast<int>(rects.size() * 6));
    QSGGeometry::Point2D *vertex{geometry->vertexDataAsPoint2D()};
    const std::span vertices{vertex, static_cast<std::size_t>(rects.size() * 6)};
    std::size_t index{0};
    for (const QRectF &rect : rects) {
        const auto left{static_cast<float>(rect.left())};
        const auto top{static_cast<float>(rect.top())};
        const auto right{static_cast<float>(rect.right())};
        const auto bottom{static_cast<float>(rect.bottom())};
        for (const auto &[x, y] :
             {std::pair{left, top}, {right, top}, {left, bottom}, {right, top}, {right, bottom}, {left, bottom}}) {
            vertices[index++].set(x, y);
        }
    }
    node->markDirty(QSGNode::DirtyGeometry);
    auto *const material{static_cast<QSGFlatColorMaterial *>(node->material())};
    if (material->color() != color) {
        material->setColor(color);
        node->markDirty(QSGNode::DirtyMaterial);
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
    , m_ownDocument{std::make_unique<TextDocument>()}
    , m_font(QStringLiteral("Menlo"), 13)
{
    attach(m_ownDocument.get());
    m_highlightTimer.setSingleShot(true);
    m_highlightTimer.setInterval(0);
    connect(&m_highlightTimer, &QTimer::timeout, this, &EditorView::highlightAhead);
    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(0);
    connect(&m_searchTimer, &QTimer::timeout, this, &EditorView::searchAhead);
    // The selected match follows the selection.
    connect(this, &EditorView::selectionChanged, this, &EditorView::searchChanged);
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
    const qsizetype anchor{topLine()};
    m_font = font;
    updateMetrics();
    m_widestLine = 0.0;
    m_rows.clear();
    m_numbers.clear();
    emit fontChanged();
    // Columns per row changed too.
    resetWrap(anchor);
}

void EditorView::setDocument(TextDocument *document)
{
    TextDocument *const shown{document != nullptr ? document : m_ownDocument.get()};
    if (m_document == shown) {
        return;
    }
    // Where this view was, for the next view of that document (unless it is
    // being deleted).
    if (m_document != nullptr) {
        m_document->setPlace(
            {.contentX = m_contentX, .contentY = m_contentY, .caret = {.cursor = m_cursor, .anchor = m_anchor}}
        );
    }
    attach(shown);
    startOver(shown->place());
    emit documentChanged();
    emit textChanged();
    emit undoStateChanged();
    emit modifiedChanged();
    emit syntaxChanged();
    emit indentationChanged();
    emit defaultIndentationChanged();
}

void EditorView::attach(TextDocument *document)
{
    if (m_document != nullptr) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_document = document;
    connect(document, &TextDocument::replaced, this, &EditorView::documentReplaced);
    connect(document, &TextDocument::reset, this, &EditorView::documentReset);
    connect(document, &TextDocument::modifiedChanged, this, &EditorView::modifiedChanged);
    connect(document, &TextDocument::undoStateChanged, this, &EditorView::undoStateChanged);
    connect(document, &TextDocument::syntaxChanged, this, &EditorView::syntaxChanged);
    connect(document, &TextDocument::highlightingChanged, this, &EditorView::documentHighlightingChanged);
    connect(document, &TextDocument::indentationChanged, this, &EditorView::documentIndentationChanged);
    connect(document, &TextDocument::defaultIndentationChanged, this, &EditorView::defaultIndentationChanged);
    // A document deleted under the view (its file closed): back to its own.
    if (document != m_ownDocument.get()) {
        connect(document, &QObject::destroyed, this, [this] { setDocument(nullptr); });
    }
    if (!m_syntaxTheme.isEmpty()) {
        document->setTheme(m_syntaxTheme);
    }
    // The search goes on, over the new text.
    auto search{std::make_unique<TextSearch>(&document->buffer())};
    if (m_search != nullptr) {
        search->setQuery(m_search->pattern(), m_search->caseSensitive(), m_search->regularExpression());
    }
    m_search = std::move(search);
}

void EditorView::startOver(const TextDocument::Place &place)
{
    if (wrap()) {
        m_wrapIndex.reset(buffer(), static_cast<qsizetype>(std::floor(m_wrapWidth / m_charWidth)));
    }
    m_cursor = std::clamp(place.caret.cursor, qsizetype{0}, buffer().length());
    m_anchor = std::clamp(place.caret.anchor, qsizetype{0}, buffer().length());
    m_preedit.clear();
    m_preferredX = -1.0;
    m_widestLine = 0.0;
    m_rows.clear();
    m_numbers.clear();
    m_contentX = place.contentX;
    m_contentY = place.contentY;
    emit contentXChanged();
    emit contentYChanged();
    emit selectionChanged();
    if (m_search->isActive()) {
        m_search->restart();
        searchAhead();
    }
    emit textChanged();
    emit contentSizeChanged();
    emit cursorChanged();
    polish();
}

void EditorView::documentReset(bool keepPlace)
{
    if (keepPlace) {
        startOver({.contentX = m_contentX, .contentY = m_contentY, .caret = {.cursor = m_cursor, .anchor = m_cursor}});
    } else {
        startOver({});
    }
    emit indentationChanged();
    emit caretMoved();
}

void EditorView::documentReplaced(qsizetype position, const QString &removed, const QString &inserted)
{
    if (wrap()) {
        m_wrapIndex.linesReplaced(
            buffer(),
            buffer().lineAt(position),
            removed.count(QLatin1Char('\n')),
            inserted.count(QLatin1Char('\n'))
        );
    }
    // Another view's edit: the caret stays on the same text.
    if ((m_flags & Editing) == 0U) {
        m_cursor = shifted(m_cursor, position, removed.size(), inserted.size());
        m_anchor = shifted(m_anchor, position, removed.size(), inserted.size());
        m_preedit.clear();
    }
    notifyTextChanged(position, removed, inserted);
    if (m_search->isActive()) {
        m_search->restart();
        searchAhead();
    }
    emit textChanged();
    emit contentSizeChanged();
    emit cursorChanged();
    emit selectionChanged();
    polish();
}

void EditorView::documentIndentationChanged()
{
    // Tab stops move.
    m_rows.clear();
    m_widestLine = 0.0;
    emit indentationChanged();
    emit contentSizeChanged();
    emit cursorChanged();
    polish();
}

void EditorView::documentHighlightingChanged()
{
    m_rows.clear();
    polish();
}

void EditorView::setSyntaxTheme(const QString &name)
{
    if (m_syntaxTheme == name) {
        return;
    }
    m_syntaxTheme = name;
    m_document->setTheme(name);
    emit syntaxThemeChanged();
}

void EditorView::setColor(QRgb &field, const QColor &color)
{
    if (field == color.rgba()) {
        return;
    }
    field = color.rgba();
    // The invisibles take their color with their geometry.
    m_flags |= InvisiblesChanged;
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

void EditorView::setMatchColor(const QColor &color)
{
    setColor(m_colors.match, color);
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
    const auto digits{std::max(3, static_cast<qint32>(std::log10(static_cast<qreal>(buffer().lineCount()))) + 1)};
    return (static_cast<qreal>(digits) * m_charWidth) + (2.0 * padding);
}

qreal EditorView::textLeft() const
{
    return gutterWidth() + padding;
}

qsizetype EditorView::rowOfLine(qsizetype line) const
{
    return wrap() ? m_wrapIndex.rowOf(line) : line;
}

qsizetype EditorView::lineAtRow(qsizetype row) const
{
    return wrap() ? m_wrapIndex.lineAt(row) : std::clamp(row, qsizetype{0}, buffer().lineCount() - 1);
}

qsizetype EditorView::totalRows() const
{
    return wrap() ? m_wrapIndex.totalRows() : buffer().lineCount();
}

qreal EditorView::wrapWidth() const
{
    constexpr qreal fewestColumns{20.0};
    return std::max(fewestColumns * m_charWidth, width() - textLeft() - padding);
}

qsizetype EditorView::topLine() const
{
    const auto row{static_cast<qsizetype>(std::floor((m_contentY - topPadding) / m_lineHeight))};
    return lineAtRow(std::max(row, qsizetype{0}));
}

void EditorView::resetWrap(qsizetype anchorLine)
{
    m_wrapWidth = wrapWidth();
    m_rows.clear();
    if (wrap()) {
        m_wrapIndex.reset(buffer(), static_cast<qsizetype>(std::floor(m_wrapWidth / m_charWidth)));
    }
    emit contentSizeChanged();
    emit cursorChanged();
    if (anchorLine > 0) {
        emit scrollRequested(topPadding + (static_cast<qreal>(rowOfLine(anchorLine)) * m_lineHeight));
    }
    polish();
}

void EditorView::setWrap(bool wrap)
{
    if (this->wrap() == wrap) {
        return;
    }
    const qsizetype anchor{topLine()};
    m_flags = wrap ? (m_flags | Wrap) : (m_flags & ~Wrap);
    emit layoutOptionsChanged();
    resetWrap(anchor);
}

void EditorView::setShowInvisibles(bool show)
{
    if (showInvisibles() == show) {
        return;
    }
    m_flags = show ? (m_flags | ShowInvisibles) : (m_flags & ~ShowInvisibles);
    m_rows.clear();
    emit layoutOptionsChanged();
    polish();
}

const QTextLayout *EditorView::laidOut(qsizetype line) const
{
    const auto row{std::ranges::find_if(m_rows, [line](const Row &candidate) { return candidate.line == line; })};
    return row != m_rows.end() ? row->layout.get() : nullptr;
}

QRectF EditorView::positionRect(qsizetype position) const
{
    const qsizetype line{buffer().lineAt(position)};
    qsizetype column{position - buffer().lineStart(line)};
    if (!m_preedit.isEmpty() && position == m_cursor) {
        column += m_preeditCursor;
    }
    std::unique_ptr<QTextLayout> owned;
    const QTextLayout *layout{laidOut(line)};
    if (layout == nullptr) {
        owned = layoutLine(line);
        layout = owned.get();
    }
    const QTextLine textLine{layout->lineForTextPosition(static_cast<int>(column))};
    const qsizetype row{rowOfLine(line) + (textLine.isValid() ? textLine.lineNumber() : 0)};
    const qreal x{textLine.isValid() ? textLine.cursorToX(static_cast<int>(column)) : 0.0};
    return {textLeft() + x, topPadding + (static_cast<qreal>(row) * m_lineHeight), caretWidth, m_lineHeight};
}

qsizetype EditorView::positionAtRow(qsizetype row, qreal x) const
{
    row = std::clamp(row, qsizetype{0}, totalRows() - 1);
    const qsizetype line{lineAtRow(row)};
    std::unique_ptr<QTextLayout> owned;
    const QTextLayout *layout{laidOut(line)};
    if (layout == nullptr) {
        owned = layoutLine(line);
        layout = owned.get();
    }
    const qsizetype visualLine{std::clamp(row - rowOfLine(line), qsizetype{0}, qsizetype{layout->lineCount()} - 1)};
    const QTextLine textLine{layout->lineAt(static_cast<int>(visualLine))};
    qsizetype column{textLine.xToCursor(x, QTextLine::CursorBetweenCharacters)};
    // The end of a wrapped row is the start of the next one: stay on this one.
    if (visualLine + 1 < layout->lineCount() && column >= textLine.textStart() + textLine.textLength()) {
        column = textLine.textStart() + textLine.textLength() - 1;
    }
    return buffer().lineStart(line) + column;
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
    return wrap() ? width() : textLeft() + m_widestLine + m_charWidth + padding;
}

qreal EditorView::contentHeight() const
{
    return topPadding + (static_cast<qreal>(totalRows()) * m_lineHeight) + padding;
}

void EditorView::setCursorPosition(qint64 position)
{
    moveCaret(position, Select::Collapse);
}

qint32 EditorView::cursorLine() const
{
    return static_cast<qint32>(buffer().lineAt(m_cursor));
}

qint32 EditorView::cursorColumn() const
{
    return static_cast<qint32>(m_cursor - buffer().lineStart(buffer().lineAt(m_cursor)));
}

QRectF EditorView::cursorRectangle() const
{
    return positionRect(m_cursor);
}

bool EditorView::canPaste()
{
    return !QGuiApplication::clipboard()->text().isEmpty();
}

// --- text and edits ---------------------------------------------------------

QString EditorView::selectedText() const
{
    return buffer().text(selectionStart(), selectionEnd() - selectionStart());
}

void EditorView::replace(qsizetype position, qsizetype count, const QString &text, bool mergeable)
{
    m_flags |= Editing;
    m_document->replace(position, count, text, mergeable, {.cursor = m_cursor, .anchor = m_anchor});
    m_flags &= ~Editing;
    editedTo({.cursor = position + text.size(), .anchor = position + text.size()});
}

void EditorView::editedTo(TextDocument::Caret caret)
{
    m_cursor = caret.cursor;
    m_anchor = caret.anchor;
    m_preferredX = -1.0;
    emit cursorChanged();
    emit caretMoved();
    emit selectionChanged();
    notifyCursorChanged();
    polish();
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

void EditorView::undo()
{
    m_flags |= Editing;
    const std::optional<TextDocument::Caret> caret{m_document->undo()};
    m_flags &= ~Editing;
    if (caret) {
        editedTo(*caret);
    }
}

void EditorView::redo()
{
    m_flags |= Editing;
    const std::optional<TextDocument::Caret> caret{m_document->redo()};
    m_flags &= ~Editing;
    if (caret) {
        editedTo(*caret);
    }
}

void EditorView::select(qint64 start, qint64 end)
{
    start = std::clamp(start, qint64{0}, buffer().length());
    end = std::clamp(end, qint64{0}, buffer().length());
    m_anchor = start;
    moveCaret(end, Select::Extend);
    // Even when the caret did not move, the anchor may have.
    emit selectionChanged();
    polish();
}

void EditorView::selectAll()
{
    select(0, buffer().length());
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

// --- caret and selection -------------------------------------------------------

void EditorView::moveCaret(qsizetype position, Select select)
{
    position = std::clamp(position, qsizetype{0}, buffer().length());
    const qsizetype anchor{select == Select::Extend ? m_anchor : position};
    m_document->closeMerge();
    m_preferredX = -1.0;
    if (position == m_cursor && anchor == m_anchor) {
        return;
    }
    const bool selectionWasVisible{hasSelection()};
    m_cursor = position;
    m_anchor = anchor;
    emit cursorChanged();
    emit caretMoved();
    if (selectionWasVisible || hasSelection()) {
        emit selectionChanged();
    }
    notifyCursorChanged();
    polish();
}

void EditorView::moveVertically(qsizetype lines, Select select)
{
    // By visual rows, aiming for the same x.
    const QRectF caret{positionRect(m_cursor)};
    const qreal preferredX{m_preferredX >= 0.0 ? m_preferredX : caret.x() - textLeft()};
    const auto row{static_cast<qsizetype>(std::lround((caret.y() - topPadding) / m_lineHeight))};
    moveCaret(positionAtRow(row + lines, preferredX), select);
    m_preferredX = preferredX;
}

qsizetype EditorView::wordBoundary(qsizetype position, bool forward) const
{
    const qsizetype line{buffer().lineAt(position)};
    const qsizetype lineStart{buffer().lineStart(line)};
    const QString text{buffer().line(line)};
    qsizetype column{position - lineStart};
    if (forward) {
        if (column >= text.size()) {
            return std::min(position + 1, buffer().length());
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

// --- indentation --------------------------------------------------------------

indentation::Style EditorView::indentStyle() const
{
    return {
        .width = static_cast<quint8>(indentWidth()),
        .kind = indentWithTabs() ? indentation::Kind::Tabs : indentation::Kind::Spaces
    };
}

void EditorView::insertNewline()
{
    const qsizetype line{buffer().lineAt(selectionStart())};
    const qsizetype lineStart{buffer().lineStart(line)};
    const QString text{buffer().text(lineStart, selectionStart() - lineStart)};
    insert(QStringLiteral("\n") + indentation::leadingWhitespace(text));
}

void EditorView::insertTab()
{
    if (hasSelection() && buffer().lineAt(selectionStart()) != buffer().lineAt(selectionEnd())) {
        indent();
        return;
    }
    const qsizetype column{selectionStart() - buffer().lineStart(buffer().lineAt(selectionStart()))};
    insert(indentation::tabAt(indentStyle(), column));
}

void EditorView::indent()
{
    changeSelectedLines(false);
}

void EditorView::outdent()
{
    changeSelectedLines(true);
}

void EditorView::changeSelectedLines(bool outdenting)
{
    const qsizetype firstLine{buffer().lineAt(selectionStart())};
    qsizetype lastLine{buffer().lineAt(selectionEnd())};
    // A selection ending at the start of a line leaves that line out.
    if (hasSelection() && lastLine > firstLine && selectionEnd() == buffer().lineStart(lastLine)) {
        --lastLine;
    }
    const qsizetype start{buffer().lineStart(firstLine)};
    const qsizetype end{buffer().lineStart(lastLine) + buffer().lineLength(lastLine)};
    const QString before{buffer().text(start, end - start)};
    const indentation::Style style{
        .width = static_cast<quint8>(indentWidth()),
        .kind = indentWithTabs() ? indentation::Kind::Tabs : indentation::Kind::Spaces
    };
    const QString after{outdenting ? indentation::outdented(before, style) : indentation::indented(before, style)};
    if (after == before) {
        return;
    }
    const bool selected{hasSelection()};
    const qsizetype caretColumn{m_cursor - buffer().lineStart(buffer().lineAt(m_cursor))};
    replace(start, end - start, after, false);
    if (selected) {
        select(start, start + after.size());
        return;
    }
    // One line, no selection: the caret keeps its place in the text.
    const qsizetype delta{after.size() - before.size()};
    moveCaret(start + std::max(caretColumn + delta, qsizetype{0}), Select::Collapse);
}

bool EditorView::goToLine(const QString &target)
{
    const QStringList parts{target.trimmed().split(QLatin1Char(':'))};
    if (parts.size() > 2) {
        return false;
    }
    bool lineOk{false};
    bool columnOk{true};
    const qsizetype line{parts.first().toLongLong(&lineOk)};
    const qsizetype column{parts.size() == 2 ? parts.last().toLongLong(&columnOk) : 1};
    if (!lineOk || !columnOk) {
        return false;
    }
    const qsizetype clampedLine{std::clamp(line - 1, qsizetype{0}, buffer().lineCount() - 1)};
    const qsizetype clampedColumn{std::clamp(column - 1, qsizetype{0}, buffer().lineLength(clampedLine))};
    moveCaret(buffer().lineStart(clampedLine) + clampedColumn, Select::Collapse);
    return true;
}

// --- find ---------------------------------------------------------------------

QString EditorView::searchPattern() const
{
    return m_search->pattern();
}

void EditorView::setSearchPattern(const QString &pattern)
{
    setSearch(pattern, m_search->caseSensitive(), m_search->regularExpression());
}

bool EditorView::searchCaseSensitive() const
{
    return m_search->caseSensitive();
}

void EditorView::setSearchCaseSensitive(bool caseSensitive)
{
    setSearch(m_search->pattern(), caseSensitive, m_search->regularExpression());
}

bool EditorView::searchRegularExpression() const
{
    return m_search->regularExpression();
}

void EditorView::setSearchRegularExpression(bool regularExpression)
{
    setSearch(m_search->pattern(), m_search->caseSensitive(), regularExpression);
}

qint32 EditorView::searchMatchCount() const
{
    return static_cast<qint32>(m_search->matches().size());
}

qint32 EditorView::searchCurrent() const
{
    if (!hasSelection()) {
        return -1;
    }
    return static_cast<qint32>(m_search->indexOf(selectionStart(), selectionEnd() - selectionStart()));
}

bool EditorView::searchComplete() const
{
    return m_search->isDone() && !m_search->isCapped();
}

QString EditorView::searchError() const
{
    return m_search->error();
}

void EditorView::setSearch(const QString &pattern, bool caseSensitive, bool regularExpression)
{
    if (pattern == m_search->pattern() && caseSensitive == m_search->caseSensitive() &&
        regularExpression == m_search->regularExpression()) {
        return;
    }
    m_search->setQuery(pattern, caseSensitive, regularExpression);
    searchAhead();
}

void EditorView::searchAhead()
{
    if (!m_search->advance(QDeadlineTimer{searchSlice})) {
        m_searchTimer.start();
    }
    emit searchChanged();
    polish();
}

void EditorView::selectMatch(qsizetype index)
{
    const TextSearch::Match &match{m_search->matches().at(index)};
    select(match.start, match.start + match.length);
}

bool EditorView::findNext()
{
    if (!m_search->isActive()) {
        return false;
    }
    const qsizetype from{hasSelection() ? selectionStart() + 1 : m_cursor};
    qsizetype index{m_search->indexFrom(from)};
    if (index == m_search->matches().size() && !m_search->isDone()) {
        m_search->advance(QDeadlineTimer{QDeadlineTimer::Forever});
        emit searchChanged();
        index = m_search->indexFrom(from);
    }
    if (m_search->matches().isEmpty()) {
        return false;
    }
    selectMatch(index == m_search->matches().size() ? 0 : index);
    return true;
}

bool EditorView::findPrevious()
{
    if (!m_search->isActive()) {
        return false;
    }
    const qsizetype index{m_search->indexFrom(hasSelection() ? selectionStart() : m_cursor) - 1};
    if (index < 0 && !m_search->isDone()) {
        // Wrapping to the last match: find them all first.
        m_search->advance(QDeadlineTimer{QDeadlineTimer::Forever});
        emit searchChanged();
    }
    if (m_search->matches().isEmpty()) {
        return false;
    }
    selectMatch(index < 0 ? m_search->matches().size() - 1 : index);
    return true;
}

void EditorView::replaceCurrent(const QString &replacement)
{
    const qint32 current{searchCurrent()};
    if (current < 0) {
        findNext();
        return;
    }
    const TextSearch::Match match{m_search->matches().at(current)};
    replace(match.start, match.length, m_search->replacementFor(match, replacement), false);
    findNext();
}

qint32 EditorView::replaceAll(const QString &replacement)
{
    if (!m_search->isActive()) {
        return 0;
    }
    m_search->advance(QDeadlineTimer{QDeadlineTimer::Forever});
    const QList<TextSearch::Match> matches{m_search->matches()};
    if (matches.isEmpty()) {
        return 0;
    }
    // One edit over the span of the matches, so one undo.
    const qsizetype start{matches.front().start};
    const qsizetype end{matches.back().start + matches.back().length};
    QString text;
    qsizetype done{start};
    for (const TextSearch::Match &match : matches) {
        text.append(buffer().text(done, match.start - done));
        text.append(m_search->replacementFor(match, replacement));
        done = match.start + match.length;
    }
    replace(start, end - start, text, false);
    return static_cast<qint32>(matches.size());
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
    const auto row{static_cast<qsizetype>(std::floor((m_contentY - topPadding) / m_lineHeight))};
    return std::clamp(lineAtRow(std::max(row, qsizetype{0})) - overscan, qsizetype{0}, buffer().lineCount() - 1);
}

qsizetype EditorView::lastVisibleLine() const
{
    return std::min(firstVisibleLine() + visibleLineCount(), buffer().lineCount());
}

qsizetype EditorView::visibleLineCount() const
{
    return static_cast<qsizetype>(std::ceil(height() / m_lineHeight)) + (2 * overscan) + 1;
}

QString EditorView::displayText(qsizetype line) const
{
    QString text{buffer().line(line)};
    if (!m_preedit.isEmpty() && line == buffer().lineAt(m_cursor)) {
        text.insert(m_cursor - buffer().lineStart(line), m_preedit);
    }
    return text;
}

std::unique_ptr<QTextLayout> EditorView::layoutLine(qsizetype line) const
{
    const bool composing{!m_preedit.isEmpty() && line == buffer().lineAt(m_cursor)};
    return layoutText(displayText(line), composing ? m_cursor - buffer().lineStart(line) : -1);
}

std::unique_ptr<QTextLayout>
EditorView::layoutText(const QString &text, qsizetype preeditColumn, const QList<QTextLayout::FormatRange> &formats)
    const
{
    auto layout{std::make_unique<QTextLayout>(text, m_font)};
    QTextOption option;
    option.setWrapMode(wrap() ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    option.setTabStopDistance(static_cast<qreal>(indentWidth()) * m_charWidth);
    if (showInvisibles()) {
        option.setFlags(QTextOption::ShowTabsAndSpaces);
    }
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
    // Every visual line one line height below the last.
    const qreal lineWidth{wrap() ? m_wrapWidth : noWrapWidth};
    qreal y{0.0};
    for (QTextLine textLine{layout->createLine()}; textLine.isValid(); textLine = layout->createLine()) {
        textLine.setLineWidth(lineWidth);
        textLine.setPosition({0.0, y});
        y += m_lineHeight;
    }
    layout->endLayout();
    return layout;
}

std::unique_ptr<QTextLayout> EditorView::layoutNumber(qsizetype line) const
{
    return layoutText(QString::number(line + 1), -1);
}

qsizetype EditorView::positionAt(const QPointF &point) const
{
    const auto row{static_cast<qsizetype>(std::floor((point.y() + m_contentY - topPadding) / m_lineHeight))};
    if (row < 0) {
        return 0;
    }
    if (row >= totalRows()) {
        return buffer().length();
    }
    return positionAtRow(row, point.x() + m_contentX - textLeft());
}

EditorView::Row EditorView::takeRow(qsizetype line, qsizetype cursorLine)
{
    // A row already laid out with the same text, variant and starting state
    // (wherever its line was), else a new one.
    const bool composing{!m_preedit.isEmpty() && line == cursorLine};
    const bool highlighted{!composing && highlighter().isActive() && highlighter().isReady(line)};
    const quint64 variant{composing ? m_preeditGeneration : (highlighted ? Highlighted : Plain)};
    KSyntaxHighlighting::State state{highlighted ? highlighter().startState(line) : KSyntaxHighlighting::State{}};
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
        composing ? m_cursor - buffer().lineStart(line) : -1,
        highlighted ? highlighter().formats(line, text) : QList<QTextLayout::FormatRange>{}
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
    Number laidOut{.id = m_nextRowId++, .line = line, .layout = layoutNumber(line), .width = 0.0};
    laidOut.width = laidOut.layout->lineAt(0).naturalTextWidth();
    return laidOut;
}

void EditorView::layoutRows()
{
    // Lay out the lines in view, reusing the rows (and numbers) already laid
    // out: a row is found again by its text, wherever its line moved.
    if (wrap() && !qFuzzyCompare(m_wrapWidth, wrapWidth())) {
        resetWrap(topLine());
    }
    const qsizetype first{firstVisibleLine()};
    const qsizetype count{std::min(visibleLineCount(), buffer().lineCount() - first)};
    const qsizetype cursorLine{buffer().lineAt(m_cursor)};
    const qsizetype baseRow{rowOfLine(first)};
    // Down to this row (overscan included): wrapped lines take several.
    const qsizetype lastRow{
        static_cast<qsizetype>(std::floor((m_contentY + height() - topPadding) / m_lineHeight)) + overscan
    };
    // Highlight up to the view, unless that takes long: the rest comes later.
    if (!highlighter().advance(first + count, QDeadlineTimer{highlightInLayout}) ||
        !highlighter().isReady(std::min(first + count + highlightLookahead, buffer().lineCount()))) {
        m_highlightTimer.start();
    }
    std::vector<Row> rows;
    std::vector<Number> numbers;
    rows.reserve(static_cast<std::size_t>(count));
    numbers.reserve(static_cast<std::size_t>(count));
    qsizetype rowsAbove{0};
    for (qsizetype line{first}; line < buffer().lineCount() && baseRow + rowsAbove <= lastRow; ++line) {
        Row row{takeRow(line, cursorLine)};
        row.top = static_cast<qreal>(rowsAbove) * m_lineHeight;
        const qsizetype visualLines{row.layout->lineCount()};
        // Measured: the estimate (and the rows below it) corrected.
        if (wrap() && m_wrapIndex.rows(line) != visualLines) {
            m_wrapIndex.setRows(line, visualLines);
        }
        rowsAbove += visualLines;
        rows.push_back(std::move(row));
        numbers.push_back(takeNumber(line));
    }
    m_rows = std::move(rows);
    m_numbers = std::move(numbers);
    m_baseLine = first;
    m_baseTop = topPadding + (static_cast<qreal>(baseRow) * m_lineHeight);
}

void EditorView::highlightAhead()
{
    const qsizetype last{lastVisibleLine()};
    const bool wasReady{highlighter().isReady(last)};
    const bool done{
        highlighter().advance(std::min(last + highlightLookahead, buffer().lineCount()), QDeadlineTimer{highlightSlice})
    };
    if (!wasReady && highlighter().isReady(last)) {
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
    const qsizetype cursorLine{buffer().lineAt(caret)};
    for (const Row &row : m_rows) {
        const qsizetype lineStart{buffer().lineStart(row.line)};
        const qsizetype lineEnd{lineStart + buffer().lineLength(row.line)};
        if (row.line == cursorLine) {
            qsizetype column{caret - lineStart};
            if (!m_preedit.isEmpty() && m_dropPosition < 0) {
                column += m_preeditCursor;
            }
            const QTextLine textLine{row.layout->lineForTextPosition(static_cast<int>(column))};
            m_caretRect = {
                textLine.cursorToX(static_cast<int>(column)),
                row.top + (static_cast<qreal>(textLine.lineNumber()) * m_lineHeight),
                caretWidth,
                m_lineHeight
            };
        }
        if (hasSelection() && selectionEnd() > lineStart && selectionStart() <= lineEnd) {
            appendSelectionRects(row, lineStart, lineEnd);
        }
    }
}

void EditorView::appendSelectionRects(const Row &row, qsizetype lineStart, qsizetype lineEnd)
{
    // One rectangle per visual line, past the text when the line's end is
    // selected too.
    const qsizetype from{std::max(selectionStart(), lineStart) - lineStart};
    const qsizetype to{std::min(selectionEnd(), lineEnd) - lineStart};
    const bool pastEnd{selectionEnd() > lineEnd};
    for (qint32 index{0}; index < row.layout->lineCount(); ++index) {
        const QTextLine textLine{row.layout->lineAt(index)};
        const qsizetype start{std::max(from, qsizetype{textLine.textStart()})};
        const qsizetype end{std::min(to, qsizetype{textLine.textStart()} + textLine.textLength())};
        const bool last{index + 1 == row.layout->lineCount()};
        if (start > end || (start == end && !(last && pastEnd))) {
            continue;
        }
        const qreal left{textLine.cursorToX(static_cast<int>(start))};
        qreal right{textLine.cursorToX(static_cast<int>(end))};
        if (last && pastEnd) {
            right += m_charWidth / 2.0;
        }
        m_selectionRects.append({left, row.top + (static_cast<qreal>(index) * m_lineHeight), right - left, m_lineHeight}
        );
    }
}

void EditorView::appendRangeRects(QList<QRectF> &rects, const Row &row, qsizetype start, qsizetype end) const
{
    for (qint32 index{0}; index < row.layout->lineCount(); ++index) {
        const QTextLine textLine{row.layout->lineAt(index)};
        const qsizetype from{std::max(start, qsizetype{textLine.textStart()})};
        const qsizetype to{std::min(end, qsizetype{textLine.textStart()} + textLine.textLength())};
        if (from >= to) {
            continue;
        }
        const qreal left{textLine.cursorToX(static_cast<int>(from))};
        const qreal right{textLine.cursorToX(static_cast<int>(to))};
        rects.append({left, row.top + (static_cast<qreal>(index) * m_lineHeight), right - left, m_lineHeight});
    }
}

void EditorView::layoutMatches()
{
    // The search's matches in the rows laid out (each on one line).
    m_matchRects.clear();
    if (m_rows.empty() || !m_search->isActive()) {
        return;
    }
    const qsizetype first{buffer().lineStart(m_rows.front().line)};
    const QList<TextSearch::Match> &matches{m_search->matches()};
    for (qsizetype index{m_search->indexFrom(first)}; index < matches.size(); ++index) {
        const TextSearch::Match &match{matches.at(index)};
        const qsizetype line{buffer().lineAt(match.start)};
        if (line > m_rows.back().line) {
            break;
        }
        const Row &row{m_rows.at(static_cast<std::size_t>(line - m_rows.front().line))};
        const qsizetype column{match.start - buffer().lineStart(line)};
        appendRangeRects(m_matchRects, row, column, column + match.length);
    }
}

void EditorView::layoutInvisibles()
{
    // Rows keep their place while the view scrolls: only a layout changes them.
    QList<QRectF> rects;
    if (showInvisibles()) {
        const qreal dot{std::max(1.5, std::round(m_charWidth * 0.2))};
        for (const Row &row : m_rows) {
            for (qsizetype column{0}; column < row.text.size(); ++column) {
                const QChar character{row.text.at(column)};
                if (character != QLatin1Char(' ') && character != QLatin1Char('\t')) {
                    continue;
                }
                const QTextLine textLine{row.layout->lineForTextPosition(static_cast<int>(column))};
                const qreal left{textLine.cursorToX(static_cast<int>(column))};
                const qreal right{textLine.cursorToX(static_cast<int>(column + 1))};
                const qreal middle{row.top + ((textLine.lineNumber() + 0.5) * m_lineHeight)};
                if (character == QLatin1Char(' ')) {
                    rects.append({((left + right) / 2.0) - (dot / 2.0), middle - (dot / 2.0), dot, dot});
                } else {
                    rects.append({left + dot, middle - 0.5, std::max(dot, right - left - (2.0 * dot)), 1.0});
                }
            }
        }
    }
    if (rects != m_invisibleRects) {
        m_invisibleRects = std::move(rects);
        m_flags |= InvisiblesChanged;
    }
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
            matching::matchingBracket(buffer(), m_cursor, bracketSearchLines)
        }) {
        for (const qsizetype position : {pair->bracket, pair->match}) {
            const qsizetype line{buffer().lineAt(position)};
            const auto row{rowAt(line)};
            if (row != m_rows.end()) {
                const qsizetype column{position - buffer().lineStart(line)};
                appendRangeRects(m_bracketRects, *row, column, column + 1);
            }
        }
    }
    const auto cursorRow{rowAt(buffer().lineAt(m_cursor))};
    if (cursorRow == m_rows.end()) {
        return;
    }
    const matching::Range word{matching::wordAt(cursorRow->text, m_cursor - buffer().lineStart(cursorRow->line))};
    if (word.end == word.start) {
        return;
    }
    const QStringView wordText{QStringView{cursorRow->text}.sliced(word.start, word.end - word.start)};
    for (const Row &row : m_rows) {
        for (const matching::Range &found : matching::wholeWords(row.text, wordText)) {
            appendRangeRects(m_wordRects, row, found.start, found.end);
        }
    }
}

void EditorView::updatePolish()
{
    const qreal widestBefore{m_widestLine};
    layoutRows();
    layoutDecorations();
    layoutMatches();
    layoutInvisibles();
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
        auto invisibles{makeRectanglesNode()};
        created->invisibles = invisibles.get();
        created->selectionLayer = selectionLayer.get();
        created->textLayer = textLayer.get();
        created->caret = caret.get();
        created->gutter = gutter.get();
        content->appendChildNode(marksLayer.release());
        content->appendChildNode(selectionLayer.release());
        content->appendChildNode(invisibles.release());
        content->appendChildNode(textLayer.release());
        content->appendChildNode(caret.release());
        clip->appendChildNode(content.release());
        created->appendChildNode(currentLine.release());
        created->appendChildNode(clip.release());
        created->appendChildNode(gutter.release());
        node = created.release();
        // A new window (the view moved to another one) starts without nodes.
        m_flags |= InvisiblesChanged;
    }

    // Scrolling moves the two transforms; the nodes inside move only when
    // their line does, and are filled only when they first show.
    const qreal gutter{gutterWidth()};
    const qreal baseY{m_baseTop - m_contentY};
    setClipRect(node->textClip, {gutter, 0.0, std::max(0.0, width() - gutter), height()});
    setTranslation(node->content, {textLeft() - m_contentX, baseY});
    setTranslation(node->gutter, {0.0, baseY});

    const QColor text{textColor()};
    QHash<quint64, Shown> rows;
    for (const Row &row : m_rows) {
        placeText(window, node->textLayer, node->rows, rows, row.id, row.layout.get(), text, {0.0, row.top});
    }
    removeText(node->textLayer, node->rows);
    node->rows = std::move(rows);

    const qsizetype cursorLine{buffer().lineAt(m_cursor)};
    const QColor numberColor{lineNumberColor()};
    QHash<quint64, Shown> numbers;
    // Numbers and rows are laid out in step: a number sits at its row's top.
    for (std::size_t index{0}; index < m_numbers.size(); ++index) {
        const Number &number{m_numbers.at(index)};
        const qreal y{m_rows.at(index).top};
        const QColor color{number.line == cursorLine ? text : numberColor};
        placeText(
            window,
            node->gutter,
            node->numbers,
            numbers,
            number.id,
            number.layout.get(),
            color,
            {gutter - padding - number.width, y}
        );
    }
    removeText(node->gutter, node->numbers);
    node->numbers = std::move(numbers);

    if ((m_flags & InvisiblesChanged) != 0U) {
        setRectangles(node->invisibles, m_invisibleRects, lineNumberColor());
        m_flags &= ~InvisiblesChanged;
    }
    syncRectangles(window, node->marksLayer, node->matches, m_matchRects, matchColor());
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
    const qsizetype line{buffer().lineAt(m_cursor)};
    const qsizetype lineStart{buffer().lineStart(line)};
    const qsizetype lineEnd{lineStart + buffer().lineLength(line)};
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
            moveCaret(buffer().length(), select);
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
    const qsizetype line{buffer().lineAt(m_cursor)};
    const qsizetype lineStart{buffer().lineStart(line)};
    // ⌘ to the line's start or end, ⌥ to the word's, otherwise one character.
    qsizetype target{std::clamp(m_cursor + (backspace ? -1 : 1), qsizetype{0}, buffer().length())};
    if (command) {
        target = backspace ? lineStart : lineStart + buffer().lineLength(line);
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
        insertNewline();
        return;
    case Qt::Key_Tab:
        insertTab();
        return;
    case Qt::Key_Backtab:
        outdent();
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
        replace(from, std::min<qsizetype>(event->replacementLength(), buffer().length() - from), commit, false);
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
        emit caretMoved();
        polish();
    }
    event->accept();
}

QVariant EditorView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    const qsizetype line{buffer().lineAt(m_cursor)};
    const qsizetype lineStart{buffer().lineStart(line)};
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
        return positionRect(m_anchor).translated(-m_contentX, -m_contentY);
    case Qt::ImSurroundingText:
        return buffer().line(line);
    case Qt::ImCursorPosition:
        return static_cast<qint32>(m_cursor - lineStart);
    case Qt::ImAnchorPosition:
        return static_cast<qint32>(buffer().lineAt(m_anchor) == line ? m_anchor - lineStart : m_cursor - lineStart);
    case Qt::ImAbsolutePosition:
        return static_cast<qint64>(m_cursor);
    case Qt::ImCurrentSelection:
        return selectedText();
    case Qt::ImTextBeforeCursor:
        return buffer().text(lineStart, m_cursor - lineStart);
    case Qt::ImTextAfterCursor:
        return buffer().text(m_cursor, lineStart + buffer().lineLength(line) - m_cursor);
    default:
        return QQuickItem::inputMethodQuery(query);
    }
}

void EditorView::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    // A drag selects (or moves the selection): the Flickable must not take it to scroll.
    setKeepMouseGrab(true);
    const qsizetype position{positionAt(event->position())};
    // A press right after a double click: the third click selects the line.
    const qint32 interval{QGuiApplication::styleHints()->mouseDoubleClickInterval()};
    if (m_lastDoubleClick.isValid() && m_lastDoubleClick.elapsed() < interval) {
        m_lastDoubleClick.invalidate();
        const qsizetype line{buffer().lineAt(position)};
        const qsizetype next{line + 1 < buffer().lineCount() ? buffer().lineStart(line + 1) : buffer().length()};
        select(buffer().lineStart(line), next);
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
    setKeepMouseGrab(false);
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
    // Files dropped on the window open; only text is inserted.
    if (!event->mimeData()->hasText() || event->mimeData()->hasUrls()) {
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
    const qsizetype start{wordBoundary(std::min(position + 1, buffer().length()), false)};
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
