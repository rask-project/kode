#include "EditorView.h"

#include <QFile>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QQuickWindow>
#include <QSGRectangleNode>
#include <QSGTextNode>
#include <QStringDecoder>
#include <QTextLayout>
#include <QTextOption>

#include <algorithm>
#include <cmath>

namespace tekst
{

namespace
{

constexpr qreal padding{16.0};
constexpr qreal topPadding{8.0};
constexpr qreal caretWidth{2.0};
// Lines laid out above and below the view, so short scrolls need no layout.
constexpr qsizetype overscan{4};
// No wrapping yet: lines are laid out this wide.
constexpr qreal noWrapWidth{1.0e7};

// The scene graph side: one text node per row slot and the caret. The nodes
// are children of this node, which owns them.
class EditorNode : public QSGNode
{
public:
    QList<QSGTextNode *> rows;
    QSGRectangleNode *caret{nullptr};
};

} // namespace

EditorView::EditorView(QQuickItem *parent) : QQuickItem(parent), m_font(QStringLiteral("Menlo"), 13)
{
    setFlag(ItemHasContents);
    setFlag(ItemAcceptsInputMethod);
    setFlag(ItemIsFocusScope);
    setAcceptedMouseButtons(Qt::LeftButton);
    setActiveFocusOnTab(true);
    updateMetrics();
}

EditorView::~EditorView() = default;

void EditorView::setFont(const QFont &font)
{
    if (m_font == font) {
        return;
    }
    m_font = font;
    updateMetrics();
    m_rows.clear();
    emit fontChanged();
    emit contentHeightChanged();
    emit cursorChanged();
    polish();
}

void EditorView::setTextColor(const QColor &color)
{
    if (m_textColor == color.rgba()) {
        return;
    }
    m_textColor = color.rgba();
    emit textColorChanged();
    update();
}

void EditorView::setCaretColor(const QColor &color)
{
    if (m_caretColor == color.rgba()) {
        return;
    }
    m_caretColor = color.rgba();
    emit caretColorChanged();
    update();
}

void EditorView::setContentY(qreal contentY)
{
    if (qFuzzyCompare(m_contentY, contentY)) {
        return;
    }
    m_contentY = contentY;
    emit contentYChanged();
    polish();
}

qreal EditorView::contentHeight() const
{
    return topPadding + (static_cast<qreal>(m_buffer.lineCount()) * m_lineHeight) + padding;
}

void EditorView::setCursorPosition(qint64 position)
{
    moveCursor(position);
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
    const qsizetype column{m_cursor - m_buffer.lineStart(line)};
    return {
        padding + xForColumn(line, column),
        topPadding + (static_cast<qreal>(line) * m_lineHeight),
        caretWidth,
        m_lineHeight
    };
}

bool EditorView::load(const QUrl &fileUrl)
{
    QFile file{fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString()};
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QStringDecoder decoder{QStringDecoder::Utf8};
    QString text{decoder.decode(file.readAll())};
    if (decoder.hasError()) {
        return false;
    }
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    setText(text);
    return true;
}

void EditorView::setText(const QString &text)
{
    m_buffer = TextBuffer{text};
    m_cursor = 0;
    m_preferredX = -1.0;
    textEdited();
}

void EditorView::insert(const QString &text)
{
    m_buffer.insert(m_cursor, text);
    m_cursor += text.size();
    m_preferredX = -1.0;
    textEdited();
}

void EditorView::textEdited()
{
    ++m_revision;
    emit textChanged();
    emit contentHeightChanged();
    emit cursorChanged();
    polish();
}

void EditorView::updateMetrics()
{
    const QFontMetricsF metrics{m_font};
    m_lineHeight = std::ceil(metrics.lineSpacing());
    m_ascent = metrics.ascent();
    m_charWidth = metrics.horizontalAdvance(QLatin1Char('x'));
}

qsizetype EditorView::firstVisibleLine() const
{
    const auto line{static_cast<qsizetype>(std::floor((m_contentY - topPadding) / m_lineHeight))};
    return std::clamp(line - overscan, qsizetype{0}, m_buffer.lineCount() - 1);
}

qsizetype EditorView::visibleLineCount() const
{
    return static_cast<qsizetype>(std::ceil(height() / m_lineHeight)) + (2 * overscan) + 1;
}

std::unique_ptr<QTextLayout> EditorView::layoutLine(qsizetype line) const
{
    auto layout{std::make_unique<QTextLayout>(m_buffer.line(line), m_font)};
    QTextOption option;
    option.setWrapMode(QTextOption::NoWrap);
    option.setTabStopDistance(4 * m_charWidth);
    layout->setTextOption(option);
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

void EditorView::updatePolish()
{
    // Lay out the lines in view, keeping rows still valid (scrolling only
    // lays out the lines that came into view).
    const qsizetype first{firstVisibleLine()};
    const qsizetype count{std::min(visibleLineCount(), m_buffer.lineCount() - first)};
    std::vector<Row> rows;
    rows.reserve(static_cast<std::size_t>(count));
    for (qsizetype line{first}; line < first + count; ++line) {
        const auto reusable{std::ranges::find_if(m_rows, [this, line](const Row &row) {
            return row.line == line && row.revision == m_revision && row.layout != nullptr;
        })};
        if (reusable != m_rows.end()) {
            rows.push_back(std::move(*reusable));
        } else {
            rows.push_back({.line = line, .revision = m_revision, .layout = layoutLine(line)});
        }
    }
    m_rows = std::move(rows);
    update();
}

QSGNode *EditorView::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData * /*data*/)
{
    auto *node{static_cast<EditorNode *>(oldNode)};
    if (node == nullptr) {
        auto created{std::make_unique<EditorNode>()};
        auto caret{std::unique_ptr<QSGRectangleNode>{window()->createRectangleNode()}};
        created->caret = caret.get();
        created->appendChildNode(caret.release());
        node = created.release();
    }

    // As many text nodes as rows; the caret stays the last child, on top.
    while (node->rows.size() < static_cast<qsizetype>(m_rows.size())) {
        auto text{std::unique_ptr<QSGTextNode>{window()->createTextNode()}};
        text->setRenderType(QSGTextNode::NativeRendering);
        node->rows.append(text.get());
        node->insertChildNodeBefore(text.release(), node->caret);
    }
    while (node->rows.size() > static_cast<qsizetype>(m_rows.size())) {
        // Removed from its parent, which owned it: ours to delete.
        const std::unique_ptr<QSGTextNode> text{node->rows.takeLast()};
        node->removeChildNode(text.get());
    }

    const QColor color{textColor()};
    for (qsizetype i{0}; i < node->rows.size(); ++i) {
        const Row &row{m_rows.at(static_cast<std::size_t>(i))};
        QSGTextNode *const text{node->rows.at(i)};
        text->clear();
        text->setColor(color);
        const qreal y{topPadding + (static_cast<qreal>(row.line) * m_lineHeight) - m_contentY};
        text->addTextLayout({padding, y}, row.layout.get());
    }

    const QRectF caret{cursorRectangle().translated(0.0, -m_contentY)};
    node->caret->setRect(hasActiveFocus() ? caret : QRectF{});
    node->caret->setColor(caretColor());
    return node;
}

void EditorView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.height() != oldGeometry.height()) {
        polish();
    }
}

qreal EditorView::xForColumn(qsizetype line, qsizetype column) const
{
    const auto row{std::ranges::find_if(m_rows, [line, this](const Row &candidate) {
        return candidate.line == line && candidate.revision == m_revision;
    })};
    if (row != m_rows.end() && row->layout->lineCount() > 0) {
        return row->layout->lineAt(0).cursorToX(static_cast<int>(column));
    }
    return layoutLine(line)->lineAt(0).cursorToX(static_cast<int>(column));
}

qsizetype EditorView::columnForX(qsizetype line, qreal x) const
{
    const std::unique_ptr<QTextLayout> layout{layoutLine(line)};
    return layout->lineAt(0).xToCursor(x, QTextLine::CursorBetweenCharacters);
}

void EditorView::moveCursor(qsizetype position, bool keepColumn)
{
    position = std::clamp(position, qsizetype{0}, m_buffer.length());
    if (!keepColumn) {
        m_preferredX = -1.0;
    }
    if (position == m_cursor) {
        return;
    }
    m_cursor = position;
    emit cursorChanged();
    update();
}

void EditorView::moveVertically(qsizetype lines)
{
    const qsizetype line{m_buffer.lineAt(m_cursor)};
    const qsizetype target{std::clamp(line + lines, qsizetype{0}, m_buffer.lineCount() - 1)};
    if (m_preferredX < 0.0) {
        m_preferredX = xForColumn(line, m_cursor - m_buffer.lineStart(line));
    }
    moveCursor(m_buffer.lineStart(target) + columnForX(target, m_preferredX), true);
}

void EditorView::keyPressEvent(QKeyEvent *event)
{
    // On macOS, ControlModifier is ⌘ and MetaModifier is ⌃.
    const bool command{event->modifiers().testFlag(Qt::ControlModifier)};
    const qsizetype line{m_buffer.lineAt(m_cursor)};
    const qsizetype page{std::max(qsizetype{1}, static_cast<qsizetype>(height() / m_lineHeight) - 1)};

    switch (event->key()) {
    case Qt::Key_Left:
        moveCursor(command ? m_buffer.lineStart(line) : m_cursor - 1);
        return;
    case Qt::Key_Right:
        moveCursor(command ? m_buffer.lineStart(line) + m_buffer.lineLength(line) : m_cursor + 1);
        return;
    case Qt::Key_Up:
        if (command) {
            moveCursor(0);
        } else {
            moveVertically(-1);
        }
        return;
    case Qt::Key_Down:
        if (command) {
            moveCursor(m_buffer.length());
        } else {
            moveVertically(1);
        }
        return;
    case Qt::Key_Home:
        moveCursor(m_buffer.lineStart(line));
        return;
    case Qt::Key_End:
        moveCursor(m_buffer.lineStart(line) + m_buffer.lineLength(line));
        return;
    case Qt::Key_PageUp:
        moveVertically(-page);
        return;
    case Qt::Key_PageDown:
        moveVertically(page);
        return;
    case Qt::Key_Backspace:
        if (m_cursor > 0) {
            m_buffer.remove(m_cursor - 1, 1);
            --m_cursor;
            m_preferredX = -1.0;
            textEdited();
        }
        return;
    case Qt::Key_Delete:
        if (m_cursor < m_buffer.length()) {
            m_buffer.remove(m_cursor, 1);
            m_preferredX = -1.0;
            textEdited();
        }
        return;
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
    const bool printable{!text.isEmpty() && !text.front().isNonCharacter() && text.front().isPrint()};
    if (printable && !command && !event->modifiers().testFlag(Qt::MetaModifier)) {
        insert(text);
        return;
    }
    event->ignore();
}

void EditorView::inputMethodEvent(QInputMethodEvent *event)
{
    // Prototype: committed text only (the preedit is not shown yet).
    if (!event->commitString().isEmpty()) {
        insert(event->commitString());
    }
    event->accept();
}

QVariant EditorView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImCursorRectangle:
        return cursorRectangle().translated(0.0, -m_contentY);
    case Qt::ImCursorPosition:
        return cursorColumn();
    case Qt::ImSurroundingText:
        return m_buffer.line(m_buffer.lineAt(m_cursor));
    default:
        return QQuickItem::inputMethodQuery(query);
    }
}

void EditorView::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    const QPointF point{event->position()};
    const auto line{static_cast<qsizetype>(std::floor((point.y() + m_contentY - topPadding) / m_lineHeight))};
    const qsizetype target{std::clamp(line, qsizetype{0}, m_buffer.lineCount() - 1)};
    moveCursor(m_buffer.lineStart(target) + columnForX(target, point.x() - padding));
    event->accept();
}

} // namespace tekst
