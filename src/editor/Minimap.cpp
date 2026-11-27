#include "Minimap.h"

#include "EditorView.h"
#include "SyntaxHighlighter.h"
#include "TextBuffer.h"
#include "TextDocument.h"

#include <QDeadlineTimer>
#include <QHash>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGRectangleNode>
#include <QSGVertexColorMaterial>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <numeric>
#include <span>
#include <utility>

namespace kode
{

class MinimapNode : public QSGNode
{
public:
    QSGRectangleNode *slider{nullptr};
    // Each chunk's transform; its one child draws the blocks.
    QHash<qsizetype, QSGTransformNode *> chunks;
};

namespace
{

// Each line's row, and its blocks' height and column width. Highlighting is
// the view's: it runs far enough ahead of it to cover the lines shown here.
constexpr qreal linePitch{2.0};
constexpr qreal blockHeight{1.5};
constexpr qreal columnWidth{1.0};
constexpr qreal leftMargin{4.0};
constexpr qsizetype chunkLines{64};
constexpr std::chrono::milliseconds fillSlice{2};

constexpr qsizetype chunkOf(qsizetype line) noexcept
{
    return line / chunkLines;
}

std::unique_ptr<QSGTransformNode> makeChunk()
{
    auto transform{std::make_unique<QSGTransformNode>()};
    auto blocks{std::make_unique<QSGGeometryNode>()};
    auto geometry{std::make_unique<QSGGeometry>(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0)};
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    blocks->setGeometry(geometry.release());
    blocks->setFlag(QSGNode::OwnsGeometry);
    auto material{std::make_unique<QSGVertexColorMaterial>()};
    blocks->setMaterial(material.release());
    blocks->setFlag(QSGNode::OwnsMaterial);
    transform->appendChildNode(blocks.release());
    return transform;
}

} // namespace

Minimap::Minimap(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    m_fillTimer.setSingleShot(true);
    m_fillTimer.setInterval(0);
    connect(&m_fillTimer, &QTimer::timeout, this, &QQuickItem::polish);
    connect(this, &QQuickItem::visibleChanged, this, &QQuickItem::polish);
    connect(this, &QQuickItem::heightChanged, this, &QQuickItem::polish);
    connect(this, &QQuickItem::widthChanged, this, &Minimap::invalidate);
}

Minimap::~Minimap() = default;

void Minimap::setView(EditorView *view)
{
    if (m_view == view) {
        return;
    }
    if (m_view != nullptr) {
        disconnect(m_view, nullptr, this, nullptr);
    }
    m_view = view;
    if (view != nullptr) {
        connect(view, &EditorView::contentYChanged, this, &QQuickItem::polish);
        connect(view, &EditorView::contentSizeChanged, this, &QQuickItem::polish);
        connect(view, &QQuickItem::heightChanged, this, &QQuickItem::polish);
        connect(view, &EditorView::documentChanged, this, &Minimap::attach);
        connect(view, &EditorView::highlightingAdvanced, this, &Minimap::highlightingAdvanced);
        connect(view, &EditorView::colorsChanged, this, [this] {
            if (m_view->textColor().rgba() != m_textColor) {
                invalidate();
            }
        });
    }
    attach();
    emit viewChanged();
}

void Minimap::setSliderColor(const QColor &color)
{
    if (m_sliderColor == color.rgba()) {
        return;
    }
    m_sliderColor = color.rgba();
    emit colorsChanged();
    update();
}

void Minimap::setSliderActiveColor(const QColor &color)
{
    if (m_sliderActiveColor == color.rgba()) {
        return;
    }
    m_sliderActiveColor = color.rgba();
    emit colorsChanged();
    update();
}

const TextBuffer &Minimap::buffer() const
{
    return m_document->buffer();
}

void Minimap::attach()
{
    if (m_document != nullptr) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_document = m_view != nullptr ? m_view->document() : nullptr;
    m_lines.clear();
    m_dirtyFrom = 0;
    if (m_document != nullptr) {
        connect(m_document, &TextDocument::replaced, this, &Minimap::documentReplaced);
        connect(m_document, &TextDocument::reset, this, [this] {
            m_lines.clear();
            m_dirtyFrom = 0;
            polish();
        });
        connect(m_document, &TextDocument::highlightingChanged, this, &Minimap::invalidate);
        connect(m_document, &TextDocument::indentationChanged, this, &Minimap::invalidate);
    }
    polish();
}

void Minimap::documentReplaced(qsizetype position, const QString &removed, const QString &inserted)
{
    const qsizetype line{buffer().lineAt(position)};
    const qsizetype removedLines{removed.count(QLatin1Char('\n'))};
    const qsizetype insertedLines{inserted.count(QLatin1Char('\n'))};
    const qsizetype start{line - m_linesFirst};
    const qsizetype end{start + removedLines + 1};
    if (end <= 0) {
        m_linesFirst += insertedLines - removedLines;
    } else if (start < 0) {
        m_lines.clear();
    } else if (start < m_lines.size()) {
        m_lines.remove(start, std::min(end, m_lines.size()) - start);
        m_lines.insert(start, insertedLines + 1, Line{});
    }
    if (insertedLines != removedLines) {
        m_dirtyFrom = std::min(m_dirtyFrom, chunkOf(line));
    }
    polish();
}

void Minimap::invalidate()
{
    for (Line &line : m_lines) {
        line.kind = Missing;
    }
    polish();
}

qreal Minimap::firstShown() const
{
    const qreal capacity{height() / linePitch};
    const auto lines{static_cast<qreal>(buffer().lineCount())};
    if (lines <= capacity) {
        return 0.0;
    }
    const qreal range{scrollRange()};
    const qreal fraction{range > 0.0 ? std::clamp(m_view->contentY() / range, 0.0, 1.0) : 0.0};
    return fraction * (lines - capacity);
}

qreal Minimap::scrollRange() const
{
    return std::max(0.0, m_view->contentHeight() - m_view->height());
}

qreal Minimap::sliderTravel() const
{
    const qreal shown{std::min(height(), static_cast<qreal>(buffer().lineCount()) * linePitch)};
    return shown - m_slider.height();
}

void Minimap::updatePolish()
{
    if (m_view == nullptr || m_document == nullptr || !isVisible()) {
        return;
    }
    m_textColor = m_view->textColor().rgba();
    m_first = firstShown();
    const auto first{static_cast<qsizetype>(std::floor(m_first))};
    const auto shown{static_cast<qsizetype>(std::ceil(height() / linePitch)) + 1};
    keepLines(first, std::min(first + shown, buffer().lineCount()));
    const qreal top{(static_cast<qreal>(m_view->topLine()) - m_first) * linePitch};
    const qreal bottom{(static_cast<qreal>(m_view->bottomLine() + 1) - m_first) * linePitch};
    m_slider = QRectF{0.0, top, width(), bottom - top};
    if (!fillLines()) {
        m_fillTimer.start();
    }
    update();
}

void Minimap::keepLines(qsizetype first, qsizetype end)
{
    const qsizetype keptEnd{m_linesFirst + m_lines.size()};
    if (end <= m_linesFirst || first >= keptEnd) {
        m_lines.clear();
    } else if (first > m_linesFirst) {
        m_lines.remove(0, first - m_linesFirst);
    } else if (first < m_linesFirst) {
        m_lines.insert(0, m_linesFirst - first, Line{});
    }
    m_linesFirst = first;
    m_lines.resize(std::max(end - first, qsizetype{0}));
}

bool Minimap::stale(const Line &line, qsizetype index) const
{
    const SyntaxHighlighter &highlighter{m_document->highlighter()};
    switch (line.kind) {
    case Missing:
        return true;
    case Plain:
        return highlighter.isReady(index);
    default:
        return highlighter.isReady(index) && highlighter.startState(index) != line.state;
    }
}

bool Minimap::fillLines()
{
    const QDeadlineTimer deadline{fillSlice};
    m_plainFrom = -1;
    for (qsizetype i{0}; i < m_lines.size(); ++i) {
        const qsizetype index{m_linesFirst + i};
        if (stale(m_lines.at(i), index)) {
            if (deadline.hasExpired()) {
                return false;
            }
            m_lines[i] = measure(index);
            m_dirtyChunks.insert(chunkOf(index));
        }
        if (m_plainFrom < 0 && m_lines.at(i).kind == Plain) {
            m_plainFrom = index;
        }
    }
    return true;
}

void Minimap::highlightingAdvanced()
{
    if (m_plainFrom >= 0 && m_document != nullptr && m_document->highlighter().isReady(m_plainFrom)) {
        polish();
    }
}

Minimap::Line Minimap::measure(qsizetype index) const
{
    SyntaxHighlighter &highlighter{m_document->highlighter()};
    const QString text{buffer().line(index)};
    Line line;
    QList<QTextLayout::FormatRange> formats;
    if (highlighter.isReady(index)) {
        line.kind = Highlighted;
        line.state = highlighter.startState(index);
        formats = highlighter.formats(index, text);
    } else {
        line.kind = Plain;
    }
    const auto columns{static_cast<qsizetype>(std::floor((width() - leftMargin) / columnWidth))};
    appendBlocks(line.blocks, text, formats, m_textColor, m_document->indentWidth(), columns);
    return line;
}

void Minimap::appendBlocks(
    QList<Block> &blocks,
    QStringView text,
    const QList<QTextLayout::FormatRange> &formats,
    QRgb plain,
    qsizetype tabWidth,
    qsizetype columns
)
{
    qsizetype column{0};
    qsizetype format{-1};
    QRgb formatColor{plain};
    for (qsizetype i{0}; i < text.size() && column < columns; ++i) {
        const QChar character{text.at(i)};
        if (character == QLatin1Char('\t')) {
            column += tabWidth - (column % tabWidth);
            continue;
        }
        if (character.isSpace()) {
            ++column;
            continue;
        }
        while (format + 1 < formats.size() && formats.at(format + 1).start <= i) {
            ++format;
            const QBrush foreground{formats.at(format).format.foreground()};
            formatColor = foreground.style() == Qt::NoBrush ? plain : foreground.color().rgba();
        }
        const bool inFormat{format >= 0 && i < formats.at(format).start + formats.at(format).length};
        const QRgb color{inFormat ? formatColor : plain};
        if (!blocks.isEmpty() && blocks.last().column + blocks.last().length == column &&
            blocks.last().color == color) {
            ++blocks.last().length;
        } else {
            blocks.append({.column = static_cast<qint32>(column), .length = 1, .color = color});
        }
        ++column;
    }
}

QSGNode *Minimap::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData * /*data*/)
{
    auto *node{static_cast<MinimapNode *>(oldNode)};
    if (node == nullptr) {
        auto created{std::make_unique<MinimapNode>()};
        std::unique_ptr<QSGRectangleNode> slider{window()->createRectangleNode()};
        created->slider = slider.get();
        created->appendChildNode(slider.release());
        node = created.release();
    }
    node->slider->setRect(m_slider);
    node->slider->setColor(
        QColor::fromRgba((m_flags & (Hovered | Dragging)) != 0U ? m_sliderActiveColor : m_sliderColor)
    );
    syncChunks(node);
    m_dirtyChunks.clear();
    m_dirtyFrom = std::numeric_limits<qsizetype>::max();
    return node;
}

void Minimap::syncChunks(MinimapNode *node)
{
    const qsizetype first{chunkOf(m_linesFirst)};
    const qsizetype last{m_lines.isEmpty() ? first - 1 : chunkOf(m_linesFirst + m_lines.size() - 1)};
    QHash<qsizetype, QSGTransformNode *> previous{std::exchange(node->chunks, {})};
    for (qsizetype chunk{first}; chunk <= last; ++chunk) {
        QSGTransformNode *transform{previous.take(chunk)};
        bool fill{chunk >= m_dirtyFrom || m_dirtyChunks.contains(chunk)};
        if (transform == nullptr) {
            auto made{makeChunk()};
            transform = made.get();
            node->appendChildNode(made.release());
            fill = true;
        }
        node->chunks.insert(chunk, transform);
        if (fill) {
            fillChunk(transform, chunk);
        }
        QMatrix4x4 matrix;
        matrix.translate(0.0F, static_cast<float>((static_cast<qreal>(chunk * chunkLines) - m_first) * linePitch));
        if (transform->matrix() != matrix) {
            transform->setMatrix(matrix);
        }
    }
    for (QSGTransformNode *const left : std::as_const(previous)) {
        // Removed from its parent, which owned it: ours to delete.
        const std::unique_ptr<QSGTransformNode> removed{left};
        node->removeChildNode(removed.get());
    }
}

void Minimap::fillChunk(QSGNode *transform, qsizetype chunk) const
{
    auto *const blocks{static_cast<QSGGeometryNode *>(transform->firstChild())};
    const qsizetype top{chunk * chunkLines};
    const qsizetype from{std::max(top, m_linesFirst) - m_linesFirst};
    const qsizetype to{std::min(top + chunkLines, m_linesFirst + m_lines.size()) - m_linesFirst};
    const auto lines{std::span{m_lines}.subspan(static_cast<std::size_t>(from), static_cast<std::size_t>(to - from))};
    const qsizetype
        count{std::accumulate(lines.begin(), lines.end(), qsizetype{0}, [](qsizetype sum, const Line &line) {
            return sum + line.blocks.size();
        })};
    QSGGeometry *const geometry{blocks->geometry()};
    geometry->allocate(static_cast<int>(count * 6));
    const std::span vertices{geometry->vertexDataAsColoredPoint2D(), static_cast<std::size_t>(count * 6)};
    std::size_t vertex{0};
    for (qsizetype i{from}; i < to; ++i) {
        const auto y{static_cast<float>(static_cast<qreal>(m_linesFirst + i - top) * linePitch)};
        const auto bottom{y + static_cast<float>(blockHeight)};
        for (const Block &block : m_lines.at(i).blocks) {
            const auto left{static_cast<float>(leftMargin + (block.column * columnWidth))};
            const auto right{left + static_cast<float>(block.length * columnWidth)};
            // Premultiplied, as the vertex color material expects.
            const qint32 alpha{qAlpha(block.color)};
            const auto red{static_cast<uchar>(qRed(block.color) * alpha / 255)};
            const auto green{static_cast<uchar>(qGreen(block.color) * alpha / 255)};
            const auto blue{static_cast<uchar>(qBlue(block.color) * alpha / 255)};
            const auto opacity{static_cast<uchar>(alpha)};
            for (const auto &[x, corner] :
                 {std::pair{left, y}, {right, y}, {left, bottom}, {right, y}, {right, bottom}, {left, bottom}}) {
                vertices[vertex++].set(x, corner, red, green, blue, opacity);
            }
        }
    }
    blocks->markDirty(QSGNode::DirtyGeometry);
}

void Minimap::mousePressEvent(QMouseEvent *event)
{
    if (m_view == nullptr || m_document == nullptr || event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    const qreal y{event->position().y()};
    m_dragY = y;
    if (y >= m_slider.top() && y < m_slider.bottom()) {
        m_dragContentY = m_view->contentY();
    } else {
        const qsizetype line{std::clamp(
            static_cast<qsizetype>(std::floor(m_first + (y / linePitch))),
            qsizetype{0},
            buffer().lineCount() - 1
        )};
        const qreal centered{m_view->lineTop(line) + ((m_view->m_lineHeight - m_view->height()) / 2.0)};
        m_dragContentY = std::clamp(centered, 0.0, scrollRange());
        emit scrollRequested(m_dragContentY);
    }
    m_flags |= Dragging;
    update();
    event->accept();
}

void Minimap::mouseMoveEvent(QMouseEvent *event)
{
    if ((m_flags & Dragging) == 0U || m_view == nullptr) {
        return;
    }
    const qreal travel{sliderTravel()};
    if (travel <= 0.0) {
        return;
    }
    const qreal range{scrollRange()};
    const qreal moved{(event->position().y() - m_dragY) * range / travel};
    emit scrollRequested(std::clamp(m_dragContentY + moved, 0.0, range));
}

void Minimap::mouseReleaseEvent(QMouseEvent * /*event*/)
{
    m_flags &= ~Dragging;
    update();
}

void Minimap::hoverEnterEvent(QHoverEvent * /*event*/)
{
    m_flags |= Hovered;
    update();
}

void Minimap::hoverLeaveEvent(QHoverEvent * /*event*/)
{
    m_flags &= ~Hovered;
    update();
}

} // namespace kode
