#include "EditorView.h"

#include "Commenting.h"
#include "EditorAccessible.h"
#include "Indentation.h"
#include "Matching.h"
#include "SyntaxHighlighter.h"
#include "TextSearch.h"

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Theme>

#include <QClipboard>
#include <QCursor>
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
#include <QSGVertexColorMaterial>
#include <QStringList>
#include <QStyleHints>
#include <QTextLayout>
#include <QTextOption>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ranges>
#include <span>

namespace kode
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
// Re-indenting on save: lines changed one by one up to this many.
constexpr qsizetype maxIndentEdits{1000};
// How long highlighting may take for Re-Indent, to tell code from comments and strings.
constexpr std::chrono::milliseconds reindentSearch{2000};
// How long finding the block at the caret may take, for its indent guide.
constexpr std::chrono::milliseconds guideSearch{2};
// How far a blank line looks for the lines around it, for its indent guides.
constexpr qsizetype guideNeighbours{100};
// How long finding where a block ends (or starts) may take.
constexpr std::chrono::milliseconds foldSearch{250};

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

// Brackets in comments and strings aside, as far as highlighting has gone.
std::optional<matching::BracketPair>
bracketPair(const TextBuffer &buffer, SyntaxHighlighter &highlighter, qsizetype position)
{
    return matching::matchingBracket(
        buffer,
        position,
        bracketSearchLines,
        [&highlighter](qsizetype line, QStringView text) { return highlighter.nonCode(line, text); }
    );
}

// What a text node shows: refilled only when its color changes (the layout
// behind a key never changes).
struct Shown {
    QSGTextNode *node{nullptr};
    QColor color;
};

} // namespace

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
    QSGGeometryNode *guides{nullptr};
    QSGGeometryNode *scopeGuides{nullptr};
    QSGGeometryNode *squiggles{nullptr};
    QSGNode *selectionLayer{nullptr};
    QSGNode *textLayer{nullptr};
    QSGRectangleNode *caret{nullptr};
    QSGNode *caretLayer{nullptr};
    QSGTransformNode *gutter{nullptr};
    QList<QSGRectangleNode *> selection;
    QList<QSGRectangleNode *> otherCarets;
    QList<QSGRectangleNode *> brackets;
    QList<QSGRectangleNode *> words;
    QList<QSGRectangleNode *> matches;
    QHash<quint64, Shown> rows;
    QHash<quint64, Shown> numbers;
    QHash<qsizetype, Shown> foldMarks;
    QHash<qsizetype, Shown> placeholders;
    QList<QSGRectangleNode *> placeholderBacks;
};

namespace
{

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

// A geometry node drawing triangles colored by their vertices.
std::unique_ptr<QSGGeometryNode> makeColoredNode()
{
    auto node{std::make_unique<QSGGeometryNode>()};
    auto geometry{std::make_unique<QSGGeometry>(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0)};
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    node->setGeometry(geometry.release());
    node->setFlag(QSGNode::OwnsGeometry);
    auto material{std::make_unique<QSGVertexColorMaterial>()};
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
    connect(&m_blinkTimer, &QTimer::timeout, this, [this] {
        m_flags ^= CaretOff;
        update();
    });
    // A moved caret (typing, navigation) shows at once.
    connect(this, &EditorView::cursorChanged, this, &EditorView::restartBlink);
    setFlag(ItemHasContents);
    setFlag(ItemAcceptsInputMethod);
    setFlag(ItemIsFocusScope);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setFlag(ItemAcceptsDrops);
    setActiveFocusOnTab(true);
    setCursor(Qt::IBeamCursor);
    // For ⌘ over a word (a link to its definition).
    setAcceptHoverEvents(true);
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
    connect(document, &TextDocument::diagnosticsChanged, this, &QQuickItem::polish);
    // Rows whose tokens changed are laid out again; the rest stay.
    connect(document, &TextDocument::semanticTokensChanged, this, &QQuickItem::polish);
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
    m_others.clear();
    m_folds.clear();
    m_preedit.clear();
    m_preferredX = -1.0;
    m_widestLine = 0.0;
    m_rows.clear();
    m_numbers.clear();
    m_guess = {};
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
    const qsizetype line{buffer().lineAt(position)};
    const qsizetype removedLines{removed.count(QLatin1Char('\n'))};
    const qsizetype insertedLines{inserted.count(QLatin1Char('\n'))};
    if (wrap()) {
        m_wrapIndex.linesReplaced(buffer(), line, removedLines, insertedLines);
    }
    m_folds.linesReplaced(line, removedLines, insertedLines);
    SyntaxHighlighter::guessEdited(m_guess, line);
    // Another view's edit: the caret stays on the same text.
    if ((m_flags & Editing) == 0U) {
        m_cursor = shifted(m_cursor, position, removed.size(), inserted.size());
        m_anchor = shifted(m_anchor, position, removed.size(), inserted.size());
        for (TextDocument::Caret &other : m_others) {
            other.cursor = shifted(other.cursor, position, removed.size(), inserted.size());
            other.anchor = shifted(other.anchor, position, removed.size(), inserted.size());
        }
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
    m_guess = {};
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
    // The invisibles, guides and underlines take their color with their geometry.
    m_flags |= InvisiblesChanged | GuidesChanged | DiagnosticsChanged;
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

void EditorView::setGuideColor(const QColor &color)
{
    setColor(m_colors.guide, color);
}

void EditorView::setScopeColor(const QColor &color)
{
    setColor(m_colors.scope, color);
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
    return (static_cast<qreal>(digits) * m_charWidth) + (2.0 * padding) + foldColumnWidth();
}

qreal EditorView::foldColumnWidth() const
{
    return std::round(m_charWidth * 1.5);
}

qreal EditorView::textLeft() const
{
    return gutterWidth() + padding;
}

qsizetype EditorView::unfoldedRow(qsizetype line) const
{
    return wrap() ? m_wrapIndex.rowOf(line) : line;
}

qsizetype EditorView::unfoldedLine(qsizetype row) const
{
    return wrap() ? m_wrapIndex.lineAt(row) : std::clamp(row, qsizetype{0}, buffer().lineCount() - 1);
}

qsizetype EditorView::rowOfLine(qsizetype line) const
{
    if (m_folds.isEmpty()) {
        return unfoldedRow(line);
    }
    return m_folds.rowOf(line, [this](qsizetype at) { return unfoldedRow(at); });
}

qsizetype EditorView::lineAtRow(qsizetype row) const
{
    if (m_folds.isEmpty()) {
        return unfoldedLine(row);
    }
    return m_folds.lineAt(
        std::clamp(row, qsizetype{0}, totalRows() - 1),
        [this](qsizetype at) { return unfoldedRow(at); },
        [this](qsizetype at) { return unfoldedLine(at); }
    );
}

qsizetype EditorView::totalRows() const
{
    const qsizetype rows{wrap() ? m_wrapIndex.totalRows() : buffer().lineCount()};
    if (m_folds.isEmpty()) {
        return rows;
    }
    return rows - m_folds.hiddenRows([this](qsizetype at) { return unfoldedRow(at); });
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

qsizetype EditorView::bottomLine() const
{
    const auto row{static_cast<qsizetype>(std::floor((m_contentY + height() - topPadding) / m_lineHeight))};
    return lineAtRow(std::max(row, qsizetype{0}));
}

qreal EditorView::lineTop(qsizetype line) const
{
    return topPadding + (static_cast<qreal>(rowOfLine(line)) * m_lineHeight);
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

void EditorView::setShowIndentGuides(bool show)
{
    if (showIndentGuides() == show) {
        return;
    }
    m_flags = show ? (m_flags | ShowIndentGuides) : (m_flags & ~ShowIndentGuides);
    emit layoutOptionsChanged();
    polish();
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
    // An edit only the main caret makes (Return, Tab, a drop…): the others go.
    m_others.clear();
    m_flags |= Editing;
    m_document->replace(position, count, text, mergeable, {.cursor = m_cursor, .anchor = m_anchor});
    m_flags &= ~Editing;
    editedTo({.cursor = position + text.size(), .anchor = position + text.size()});
}

void EditorView::editedTo(TextDocument::Caret caret)
{
    if (m_folds.reveal(buffer().lineAt(caret.cursor))) {
        emit contentSizeChanged();
    }
    m_cursor = caret.cursor;
    m_anchor = caret.anchor;
    m_preferredX = -1.0;
    m_preferredAnchorX = -1.0;
    emit cursorChanged();
    emit caretMoved();
    emit selectionChanged();
    notifyCursorChanged();
    polish();
}

template <typename RangeOf> void EditorView::replaceEach(RangeOf rangeOf, const QString &text, bool mergeable)
{
    const TextDocument::Range primary{rangeOf(TextDocument::Caret{.cursor = m_cursor, .anchor = m_anchor})};
    QList<TextDocument::Range> ranges;
    ranges.reserve(m_others.size() + 1);
    ranges.append(primary);
    std::ranges::transform(m_others, std::back_inserter(ranges), rangeOf);
    std::ranges::sort(ranges, {}, &TextDocument::Range::position);
    // Overlapping ranges (or carets at one place) join.
    QList<TextDocument::Range> joined;
    joined.reserve(ranges.size());
    for (const TextDocument::Range &range : ranges) {
        if (joined.isEmpty()) {
            joined.append(range);
            continue;
        }
        TextDocument::Range &last{joined.last()};
        const qsizetype lastEnd{last.position + last.count};
        if (range.position < lastEnd || range.position == last.position) {
            last.count = std::max(lastEnd, range.position + range.count) - last.position;
        } else {
            joined.append(range);
        }
    }
    m_flags |= Editing;
    m_document->replaceEach(joined, text, mergeable, {.cursor = m_cursor, .anchor = m_anchor});
    m_flags &= ~Editing;
    // Each caret after its text; the earlier ranges shift the later ones.
    m_others.clear();
    qsizetype shift{0};
    qsizetype caret{-1};
    for (const TextDocument::Range &range : joined) {
        const qsizetype after{range.position + shift + text.size()};
        shift += text.size() - range.count;
        const bool main{primary.position >= range.position && primary.position <= range.position + range.count};
        if (main && caret < 0) {
            caret = after;
        } else {
            m_others.append({.cursor = after, .anchor = after});
        }
    }
    editedTo({.cursor = caret, .anchor = caret});
}

namespace
{

// A caret's selection, or the empty range at it.
TextDocument::Range selectionOf(const TextDocument::Caret &caret)
{
    const qsizetype start{std::min(caret.cursor, caret.anchor)};
    return {.position = start, .count = std::max(caret.cursor, caret.anchor) - start};
}

} // namespace

void EditorView::insert(const QString &text)
{
    if (!m_others.isEmpty()) {
        replaceEach(selectionOf, text, text.size() == 1);
        return;
    }
    replace(selectionStart(), selectionEnd() - selectionStart(), text, !hasSelection() && text.size() == 1);
}

void EditorView::applyChanges(const QList<TextDocument::Change> &changes, qsizetype main)
{
    if (main < 0 || main >= changes.size()) {
        return;
    }
    m_others.clear();
    m_flags |= Editing;
    m_document->replaceEach(changes, {.cursor = m_cursor, .anchor = m_anchor});
    m_flags &= ~Editing;
    const TextDocument::Change &chosen{changes.at(main)};
    qsizetype caret{chosen.range.position + chosen.text.size()};
    for (const TextDocument::Change &before : changes.first(main)) {
        caret += before.text.size() - before.range.count;
    }
    editedTo({.cursor = caret, .anchor = caret});
}

void EditorView::deleteSelection()
{
    if (hasSelection()) {
        replace(selectionStart(), selectionEnd() - selectionStart(), {}, false);
    }
}

void EditorView::undo()
{
    m_others.clear();
    m_flags |= Editing;
    const std::optional<TextDocument::Caret> caret{m_document->undo()};
    m_flags &= ~Editing;
    if (caret) {
        editedTo(*caret);
    }
}

void EditorView::redo()
{
    m_others.clear();
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

void EditorView::selectBlock()
{
    const TextBuffer &text{buffer()};
    const qint64 start{selectionStart()};
    const qint64 end{selectionEnd()};
    const auto lines{[&text](qsizetype first, qsizetype last) {
        return std::pair<qint64, qint64>{text.lineStart(first), text.lineStart(last) + text.lineLength(last)};
    }};
    const auto larger{[start, end](const std::pair<qint64, qint64> &span) {
        return span.first <= start && span.second >= end && (span.first < start || span.second > end);
    }};
    const qsizetype line{text.lineAt(start)};
    std::optional<folding::Fold> block{foldUnder(line)};
    if (!block || block->last + 1 < text.lineAt(end)) {
        block = foldAround(line);
    }
    constexpr qsizetype deepest{64};
    for (qsizetype depth{0}; block && depth < deepest; ++depth) {
        const qsizetype header{block->first - 1};
        // A "{" alone on its line belongs with the line above (a function's signature).
        const bool braceOnly{header > 0 && text.line(header).trimmed() == QLatin1String("{")};
        for (const auto &span :
             {lines(block->first, block->last), lines(braceOnly ? header - 1 : header, block->last + 1)}) {
            if (larger(span)) {
                growSelection(span);
                return;
            }
        }
        const std::optional<folding::Fold> outer{foldAround(braceOnly ? header - 1 : header)};
        block = outer && outer->first < block->first ? outer : std::nullopt;
    }
    if (larger({0, text.length()})) {
        growSelection({0, text.length()});
    }
}

void EditorView::growSelection(const std::pair<qint64, qint64> &span)
{
    // Steps from another selection than the one made here are stale.
    const std::pair<qint64, qint64> current{m_anchor, m_cursor};
    if (current != m_blockSelection) {
        m_blockSteps.clear();
    }
    m_blockSteps.emplace_back(current);
    select(span.first, span.second);
    m_blockSelection = {m_anchor, m_cursor};
}

void EditorView::unselectBlock()
{
    if (m_blockSteps.isEmpty() || std::pair<qint64, qint64>{m_anchor, m_cursor} != m_blockSelection) {
        return;
    }
    const std::pair<qint64, qint64> previous{m_blockSteps.takeLast()};
    select(previous.first, previous.second);
    m_blockSelection = {m_anchor, m_cursor};
}

void EditorView::selectNextOccurrence()
{
    if (!hasSelection()) {
        const qsizetype start{wordBoundary(std::min(m_cursor + 1, buffer().length()), false)};
        const qsizetype end{wordBoundary(start, true)};
        if (start < end && m_cursor >= start && m_cursor <= end) {
            select(start, end);
            m_flags |= WholeWords;
        }
        return;
    }
    const QString needle{selectedText()};
    if (needle.contains(QLatin1Char('\n'))) {
        return;
    }
    const std::optional<TextDocument::Range> found{nextOccurrence(needle, selectionEnd())};
    if (!found) {
        return;
    }
    // The new one becomes the main caret (the view follows it).
    m_others.append({.cursor = m_cursor, .anchor = m_anchor});
    m_document->closeMerge();
    editedTo({.cursor = found->position + found->count, .anchor = found->position});
}

bool EditorView::covered(qsizetype start, qsizetype end) const
{
    const auto overlaps{[start, end](const TextDocument::Caret &caret) {
        const TextDocument::Range range{selectionOf(caret)};
        return start < range.position + range.count && range.position < end;
    }};
    return overlaps({.cursor = m_cursor, .anchor = m_anchor}) || std::ranges::any_of(m_others, overlaps);
}

std::optional<TextDocument::Range> EditorView::nextOccurrence(const QString &needle, qsizetype from) const
{
    const TextBuffer &text{buffer()};
    const qsizetype lines{text.lineCount()};
    const qsizetype firstLine{text.lineAt(from)};
    // Each line once from `from`'s, around the end, and `from`'s again before `from`.
    for (qsizetype step{0}; step <= lines; ++step) {
        const qsizetype line{(firstLine + step) % lines};
        const qsizetype lineStart{text.lineStart(line)};
        const QString content{text.line(line)};
        const qsizetype column{step == 0 ? from - lineStart : 0};
        const auto wholeWord{[&content, &needle](qsizetype found) {
            const qsizetype end{found + needle.size()};
            return (found == 0 || !matching::isWordCharacter(content.at(found - 1))) &&
                   (end == content.size() || !matching::isWordCharacter(content.at(end)));
        }};
        for (qsizetype found{content.indexOf(needle, column)}; found >= 0; found = content.indexOf(needle, found + 1)) {
            const qsizetype position{lineStart + found};
            if (step == lines && position >= from) {
                return std::nullopt;
            }
            const bool word{(m_flags & WholeWords) == 0U || wholeWord(found)};
            if (word && !covered(position, position + needle.size())) {
                return TextDocument::Range{.position = position, .count = needle.size()};
            }
        }
    }
    return std::nullopt;
}

bool EditorView::clearOthers()
{
    if (m_others.isEmpty()) {
        return false;
    }
    m_others.clear();
    emit selectionChanged();
    polish();
    return true;
}

void EditorView::moveEach(Step step)
{
    const TextBuffer &text{buffer()};
    const auto moved{[this, &text, &step](const TextDocument::Caret &caret) {
        const TextDocument::Range range{selectionOf(caret)};
        const qsizetype line{text.lineAt(caret.cursor)};
        qsizetype position{std::clamp(caret.cursor + (step.forward ? 1 : -1), qsizetype{0}, text.length())};
        if (step.line) {
            position = step.forward ? text.lineStart(line) + text.lineLength(line) : text.lineStart(line);
        } else if (step.word) {
            position = wordBoundary(caret.cursor, step.forward);
        } else if (range.count > 0 && !step.extend) {
            position = step.forward ? range.position + range.count : range.position;
        }
        position = skipFolded(position, step.forward);
        return TextDocument::Caret{.cursor = position, .anchor = step.extend ? caret.anchor : position};
    }};
    const TextDocument::Caret primary{moved({.cursor = m_cursor, .anchor = m_anchor})};
    QList<TextDocument::Caret> others;
    others.reserve(m_others.size());
    std::ranges::transform(m_others, std::back_inserter(others), moved);
    // Carets that meet become one.
    std::ranges::sort(others, {}, &TextDocument::Caret::cursor);
    const auto repeated{std::ranges::unique(others, {}, &TextDocument::Caret::cursor)};
    others.erase(repeated.begin(), repeated.end());
    others.removeIf([&primary](const TextDocument::Caret &caret) { return caret.cursor == primary.cursor; });
    m_others = std::move(others);
    m_document->closeMerge();
    editedTo(primary);
}

qsizetype EditorView::rowAt(qsizetype position) const
{
    return static_cast<qsizetype>(std::lround((positionRect(position).y() - topPadding) / m_lineHeight));
}

void EditorView::addCaretVertically(qint32 lines, bool block)
{
    const auto outermost{[&lines](qsizetype a, qsizetype b) { return lines > 0 ? std::max(a, b) : std::min(a, b); }};
    qsizetype from{m_cursor};
    for (const TextDocument::Caret &other : m_others) {
        from = outermost(from, other.cursor);
    }
    const qsizetype row{rowAt(from) + lines};
    if (row < 0 || row >= totalRows()) {
        return;
    }
    const qreal cursorX{m_preferredX >= 0.0 ? m_preferredX : positionRect(m_cursor).x() - textLeft()};
    const qreal anchorX{
        !block || !hasSelection()   ? cursorX
        : m_preferredAnchorX >= 0.0 ? m_preferredAnchorX
                                    : positionRect(m_anchor).x() - textLeft()
    };
    const TextDocument::Caret added{.cursor = positionAtRow(row, cursorX), .anchor = positionAtRow(row, anchorX)};
    // The new one becomes the main caret (the view follows it).
    m_others.append({.cursor = m_cursor, .anchor = m_anchor});
    m_flags &= ~WholeWords;
    m_document->closeMerge();
    editedTo(added);
    m_preferredX = cursorX;
    m_preferredAnchorX = anchorX;
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
    if (!m_others.isEmpty()) {
        replaceEach(selectionOf, text, false);
        return;
    }
    replace(selectionStart(), selectionEnd() - selectionStart(), text, false);
}

// --- caret and selection -------------------------------------------------------

void EditorView::moveCaret(qsizetype position, Select select)
{
    position = std::clamp(position, qsizetype{0}, buffer().length());
    clearOthers();
    m_flags &= ~WholeWords;
    if (m_folds.reveal(buffer().lineAt(position))) {
        emit contentSizeChanged();
        polish();
    }
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
    const qsizetype row{rowAt(m_cursor)};
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
        // Past the spaces, then past a word or a run of punctuation.
        while (column < text.size() && text.at(column).isSpace()) {
            ++column;
        }
        const bool word{column < text.size() && isWordCharacter(text.at(column))};
        while (column < text.size() && !text.at(column).isSpace() && isWordCharacter(text.at(column)) == word) {
            ++column;
        }
        return lineStart + column;
    }
    if (column == 0) {
        return std::max(position - 1, qsizetype{0});
    }
    while (column > 0 && text.at(column - 1).isSpace()) {
        --column;
    }
    const bool word{column > 0 && isWordCharacter(text.at(column - 1))};
    while (column > 0 && !text.at(column - 1).isSpace() && isWordCharacter(text.at(column - 1)) == word) {
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

void EditorView::reindentEdited()
{
    // Spans apart only by blank lines are one: a span indents from the line above it as it stands.
    QList<TextDocument::Lines> spans;
    for (const TextDocument::Lines &lines : m_document->editedLines()) {
        const qsizetype last{std::min(lines.last, buffer().lineCount() - 1)};
        if (!spans.isEmpty() &&
            std::ranges::all_of(std::views::iota(spans.last().last + 1, lines.first), [this](qsizetype line) {
                return buffer().line(line).trimmed().isEmpty();
            })) {
            spans.last().last = last;
            continue;
        }
        spans.append({.first = lines.first, .last = last});
    }
    // The lines that move and their indentation, in order.
    QList<std::pair<qsizetype, qint32>> moved;
    for (const TextDocument::Lines &span : std::as_const(spans)) {
        const std::optional<QList<qint32>> columns{reindented(span.first, span.last)};
        if (!columns) {
            return;
        }
        for (qsizetype line{span.first}; line <= span.last; ++line) {
            if (const qint32 column{columns->at(line - span.first)}; column >= 0) {
                moved.emplace_back(line, column);
            }
        }
    }
    if (moved.isEmpty()) {
        return;
    }
    // The caret stays on the same text (lines only change their indentation).
    const qsizetype line{buffer().lineAt(m_cursor)};
    const qsizetype column{
        m_cursor - buffer().lineStart(line) - indentation::leadingWhitespace(buffer().line(line)).size()
    };
    m_others.clear();
    m_flags |= Editing;
    applyIndentation(moved);
    m_flags &= ~Editing;
    const QString text{buffer().line(line)};
    const qsizetype leading{indentation::leadingWhitespace(text).size()};
    moveCaret(
        buffer().lineStart(line) + std::min(leading + std::max(column, qsizetype{0}), text.size()),
        Select::Collapse
    );
}

void EditorView::applyIndentation(const QList<std::pair<qsizetype, qint32>> &moved)
{
    const TextDocument::Caret before{.cursor = m_cursor, .anchor = m_anchor};
    // Each edit to the piece table costs more as they add up: many lines go
    // as one replacement, from the first to the last.
    if (moved.size() > maxIndentEdits) {
        const qsizetype firstLine{moved.constFirst().first};
        const qsizetype lastLine{moved.constLast().first};
        QList<qint32> columns(lastLine - firstLine + 1, -1);
        for (const auto &[line, column] : moved) {
            columns[line - firstLine] = column;
        }
        const qsizetype start{buffer().lineStart(firstLine)};
        const qsizetype end{buffer().lineStart(lastLine) + buffer().lineLength(lastLine)};
        m_document->replace(start, end - start, reindentedText(firstLine, lastLine, columns, firstLine), false, before);
        return;
    }
    const indentation::Style style{indentStyle()};
    QList<TextDocument::Change> changes;
    changes.reserve(moved.size());
    for (const auto &[line, column] : moved) {
        changes.append(
            {.range =
                 {.position = buffer().lineStart(line),
                  .count = indentation::leadingWhitespace(buffer().line(line)).size()},
             .text = indentation::whitespace(column, style)}
        );
    }
    m_document->replaceEach(changes, before);
}

bool EditorView::colonBlocks() const
{
    return highlighter().isActive() && highlighter().definition().indentationBasedFoldingEnabled();
}

QList<indentation::Span> EditorView::nonCode(qsizetype line, QStringView text)
{
    if (!highlighter().advance(line, QDeadlineTimer{foldSearch})) {
        return {};
    }
    return highlighter().nonCode(line, text);
}

void EditorView::insertNewline()
{
    const qsizetype line{buffer().lineAt(selectionStart())};
    const qsizetype lineStart{buffer().lineStart(line)};
    const QString text{buffer().line(line)};
    const QStringView before{QStringView{text}.first(selectionStart() - lineStart)};
    const QStringView leading{indentation::leadingWhitespace(before)};
    if (!m_others.isEmpty()) {
        insert(QStringLiteral("\n") + leading);
        return;
    }
    // What follows the caret starts the new line without its spaces.
    const qsizetype endLine{buffer().lineAt(selectionEnd())};
    const QString rest{buffer().line(endLine).sliced(selectionEnd() - buffer().lineStart(endLine))};
    const qsizetype restSpaces{indentation::leadingWhitespace(rest).size()};
    const indentation::Style style{indentStyle()};
    const indentation::Brackets above{indentation::brackets(before, nonCode(line, text))};
    const qint32 closing{indentation::brackets(rest, {}).leading};
    const qsizetype previous{indentation::columns(leading, style.width)};
    const auto indentFor{[&](qint32 leadingClosers) {
        const qsizetype columns{indentation::indentAfter(previous, above, leadingClosers, style, colonBlocks())};
        return columns == previous ? leading.toString() : indentation::whitespace(columns, style);
    }};
    const qsizetype start{selectionStart()};
    const qsizetype count{selectionEnd() + restSpaces - start};
    if (above.open > 0 && closing > 0) {
        const QString inner{QStringLiteral("\n") + indentFor(0)};
        replace(start, count, inner + QStringLiteral("\n") + indentFor(closing), false);
        moveCaret(start + inner.size(), Select::Collapse);
        return;
    }
    if (const std::optional<QChar> closer{above.open > 0 ? closerToAdd(line, before, rest) : std::nullopt}) {
        const QString inner{QStringLiteral("\n") + indentFor(0)};
        replace(start, count, inner + QStringLiteral("\n") + indentFor(1) + *closer, false);
        moveCaret(start + inner.size(), Select::Collapse);
        return;
    }
    replace(start, count, QStringLiteral("\n") + indentFor(closing), false);
}

std::optional<QChar> EditorView::closerToAdd(qsizetype line, QStringView before, QStringView rest) const
{
    constexpr QStringView openers{u"([{"};
    constexpr QStringView closers{u")]}"};
    const QStringView trimmed{before.trimmed()};
    if (m_document->syntax().isEmpty() || !rest.trimmed().isEmpty() || trimmed.isEmpty() ||
        !openers.contains(trimmed.back())) {
        return std::nullopt;
    }
    // A block already under it (its first line indented deeper): it has its closing one.
    constexpr qsizetype linesToRead{50};
    const indentation::Style style{indentStyle()};
    const qsizetype own{indentation::columns(indentation::leadingWhitespace(before), style.width)};
    const qsizetype last{std::min(buffer().lineCount() - 1, line + linesToRead)};
    for (qsizetype next{line + 1}; next <= last; ++next) {
        const QString text{buffer().line(next)};
        if (text.trimmed().isEmpty()) {
            continue;
        }
        if (indentation::columns(indentation::leadingWhitespace(text), style.width) > own) {
            return std::nullopt;
        }
        break;
    }
    return closers.at(openers.indexOf(trimmed.back()));
}

bool EditorView::typePair(const QString &text)
{
    constexpr QStringView openers{u"([{\"'`"};
    constexpr QStringView closers{u")]}\"'`"};
    if (text.size() != 1 || !m_others.isEmpty() || hasSelection() || m_document->syntax().isEmpty()) {
        return false;
    }
    const QChar typed{text.front()};
    const qsizetype lineStart{buffer().lineStart(buffer().lineAt(m_cursor))};
    const QString line{buffer().line(buffer().lineAt(m_cursor))};
    const qsizetype column{m_cursor - lineStart};
    const QChar next{column < line.size() ? line.at(column) : QChar{}};
    const QChar previous{column > 0 ? line.at(column - 1) : QChar{}};
    if (closers.contains(typed) && next == typed) {
        moveCaret(m_cursor + 1, Select::Collapse);
        return true;
    }
    const qsizetype pair{openers.indexOf(typed)};
    // Only before a space, the line's end or what ends an expression.
    if (pair < 0 || (!next.isNull() && !next.isSpace() && !QStringView{u")]};,."}.contains(next))) {
        return false;
    }
    // A quote not after a word (an apostrophe), the same quote or an escape;
    // never in prose.
    constexpr qsizetype firstQuote{3};
    const bool prose{m_document->syntax() == QLatin1StringView{"Markdown"}};
    if (pair >= firstQuote &&
        (prose || matching::isWordCharacter(previous) || previous == typed || previous == u'\\')) {
        return false;
    }
    const qsizetype at{m_cursor};
    replace(at, 0, QString{typed} + closers.at(pair), false);
    moveCaret(at + 1, Select::Collapse);
    return true;
}

void EditorView::alignClosing()
{
    const qsizetype line{buffer().lineAt(m_cursor)};
    const qsizetype lineStart{buffer().lineStart(line)};
    const QString text{buffer().line(line)};
    const QStringView leading{indentation::leadingWhitespace(text)};
    if (m_cursor - lineStart != leading.size() + 1 || !QStringView{u")]}"}.contains(text.at(leading.size()))) {
        return;
    }
    const std::optional<matching::BracketPair> pair{bracketPair(buffer(), highlighter(), m_cursor - 1)};
    if (!pair || pair->match >= pair->bracket) {
        return;
    }
    const QString opening{buffer().line(buffer().lineAt(pair->match))};
    const QStringView wanted{indentation::leadingWhitespace(opening)};
    if (wanted == leading) {
        return;
    }
    replace(lineStart, leading.size(), wanted.toString(), false);
    moveCaret(lineStart + wanted.size() + 1, Select::Collapse);
}

std::optional<QList<qint32>> EditorView::reindented(qsizetype firstLine, qsizetype lastLine)
{
    // Not knowing what is a comment or string would move lines wrongly.
    if (colonBlocks() || !highlighter().advance(lastLine, QDeadlineTimer{reindentSearch})) {
        return std::nullopt;
    }
    const indentation::Style style{indentStyle()};
    // From the closest line above with text in it (the start: none).
    qsizetype previous{0};
    indentation::Brackets above;
    for (qsizetype line{firstLine - 1}; line >= 0; --line) {
        const QString text{buffer().line(line)};
        if (!text.trimmed().isEmpty()) {
            previous = indentation::columns(indentation::leadingWhitespace(text), style.width);
            above = indentation::brackets(text, nonCode(line, text));
            break;
        }
    }
    QList<qint32> columns(lastLine - firstLine + 1, -1);
    // What the last line indented moved by, for the comment or string lines after it.
    qsizetype moved{0};
    for (qsizetype line{firstLine}; line <= lastLine; ++line) {
        const QString text{buffer().line(line)};
        const QStringView leading{indentation::leadingWhitespace(text)};
        const QStringView content{QStringView{text}.sliced(leading.size())};
        const qsizetype was{indentation::columns(leading, style.width)};
        // Blank: no spaces left. Inside a comment or string from above: moved as the line before.
        qsizetype wanted{0};
        if (!content.isEmpty() && !leading.isEmpty() && highlighter().startsInNonCode(line)) {
            wanted = std::max(qsizetype{0}, was + moved);
        } else if (!content.isEmpty()) {
            // Comments and strings only matter where there are brackets to skip.
            const bool bracketed{std::ranges::any_of(content, [](QChar character) {
                return QStringView{u"()[]{}"}.contains(character);
            })};
            const indentation::Brackets own{
                indentation::brackets(text, bracketed ? nonCode(line, text) : QList<indentation::Span>{})
            };
            previous = indentation::indentAfter(previous, above, own.leading, style, false);
            moved = previous - was;
            above = own;
            wanted = previous;
        }
        if (!indentation::isWhitespace(leading, wanted, style)) {
            columns[line - firstLine] = static_cast<qint32>(wanted);
        }
    }
    return columns;
}

QString
EditorView::reindentedText(qsizetype firstLine, qsizetype lastLine, const QList<qint32> &columns, qsizetype base) const
{
    const indentation::Style style{indentStyle()};
    QString result;
    result.reserve(buffer().lineStart(lastLine) + buffer().lineLength(lastLine) - buffer().lineStart(firstLine));
    for (qsizetype line{firstLine}; line <= lastLine; ++line) {
        if (line > firstLine) {
            result += QLatin1Char('\n');
        }
        const QString text{buffer().line(line)};
        const qint32 column{columns.at(line - base)};
        if (column < 0) {
            result += text;
            continue;
        }
        result += indentation::whitespace(column, style);
        result += QStringView{text}.sliced(indentation::leadingWhitespace(text).size());
    }
    return result;
}

void EditorView::reindent()
{
    const qsizetype firstLine{buffer().lineAt(selectionStart())};
    qsizetype lastLine{buffer().lineAt(selectionEnd())};
    if (hasSelection() && lastLine > firstLine && selectionEnd() == buffer().lineStart(lastLine)) {
        --lastLine;
    }
    const std::optional<QList<qint32>> columns{reindented(firstLine, lastLine)};
    if (!columns || std::ranges::all_of(*columns, [](qint32 column) { return column < 0; })) {
        return;
    }
    const qsizetype start{buffer().lineStart(firstLine)};
    const qsizetype end{buffer().lineStart(lastLine) + buffer().lineLength(lastLine)};
    const QString after{reindentedText(firstLine, lastLine, *columns, firstLine)};
    const bool selected{hasSelection()};
    // One line: the caret stays on the same text (or after the indentation).
    const qsizetype caretColumn{m_cursor - start - indentation::leadingWhitespace(buffer().line(firstLine)).size()};
    replace(start, end - start, after, false);
    if (selected) {
        select(start, start + after.size());
        return;
    }
    moveCaret(
        start + indentation::leadingWhitespace(after).size() + std::max(caretColumn, qsizetype{0}),
        Select::Collapse
    );
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

template <typename Change> void EditorView::changeSelectedLines(Change change)
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
    const QString after{change(QStringView{before})};
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

void EditorView::indent()
{
    const indentation::Style style{indentStyle()};
    changeSelectedLines([style](QStringView lines) { return indentation::indented(lines, style); });
}

void EditorView::outdent()
{
    const indentation::Style style{indentStyle()};
    changeSelectedLines([style](QStringView lines) { return indentation::outdented(lines, style); });
}

commenting::Markers EditorView::commentMarkers() const
{
    if (!highlighter().isActive()) {
        return {};
    }
    const KSyntaxHighlighting::Definition definition{highlighter().definition()};
    const QPair<QString, QString> block{definition.multiLineCommentMarker()};
    return {.line = definition.singleLineCommentMarker(), .open = block.first, .close = block.second};
}

void EditorView::toggleLineComment()
{
    const commenting::Markers markers{commentMarkers()};
    const bool atColumnZero{
        highlighter().isActive() &&
        highlighter().definition().singleLineCommentPosition() == KSyntaxHighlighting::CommentPosition::StartOfLine
    };
    changeSelectedLines([&markers, &atColumnZero](QStringView lines) {
        return commenting::toggledLines(lines, markers, atColumnZero);
    });
}

void EditorView::toggleBlockComment()
{
    const commenting::Markers markers{commentMarkers()};
    if (markers.open.isEmpty() || markers.close.isEmpty()) {
        toggleLineComment();
        return;
    }
    if (!hasSelection()) {
        changeSelectedLines([&markers](QStringView line) {
            const QStringView indent{indentation::leadingWhitespace(line)};
            return indent.toString() + commenting::toggledBlock(line.sliced(indent.size()), markers);
        });
        return;
    }
    const qsizetype start{selectionStart()};
    const QString after{commenting::toggledBlock(selectedText(), markers)};
    replace(start, selectionEnd() - start, after, false);
    select(start, start + after.size());
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
    m_foldableGlyph.reset();
    m_foldedGlyph.reset();
    m_placeholderGlyph.reset();
    m_flags |= FoldGlyphsChanged;
}

qsizetype EditorView::firstVisibleLine() const
{
    const auto row{static_cast<qsizetype>(std::floor((m_contentY - topPadding) / m_lineHeight))};
    return lineAtRow(std::max(row - overscan, qsizetype{0}));
}

qsizetype EditorView::lastVisibleLine() const
{
    const auto row{static_cast<qsizetype>(std::floor((m_contentY + height() - topPadding) / m_lineHeight))};
    return std::min(lineAtRow(row + overscan) + 1, buffer().lineCount());
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

namespace
{

// The server's styles in place of the highlighting's where its tokens are,
// as Qt Creator's replace them (what the token's style leaves unset is the
// plain text's, not the highlighting's).
QList<QTextLayout::FormatRange> withSemanticTokens(
    const QList<QTextLayout::FormatRange> &formats,
    const QList<TextDocument::SemanticToken> &tokens,
    SyntaxHighlighter &highlighter
)
{
    QList<QTextLayout::FormatRange> result;
    result.reserve(formats.size() + tokens.size());
    for (const QTextLayout::FormatRange &range : formats) {
        const qint32 end{range.start + range.length};
        qint32 from{range.start};
        for (const TextDocument::SemanticToken &token : tokens) {
            const qint32 tokenEnd{token.column + token.length};
            if (tokenEnd <= from || token.column >= end) {
                continue;
            }
            if (token.column > from) {
                result.append({.start = from, .length = token.column - from, .format = range.format});
            }
            from = std::max(from, tokenEnd);
        }
        if (from < end) {
            result.append({.start = from, .length = end - from, .format = range.format});
        }
    }
    for (const TextDocument::SemanticToken &token : tokens) {
        result.append({.start = token.column, .length = token.length, .format = highlighter.semanticFormat(token.style)}
        );
    }
    // In order: the text node takes each range's color assuming so.
    std::ranges::stable_sort(result, {}, &QTextLayout::FormatRange::start);
    return result;
}

} // namespace

EditorView::Row EditorView::takeRow(qsizetype line, qsizetype cursorLine)
{
    // A row already laid out with the same text, variant and starting state
    // (wherever its line was), else a new one.
    const bool composing{!m_preedit.isEmpty() && line == cursorLine};
    auto [variant, state]{composing ? std::pair{m_preeditGeneration, KSyntaxHighlighting::State{}} : rowStart(line)};
    const bool highlighted{variant == Highlighted || variant == Guessed};
    QString text{displayText(line)};
    const QList<TextDocument::SemanticToken> &semantic{
        composing ? QList<TextDocument::SemanticToken>{} : m_document->semanticTokens(line)
    };
    const auto reusable{std::ranges::find_if(m_rows, [&text, &state, &semantic, variant](const Row &row) {
        return row.layout != nullptr && row.variant == variant && row.text == text && row.state == state &&
               row.semantic == semantic;
    })};
    if (reusable != m_rows.end()) {
        Row row{std::move(*reusable)};
        row.line = line;
        return row;
    }
    Row row{
        .id = m_nextRowId++,
        .line = line,
        .variant = variant,
        .text = {},
        .layout = {},
        .state = std::move(state),
        .semantic = semantic,
    };
    QList<QTextLayout::FormatRange> formats{
        highlighted ? highlighter().formats(line, text, row.state) : QList<QTextLayout::FormatRange>{}
    };
    if (!semantic.isEmpty()) {
        formats = withSemanticTokens(formats, semantic, highlighter());
    }
    row.layout = layoutText(text, composing ? m_cursor - buffer().lineStart(line) : -1, formats);
    row.opens = highlighted ? highlighter().openedRegions() : 0;
    row.width = row.layout->lineAt(0).naturalTextWidth();
    row.text = std::move(text);
    m_widestLine = std::max(m_widestLine, row.width);
    return row;
}

std::pair<quint64, KSyntaxHighlighting::State> EditorView::rowStart(qsizetype line) const
{
    if (!highlighter().isActive()) {
        return {Plain, {}};
    }
    if (highlighter().isReady(line)) {
        return {Highlighted, highlighter().startState(line)};
    }
    const qsizetype guessed{line - m_guess.first};
    if (guessed >= 0 && guessed < m_guess.states.size()) {
        return {Guessed, m_guess.states.at(guessed)};
    }
    return {Plain, {}};
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
    // Folded lines between them included.
    const qsizetype count{lastVisibleLine() - first};
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
    // Far ahead of it: highlighted from a guessed state meanwhile.
    if (!highlighter().isReady(first + count)) {
        highlighter().guess(m_guess, first, first + count, QDeadlineTimer{highlightInLayout});
    }
    std::vector<Row> rows;
    std::vector<Number> numbers;
    const auto shown{static_cast<std::size_t>(std::min(count, visibleLineCount()))};
    rows.reserve(shown);
    numbers.reserve(shown);
    qsizetype rowsAbove{0};
    for (qsizetype line{first}; line < buffer().lineCount() && baseRow + rowsAbove <= lastRow;
         line = m_folds.nextShown(line + 1)) {
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
    // The guess in view first: the layout may have left it short.
    if (const qsizetype first{firstVisibleLine()}; !wasReady && !m_guess.covers(first, last)) {
        highlighter().guess(m_guess, first, last, QDeadlineTimer{highlightSlice});
        polish();
        m_highlightTimer.start();
        return;
    }
    const bool done{
        highlighter().advance(std::min(last + highlightLookahead, buffer().lineCount()), QDeadlineTimer{highlightSlice})
    };
    if (!wasReady && highlighter().isReady(last)) {
        m_guess = {};
        polish();
    }
    emit highlightingAdvanced();
    if (!done) {
        m_highlightTimer.start();
    }
}

void EditorView::layoutDecorations()
{
    // The selection, one rectangle per line in view, past the text when the
    // line's end is selected too; and the caret.
    m_selectionRects.clear();
    m_otherCaretRects.clear();
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
            m_caretRect = caretRect(row, column);
        }
        if (hasSelection() && selectionEnd() > lineStart && selectionStart() <= lineEnd) {
            appendSelectionRects(row, lineStart, lineEnd, selectionStart(), selectionEnd());
        }
        layoutOthers(row, lineStart, lineEnd);
    }
}

QRectF EditorView::caretRect(const Row &row, qsizetype column) const
{
    const QTextLine textLine{row.layout->lineForTextPosition(static_cast<int>(column))};
    return {
        textLine.cursorToX(static_cast<int>(column)),
        row.top + (static_cast<qreal>(textLine.lineNumber()) * m_lineHeight),
        caretWidth,
        m_lineHeight
    };
}

void EditorView::layoutOthers(const Row &row, qsizetype lineStart, qsizetype lineEnd)
{
    for (const TextDocument::Caret &other : m_others) {
        const TextDocument::Range range{selectionOf(other)};
        if (range.count > 0 && range.position + range.count > lineStart && range.position <= lineEnd) {
            appendSelectionRects(row, lineStart, lineEnd, range.position, range.position + range.count);
        }
        if (other.cursor >= lineStart && other.cursor <= lineEnd) {
            m_otherCaretRects.append(caretRect(row, other.cursor - lineStart));
        }
    }
}

void EditorView::appendSelectionRects(
    const Row &row,
    qsizetype lineStart,
    qsizetype lineEnd,
    qsizetype selectedFrom,
    qsizetype selectedTo
)
{
    // One rectangle per visual line, past the text when the line's end is
    // selected too.
    const qsizetype from{std::max(selectedFrom, lineStart) - lineStart};
    const qsizetype to{std::min(selectedTo, lineEnd) - lineStart};
    const bool pastEnd{selectedTo > lineEnd};
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
        // None for a folded line.
        const auto row{std::ranges::lower_bound(m_rows, line, {}, &Row::line)};
        if (row == m_rows.end() || row->line != line) {
            continue;
        }
        const qsizetype column{match.start - buffer().lineStart(line)};
        appendRangeRects(m_matchRects, *row, column, column + match.length);
    }
}

void EditorView::layoutDiagnostics()
{
    QList<Squiggle> squiggles;
    QVariantList areas;
    const QList<TextDocument::Diagnostic> &diagnostics{m_document->diagnostics()};
    if (!m_rows.empty() && !diagnostics.isEmpty()) {
        const qsizetype first{buffer().lineStart(m_rows.front().line)};
        const qsizetype last{buffer().lineStart(m_rows.back().line) + buffer().lineLength(m_rows.back().line)};
        // Sorted by start: those starting past the rows end it.
        for (const TextDocument::Diagnostic &diagnostic : diagnostics) {
            if (diagnostic.start > last) {
                break;
            }
            if (diagnostic.end >= first) {
                appendDiagnostic(diagnostic, squiggles, areas);
            }
        }
    }
    if (squiggles != m_squiggles) {
        m_squiggles = std::move(squiggles);
        m_flags |= DiagnosticsChanged;
    }
    if (areas != m_diagnosticAreas) {
        m_diagnosticAreas = std::move(areas);
        emit diagnosticAreasChanged();
    }
}

void EditorView::appendDiagnostic(
    const TextDocument::Diagnostic &diagnostic,
    QList<Squiggle> &squiggles,
    QVariantList &areas
) const
{
    const qsizetype startLine{buffer().lineAt(diagnostic.start)};
    const qsizetype endLine{buffer().lineAt(diagnostic.end)};
    const auto firstRow{std::ranges::lower_bound(m_rows, startLine, {}, &Row::line)};
    for (auto row{firstRow}; row != m_rows.end() && row->line <= endLine; ++row) {
        const qsizetype lineStart{buffer().lineStart(row->line)};
        qsizetype from{row->line == startLine ? diagnostic.start - lineStart : 0};
        qsizetype to{row->line == endLine ? diagnostic.end - lineStart : row->text.size()};
        // An empty range marks the character at it (or before it, at a line's end).
        if (from >= to) {
            to = std::min(from + 1, row->text.size());
            from = std::max(qsizetype{0}, to - 1);
        }
        QList<QRectF> rects;
        appendRangeRects(rects, *row, from, to);
        if (rects.isEmpty()) {
            // An empty line: one character's width.
            rects.append({0.0, row->top, m_charWidth, m_lineHeight});
        }
        for (const QRectF &rect : std::as_const(rects)) {
            squiggles.append({.rect = rect, .severity = diagnostic.severity});
            areas.append(QVariantMap{
                {QStringLiteral("x"), textLeft() + rect.x()},
                {QStringLiteral("y"), m_baseTop + rect.y()},
                {QStringLiteral("width"), rect.width()},
                {QStringLiteral("height"), rect.height()},
                {QStringLiteral("message"), diagnostic.message},
            });
        }
    }
}

QRgb EditorView::severityColor(qint64 severity) const noexcept
{
    switch (severity) {
    case TextDocument::Diagnostic::Error:
        return m_colors.error;
    case TextDocument::Diagnostic::Warning:
        return m_colors.warning;
    case TextDocument::Diagnostic::Information:
        return m_colors.information;
    default:
        return m_colors.hint;
    }
}

void EditorView::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Control) {
        updateLink(event->modifiers());
    }
    QQuickItem::keyReleaseEvent(event);
}

void EditorView::hoverMoveEvent(QHoverEvent *event)
{
    updateLink(event->modifiers());
}

void EditorView::hoverLeaveEvent(QHoverEvent *event)
{
    setLink(-1, -1);
    QQuickItem::hoverLeaveEvent(event);
}

void EditorView::updateLink(Qt::KeyboardModifiers modifiers)
{
    // Where the pointer is now: the text may have scrolled under it since it moved.
    const QPointF point{mapFromGlobal(QCursor::pos())};
    if (modifiers != Qt::ControlModifier || m_document->languageServer().isEmpty() || !isUnderMouse() ||
        point.x() < textLeft()) {
        setLink(-1, -1);
        return;
    }
    const qsizetype position{positionAt(point)};
    const qsizetype line{buffer().lineAt(position)};
    const qsizetype lineStart{buffer().lineStart(line)};
    const QString text{buffer().text(lineStart, buffer().lineLength(line))};
    const matching::Range word{matching::wordAt(text, position - lineStart)};
    if (word.end <= word.start) {
        setLink(-1, -1);
        return;
    }
    setLink(lineStart + word.start, lineStart + word.end);
}

void EditorView::setLink(qsizetype start, qsizetype end)
{
    if (start == m_linkStart && end == m_linkEnd) {
        return;
    }
    m_linkStart = start;
    m_linkEnd = end;
    setCursor(start >= 0 ? Qt::PointingHandCursor : Qt::IBeamCursor);
    polish();
}

void EditorView::layoutLink()
{
    QList<QRectF> rects;
    // An edit may have moved the text under it: the next move finds it again.
    if (m_linkStart >= 0 && m_linkEnd <= buffer().length()) {
        const qsizetype line{buffer().lineAt(m_linkStart)};
        const qsizetype lineStart{buffer().lineStart(line)};
        const auto first{std::ranges::lower_bound(m_rows, line, {}, &Row::line)};
        for (auto row{first}; row != m_rows.end() && row->line == line; ++row) {
            appendRangeRects(rects, *row, m_linkStart - lineStart, m_linkEnd - lineStart);
        }
    }
    if (rects != m_linkRects) {
        m_linkRects = std::move(rects);
        m_flags |= DiagnosticsChanged;
    }
}

void EditorView::fillSquiggles(QSGGeometryNode *node) const
{
    // A wave under the text: 2 px up, 2 px down, each stroke a 1 px band.
    constexpr qreal step{2.0};
    constexpr qreal amplitude{2.0};
    constexpr qreal half{0.6};
    const auto strokes{[](const Squiggle &squiggle) {
        return static_cast<qsizetype>(std::ceil(squiggle.rect.width() / step));
    }};
    qsizetype count{0};
    for (const Squiggle &squiggle : m_squiggles) {
        count += strokes(squiggle);
    }
    // The link's underline: one band per rectangle, in the text's color.
    count += m_linkRects.size();
    QSGGeometry *const geometry{node->geometry()};
    geometry->allocate(static_cast<int>(count * 6));
    const std::span vertices{geometry->vertexDataAsColoredPoint2D(), static_cast<std::size_t>(count * 6)};
    std::size_t vertex{0};
    for (const QRectF &rect : m_linkRects) {
        const QRgb color{m_colors.text};
        const qint32 alpha{qAlpha(color)};
        const auto red{static_cast<uchar>(qRed(color) * alpha / 255)};
        const auto green{static_cast<uchar>(qGreen(color) * alpha / 255)};
        const auto blue{static_cast<uchar>(qBlue(color) * alpha / 255)};
        const auto left{static_cast<float>(rect.left())};
        const auto right{static_cast<float>(rect.right())};
        const auto top{static_cast<float>(rect.bottom() - 2.0)};
        const auto bottom{static_cast<float>(rect.bottom() - 1.0)};
        for (const auto &[x, y] :
             {std::pair{left, top}, {right, top}, {left, bottom}, {right, top}, {right, bottom}, {left, bottom}}) {
            vertices[vertex++].set(x, y, red, green, blue, static_cast<uchar>(alpha));
        }
    }
    for (const Squiggle &squiggle : m_squiggles) {
        const QRgb color{severityColor(squiggle.severity)};
        const qint32 alpha{qAlpha(color)};
        const auto red{static_cast<uchar>(qRed(color) * alpha / 255)};
        const auto green{static_cast<uchar>(qGreen(color) * alpha / 255)};
        const auto blue{static_cast<uchar>(qBlue(color) * alpha / 255)};
        const qreal bottom{squiggle.rect.bottom() - 1.0};
        for (qsizetype stroke{0}; stroke < strokes(squiggle); ++stroke) {
            const qreal x0{squiggle.rect.left() + (static_cast<qreal>(stroke) * step)};
            const auto left{static_cast<float>(x0)};
            const auto right{static_cast<float>(std::min(x0 + step, squiggle.rect.right()))};
            const auto from{static_cast<float>(stroke % 2 == 0 ? bottom : bottom - amplitude)};
            const auto to{static_cast<float>(stroke % 2 == 0 ? bottom - amplitude : bottom)};
            const auto band{static_cast<float>(half)};
            for (const auto &[x, y] :
                 {std::pair{left, from - band},
                  {right, to - band},
                  {left, from + band},
                  {right, to - band},
                  {right, to + band},
                  {left, from + band}}) {
                vertices[vertex++].set(x, y, red, green, blue, static_cast<uchar>(alpha));
            }
        }
    }
    node->markDirty(QSGNode::DirtyGeometry);
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

qsizetype EditorView::guideIndent(const Row &row) const
{
    const qsizetype width{indentWidth()};
    const qsizetype indent{folding::indentOf(row.text, width)};
    if (indent >= 0) {
        return indent;
    }
    const auto neighbour{[this, &row, width](qsizetype step) {
        for (qsizetype line{row.line + step}, seen{0};
             line >= 0 && line < buffer().lineCount() && seen < guideNeighbours;
             line += step, ++seen) {
            const qsizetype found{folding::indentOf(buffer().line(line), width)};
            if (found >= 0) {
                return found;
            }
        }
        return qsizetype{0};
    }};
    return std::min(neighbour(-1), neighbour(1));
}

std::optional<folding::Fold> EditorView::caretBlock() const
{
    const QDeadlineTimer deadline{guideSearch};
    const qsizetype line{buffer().lineAt(m_cursor)};
    if (std::optional<folding::Fold> block{folding::indentedBlock(buffer(), line, indentWidth(), deadline)}) {
        return block;
    }
    const std::optional<qsizetype> above{folding::lineAbove(buffer(), line, indentWidth(), deadline)};
    std::optional<folding::Fold> block{
        above ? folding::indentedBlock(buffer(), *above, indentWidth(), deadline) : std::nullopt
    };
    if (!block || block->last < line) {
        return std::nullopt;
    }
    return block;
}

void EditorView::layoutGuides()
{
    // A line at each indentation level a row is indented past, in the middle
    // of the level's first column, down its visual rows.
    QList<QRectF> guides;
    QList<QRectF> scope;
    if (showIndentGuides() && !m_rows.empty()) {
        const qsizetype width{indentWidth()};
        const std::optional<folding::Fold> block{caretBlock()};
        const qsizetype scopeColumn{block ? folding::indentOf(buffer().line(block->first - 1), width) : qsizetype{-1}};
        for (const Row &row : m_rows) {
            const qsizetype indent{guideIndent(row)};
            const qreal height{static_cast<qreal>(row.layout->lineCount()) * m_lineHeight};
            const bool inBlock{block && row.line >= block->first && row.line <= block->last};
            for (qsizetype column{0}; column < indent; column += width) {
                const qreal x{std::round((static_cast<qreal>(column) + 0.5) * m_charWidth)};
                (inBlock && column == scopeColumn ? scope : guides).append({x, row.top, 1.0, height});
            }
        }
    }
    if (guides != m_guideRects || scope != m_scopeGuideRects) {
        m_guideRects = std::move(guides);
        m_scopeGuideRects = std::move(scope);
        m_flags |= GuidesChanged;
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
    if (const std::optional<matching::BracketPair> pair{bracketPair(buffer(), highlighter(), m_cursor)}) {
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
    layoutGuides();
    layoutDiagnostics();
    layoutLink();
    layoutMarks();
    layoutFoldMarks();
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
        auto caretLayer{std::make_unique<QSGNode>()};
        auto gutter{std::make_unique<QSGTransformNode>()};
        created->currentLine = currentLine.get();
        created->textClip = clip.get();
        created->content = content.get();
        created->marksLayer = marksLayer.get();
        auto invisibles{makeRectanglesNode()};
        created->invisibles = invisibles.get();
        auto guides{makeRectanglesNode()};
        created->guides = guides.get();
        auto scopeGuides{makeRectanglesNode()};
        created->scopeGuides = scopeGuides.get();
        auto squiggles{makeColoredNode()};
        created->squiggles = squiggles.get();
        created->selectionLayer = selectionLayer.get();
        created->textLayer = textLayer.get();
        created->caret = caret.get();
        created->caretLayer = caretLayer.get();
        created->gutter = gutter.get();
        content->appendChildNode(marksLayer.release());
        content->appendChildNode(selectionLayer.release());
        content->appendChildNode(invisibles.release());
        content->appendChildNode(guides.release());
        content->appendChildNode(scopeGuides.release());
        content->appendChildNode(textLayer.release());
        content->appendChildNode(squiggles.release());
        content->appendChildNode(caretLayer.release());
        content->appendChildNode(caret.release());
        clip->appendChildNode(content.release());
        created->appendChildNode(currentLine.release());
        created->appendChildNode(clip.release());
        created->appendChildNode(gutter.release());
        node = created.release();
        // A new window (the view moved to another one) starts without nodes.
        m_flags |= InvisiblesChanged | GuidesChanged | DiagnosticsChanged;
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
            {gutter - padding - foldColumnWidth() - number.width, y}
        );
    }
    removeText(node->gutter, node->numbers);
    node->numbers = std::move(numbers);
    placeFoldMarks(window, node);

    if ((m_flags & InvisiblesChanged) != 0U) {
        setRectangles(node->invisibles, m_invisibleRects, lineNumberColor());
        m_flags &= ~InvisiblesChanged;
    }
    if ((m_flags & GuidesChanged) != 0U) {
        setRectangles(node->guides, m_guideRects, guideColor());
        setRectangles(node->scopeGuides, m_scopeGuideRects, scopeColor());
        m_flags &= ~GuidesChanged;
    }
    if ((m_flags & DiagnosticsChanged) != 0U) {
        fillSquiggles(node->squiggles);
        m_flags &= ~DiagnosticsChanged;
    }
    syncRectangles(window, node->marksLayer, node->matches, m_matchRects, matchColor());
    syncRectangles(window, node->marksLayer, node->words, m_wordRects, wordColor());
    syncRectangles(window, node->marksLayer, node->brackets, m_bracketRects, bracketColor());
    syncRectangles(window, node->selectionLayer, node->selection, m_selectionRects, selectionColor());

    const bool caretShown{m_caretRect.isValid()};
    const bool dropping{m_dropPosition >= 0};
    const bool blinkedOff{(m_flags & CaretOff) != 0U};
    node->caret->setRect(
        caretShown && (dropping || (hasActiveFocus() && m_preedit.isEmpty() && !blinkedOff)) ? m_caretRect : QRectF{}
    );
    node->caret->setColor(caretColor());
    const bool caretsShown{hasActiveFocus() && m_preedit.isEmpty() && !blinkedOff};
    syncRectangles(
        window,
        node->caretLayer,
        node->otherCarets,
        caretsShown ? m_otherCaretRects : QList<QRectF>{},
        caretColor()
    );
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

// --- folding -----------------------------------------------------------------

std::optional<folding::Fold> EditorView::foldUnder(qsizetype line)
{
    if (!highlighter().isActive()) {
        return std::nullopt;
    }
    const QDeadlineTimer deadline{foldSearch};
    if (highlighter().definition().indentationBasedFoldingEnabled()) {
        return folding::indentedBlock(buffer(), line, indentWidth(), deadline);
    }
    if (!highlighter().advance(line, deadline)) {
        return std::nullopt;
    }
    // The line that closes it stays shown.
    const std::optional<qsizetype> end{highlighter().regionEnd(line, deadline)};
    if (!end || *end - 1 < line + 1) {
        return std::nullopt;
    }
    return folding::Fold{.first = line + 1, .last = *end - 1};
}

std::optional<folding::Fold> EditorView::foldAround(qsizetype line)
{
    if (!highlighter().isActive()) {
        return std::nullopt;
    }
    const QDeadlineTimer deadline{foldSearch};
    std::optional<qsizetype> above;
    if (highlighter().definition().indentationBasedFoldingEnabled()) {
        above = folding::lineAbove(buffer(), line, indentWidth(), deadline);
    } else if (highlighter().advance(line, deadline)) {
        above = highlighter().regionAbove(line, deadline);
    }
    const std::optional<folding::Fold> fold{above ? foldUnder(*above) : std::nullopt};
    if (!fold || fold->last + 1 < line) {
        return std::nullopt;
    }
    return fold;
}

void EditorView::fold()
{
    const qsizetype line{buffer().lineAt(m_cursor)};
    std::optional<folding::Fold> fold{m_folds.foldedUnder(line) ? std::nullopt : foldUnder(line)};
    if (!fold) {
        fold = foldAround(line);
    }
    if (fold) {
        applyFold(*fold);
    }
}

void EditorView::unfold()
{
    if (m_folds.unfold(buffer().lineAt(m_cursor))) {
        foldsChanged();
    }
}

void EditorView::unfoldAll()
{
    if (!m_folds.isEmpty()) {
        m_folds.clear();
        foldsChanged();
    }
}

void EditorView::applyFold(folding::Fold fold)
{
    m_folds.fold(fold);
    const auto hidden{[&fold, this](qsizetype position) {
        const qsizetype line{buffer().lineAt(position)};
        return line >= fold.first && line <= fold.last;
    }};
    m_others.removeIf([&hidden](const TextDocument::Caret &caret) { return hidden(caret.cursor); });
    // A caret inside goes to the folded line.
    if (hidden(m_cursor)) {
        const qsizetype line{fold.first - 1};
        moveCaret(buffer().lineStart(line) + buffer().lineLength(line), Select::Collapse);
    }
    foldsChanged();
}

void EditorView::toggleFold(qsizetype line)
{
    if (m_folds.unfold(line)) {
        foldsChanged();
    } else if (const std::optional<folding::Fold> fold{foldUnder(line)}) {
        applyFold(*fold);
    }
}

void EditorView::foldsChanged()
{
    emit contentSizeChanged();
    emit cursorChanged();
    emit selectionChanged();
    polish();
}

std::optional<qsizetype> EditorView::foldTargetAt(const QPointF &point) const
{
    const qreal gutter{gutterWidth()};
    if (point.x() < gutter && point.x() >= gutter - padding - foldColumnWidth()) {
        const auto row{static_cast<qsizetype>(std::floor((point.y() + m_contentY - topPadding) / m_lineHeight))};
        const qsizetype line{lineAtRow(std::max(row, qsizetype{0}))};
        const bool marked{std::ranges::any_of(m_foldMarks, [line](const FoldMark &mark) {
            return mark.key / 2 == line;
        })};
        return marked ? std::optional{line} : std::nullopt;
    }
    const QPointF at{point.x() + m_contentX - textLeft(), point.y() + m_contentY - m_baseTop};
    const auto placeholder{std::ranges::find_if(m_placeholders, [&at](const Placeholder &shown) {
        return shown.rect.contains(at);
    })};
    return placeholder != m_placeholders.end() ? std::optional{placeholder->line} : std::nullopt;
}

qsizetype EditorView::skipFolded(qsizetype position, bool forward) const
{
    const std::optional<folding::Fold> fold{m_folds.hiding(buffer().lineAt(position))};
    if (!fold) {
        return position;
    }
    if (forward) {
        return fold->last + 1 < buffer().lineCount() ? buffer().lineStart(fold->last + 1) : buffer().length();
    }
    const qsizetype line{fold->first - 1};
    return buffer().lineStart(line) + buffer().lineLength(line);
}

void EditorView::layoutFoldMarks()
{
    // A marker by each line a block starts on (and each folded one), and a
    // placeholder after each folded one.
    m_foldMarks.clear();
    m_placeholders.clear();
    const bool active{highlighter().isActive()};
    if (!active && m_folds.isEmpty()) {
        return;
    }
    if (m_foldableGlyph == nullptr) {
        m_foldableGlyph = layoutText(QStringLiteral("\u25BE"), -1);
        m_foldedGlyph = layoutText(QStringLiteral("\u25B8"), -1);
        m_placeholderGlyph = layoutText(QStringLiteral("\u22EF"), -1);
    }
    const bool indented{active && highlighter().definition().indentationBasedFoldingEnabled()};
    const qreal placeholderWidth{m_placeholderGlyph->lineAt(0).naturalTextWidth() + m_charWidth};
    for (const Row &row : m_rows) {
        const bool folded{m_folds.foldedUnder(row.line)};
        const bool opens{indented ? folding::opensIndentedBlock(buffer(), row.line, indentWidth()) : row.opens > 0};
        if (!folded && !(active && opens)) {
            continue;
        }
        m_foldMarks.append({.key = (row.line * 2) + (folded ? 1 : 0), .top = row.top});
        if (folded) {
            const QTextLine last{row.layout->lineAt(row.layout->lineCount() - 1)};
            const qreal top{row.top + (static_cast<qreal>(last.lineNumber()) * m_lineHeight)};
            m_placeholders.append(
                {.rect = {last.naturalTextWidth() + (m_charWidth / 2.0), top, placeholderWidth, m_lineHeight},
                 .line = row.line}
            );
        }
    }
}

void EditorView::placeFoldMarks(QQuickWindow *window, EditorNode *node)
{
    if ((m_flags & FoldGlyphsChanged) != 0U) {
        removeText(node->gutter, node->foldMarks);
        removeText(node->textLayer, node->placeholders);
        m_flags &= ~FoldGlyphsChanged;
    }
    const qreal column{foldColumnWidth()};
    const qreal left{gutterWidth() - (padding / 2.0) - column};
    const QColor color{lineNumberColor()};
    QHash<qsizetype, Shown> marks;
    for (const FoldMark &mark : std::as_const(m_foldMarks)) {
        QTextLayout *const glyph{mark.key % 2 == 1 ? m_foldedGlyph.get() : m_foldableGlyph.get()};
        const qreal x{left + ((column - glyph->lineAt(0).naturalTextWidth()) / 2.0)};
        placeText(window, node->gutter, node->foldMarks, marks, mark.key, glyph, color, {x, mark.top});
    }
    removeText(node->gutter, node->foldMarks);
    node->foldMarks = std::move(marks);

    QHash<qsizetype, Shown> placeholders;
    QList<QRectF> backgrounds;
    backgrounds.reserve(m_placeholders.size());
    for (const Placeholder &placeholder : std::as_const(m_placeholders)) {
        const QPointF at{placeholder.rect.x() + (m_charWidth / 2.0), placeholder.rect.y()};
        placeText(
            window,
            node->textLayer,
            node->placeholders,
            placeholders,
            placeholder.line,
            m_placeholderGlyph.get(),
            color,
            at
        );
        backgrounds.append(placeholder.rect.adjusted(0.0, 2.0, 0.0, -2.0));
    }
    removeText(node->textLayer, node->placeholders);
    node->placeholders = std::move(placeholders);
    syncRectangles(window, node->marksLayer, node->placeholderBacks, backgrounds, wordColor());
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
            moveCaret(skipFolded(wordBoundary(m_cursor, false), false), select);
        } else {
            moveCaret(collapsing ? selectionStart() : skipFolded(m_cursor - 1, false), select);
        }
        return true;
    case Qt::Key_Right:
        if (command) {
            moveCaret(lineEnd, select);
        } else if (option) {
            moveCaret(skipFolded(wordBoundary(m_cursor, true), true), select);
        } else {
            moveCaret(collapsing ? selectionEnd() : skipFolded(m_cursor + 1, true), select);
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
    const Qt::KeyboardModifiers modifiers{event->modifiers()};
    const bool command{modifiers.testFlag(Qt::ControlModifier)};
    const bool option{modifiers.testFlag(Qt::AltModifier)};
    if (!m_others.isEmpty()) {
        // At each caret: its selection, else as far as the key goes.
        replaceEach(
            [this, &backspace, &command, &option](const TextDocument::Caret &caret) {
                if (caret.cursor != caret.anchor) {
                    return selectionOf(caret);
                }
                const qsizetype target{deletionTarget(caret.cursor, backspace, command, option)};
                const qsizetype start{std::min(caret.cursor, target)};
                return TextDocument::Range{.position = start, .count = std::max(caret.cursor, target) - start};
            },
            {},
            !command && !option
        );
        return true;
    }
    if (hasSelection()) {
        deleteSelection();
        return true;
    }
    const qsizetype target{deletionTarget(m_cursor, backspace, command, option)};
    // Backspace between an empty pair, "(|)", takes both.
    if (backspace && !command && !option && !m_document->syntax().isEmpty() && m_cursor > 0 &&
        m_cursor < buffer().length()) {
        const QString around{buffer().text(m_cursor - 1, 2)};
        const qsizetype opener{QStringView{u"([{\"'`"}.indexOf(around.front())};
        if (opener >= 0 && around.back() == QStringView{u")]}\"'`"}.at(opener)) {
            replace(m_cursor - 1, 2, {}, false);
            return true;
        }
    }
    const qsizetype from{std::min(m_cursor, target)};
    const qsizetype to{std::max(m_cursor, target)};
    if (from != to) {
        replace(from, to - from, {}, !command && !option);
    }
    return true;
}

// ⌘ to the line's start or end, ⌥ to the word's, otherwise one character.
qsizetype EditorView::deletionTarget(qsizetype cursor, bool backspace, bool command, bool option) const
{
    const qsizetype line{buffer().lineAt(cursor)};
    const qsizetype lineStart{buffer().lineStart(line)};
    if (command) {
        return backspace ? lineStart : lineStart + buffer().lineLength(line);
    }
    if (option) {
        return wordBoundary(cursor, !backspace);
    }
    return std::clamp(cursor + (backspace ? -1 : 1), qsizetype{0}, buffer().length());
}

// With several carets plain ← and → move them all; any other move leaves
// one (moveCaret()).
bool EditorView::handleMoveEach(const QKeyEvent *event)
{
    // On macOS, ControlModifier is ⌘, AltModifier ⌥ and MetaModifier ⌃.
    const Qt::KeyboardModifiers modifiers{event->modifiers()};
    const bool command{modifiers.testFlag(Qt::ControlModifier)};
    const bool option{modifiers.testFlag(Qt::AltModifier)};
    const bool shift{modifiers.testFlag(Qt::ShiftModifier)};
    if (modifiers.testFlag(Qt::MetaModifier) || (command && option)) {
        return false;
    }
    const qint32 key{event->key()};
    if ((key == Qt::Key_Up || key == Qt::Key_Down) && option) {
        addCaretVertically(key == Qt::Key_Down ? 1 : -1, shift);
        return true;
    }
    if (m_others.isEmpty()) {
        return false;
    }
    if (key == Qt::Key_Home || key == Qt::Key_End) {
        moveEach({.forward = key == Qt::Key_End, .word = false, .line = true, .extend = shift});
        return true;
    }
    if (key != Qt::Key_Left && key != Qt::Key_Right) {
        return false;
    }
    moveEach({.forward = key == Qt::Key_Right, .word = option, .line = command, .extend = shift});
    return true;
}

void EditorView::keyPressEvent(QKeyEvent *event)
{
    // ⌘ (Qt's Control on macOS) pressed over a word: it becomes a link.
    if (event->key() == Qt::Key_Control) {
        updateLink(event->modifiers());
    }
    if (handleMoveEach(event) || handleNavigation(event) || handleDeletion(event)) {
        return;
    }
    // Escape leaves only the main caret.
    if (event->key() == Qt::Key_Escape && clearOthers()) {
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
        if (!typePair(text)) {
            insert(text);
        }
        if (m_others.isEmpty()) {
            alignClosing();
        }
        emit textTyped(text);
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
        if (!typePair(commit)) {
            insert(commit);
        }
        emit textTyped(commit);
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
    if (event->button() == Qt::RightButton) {
        pressForContextMenu(event->position());
        event->accept();
        return;
    }
    if (const std::optional<qsizetype> folded{foldTargetAt(event->position())}) {
        toggleFold(*folded);
        event->accept();
        return;
    }
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
    // ⌘ (Qt's Control on macOS): go to the definition, as in Xcode and VS Code.
    if (event->modifiers() == Qt::ControlModifier) {
        moveCaret(position, Select::Collapse);
        emit definitionRequested(position);
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
    if (!event->buttons().testFlag(Qt::LeftButton)) {
        event->accept();
        return;
    }
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
    if (event->button() != Qt::LeftButton) {
        event->accept();
        return;
    }
    // A click on the selection that did not become a drag places the caret.
    if ((m_flags & DragPending) != 0U) {
        m_flags &= ~DragPending;
        moveCaret(positionAt(event->position()), Select::Collapse);
    }
    event->accept();
}

void EditorView::pressForContextMenu(const QPointF &point)
{
    const qsizetype position{positionAt(point)};
    if (!hasSelection() || position < selectionStart() || position >= selectionEnd()) {
        moveCaret(position, Select::Collapse);
    }
    // Queued: a native menu runs its own event loop, not inside this press.
    QMetaObject::invokeMethod(this, &EditorView::contextMenuRequested, Qt::QueuedConnection);
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
    if (event->button() != Qt::LeftButton) {
        event->accept();
        return;
    }
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
    restartBlink();
}

void EditorView::focusOutEvent(QFocusEvent *event)
{
    QQuickItem::focusOutEvent(event);
    restartBlink();
}

void EditorView::restartBlink()
{
    m_flags &= ~CaretOff;
    // The system's period (a whole on-off cycle); 0: it does not blink.
    const qint32 period{QGuiApplication::styleHints()->cursorFlashTime()};
    if (hasActiveFocus() && period > 0) {
        m_blinkTimer.start(period / 2);
    } else {
        m_blinkTimer.stop();
    }
    update();
}

} // namespace kode
