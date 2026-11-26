#include "EditorAccessible.h"

#include "EditorView.h"

#include <QQuickWindow>

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>

namespace kode
{

namespace
{

QAccessibleInterface *createAccessible(const QString & /*key*/, QObject *object)
{
    auto *const view{qobject_cast<EditorView *>(object)};
    if (view == nullptr) {
        return nullptr;
    }
    // Qt's accessibility cache owns the interface.
    return std::make_unique<EditorAccessible>(view).release();
}

} // namespace

EditorAccessible::EditorAccessible(EditorView *view) : QAccessibleObject(view) {}

void EditorAccessible::installFactory()
{
    // After Qt Quick's own factory (installed with the first window), so this
    // one is asked first for EditorViews.
    static std::once_flag installed;
    std::call_once(installed, [] { QAccessible::installFactory(&createAccessible); });
}

EditorView *EditorAccessible::view() const
{
    return static_cast<EditorView *>(object());
}

QWindow *EditorAccessible::window() const
{
    return view()->window();
}

QAccessibleInterface *EditorAccessible::parent() const
{
    // The nearest accessible ancestor item, else the window.
    for (QQuickItem *item{view()->parentItem()}; item != nullptr; item = item->parentItem()) {
        if (QAccessibleInterface *const interface{QAccessible::queryAccessibleInterface(item)}; interface != nullptr) {
            return interface;
        }
    }
    return view()->window() != nullptr ? QAccessible::queryAccessibleInterface(view()->window()) : nullptr;
}

QAccessibleInterface *EditorAccessible::child(int /*index*/) const
{
    return nullptr;
}

int EditorAccessible::childCount() const
{
    return 0;
}

int EditorAccessible::indexOfChild(const QAccessibleInterface * /*child*/) const
{
    return -1;
}

QString EditorAccessible::text(QAccessible::Text type) const
{
    switch (type) {
    case QAccessible::Name:
        return EditorView::tr("Editor");
    case QAccessible::Value:
        return view()->text();
    default:
        return {};
    }
}

QRect EditorAccessible::rect() const
{
    const EditorView *const editor{view()};
    if (editor->window() == nullptr) {
        return {};
    }
    return {editor->mapToGlobal(QPointF{0.0, 0.0}).toPoint(), editor->size().toSize()};
}

QAccessible::Role EditorAccessible::role() const
{
    return QAccessible::EditableText;
}

QAccessible::State EditorAccessible::state() const
{
    QAccessible::State state;
    state.focusable = true;
    state.focused = view()->hasActiveFocus();
    state.editable = true;
    state.multiLine = true;
    state.selectableText = true;
    return state;
}

void *EditorAccessible::interface_cast(QAccessible::InterfaceType type)
{
    if (type == QAccessible::TextInterface) {
        return static_cast<QAccessibleTextInterface *>(this);
    }
    if (type == QAccessible::EditableTextInterface) {
        return static_cast<QAccessibleEditableTextInterface *>(this);
    }
    return QAccessibleObject::interface_cast(type);
}

void EditorAccessible::selection(int selectionIndex, int *startOffset, int *endOffset) const
{
    const bool selected{selectionIndex == 0 && view()->hasSelection()};
    *startOffset = selected ? static_cast<int>(view()->selectionStart()) : 0;
    *endOffset = selected ? static_cast<int>(view()->selectionEnd()) : 0;
}

int EditorAccessible::selectionCount() const
{
    return view()->hasSelection() ? 1 : 0;
}

void EditorAccessible::addSelection(int startOffset, int endOffset)
{
    view()->select(startOffset, endOffset);
}

void EditorAccessible::removeSelection(int selectionIndex)
{
    if (selectionIndex == 0) {
        view()->setCursorPosition(view()->cursorPosition());
    }
}

void EditorAccessible::setSelection(int selectionIndex, int startOffset, int endOffset)
{
    if (selectionIndex == 0) {
        view()->select(startOffset, endOffset);
    }
}

int EditorAccessible::cursorPosition() const
{
    return static_cast<int>(view()->cursorPosition());
}

void EditorAccessible::setCursorPosition(int position)
{
    view()->setCursorPosition(position);
}

QString EditorAccessible::text(int startOffset, int endOffset) const
{
    return view()->buffer().text(startOffset, endOffset - startOffset);
}

EditorAccessible::Range EditorAccessible::unitAt(qsizetype offset, QAccessible::TextBoundaryType boundaryType) const
{
    const TextBuffer &buffer{view()->buffer()};
    const qsizetype length{buffer.length()};
    offset = std::clamp(offset, qsizetype{0}, length);
    switch (boundaryType) {
    case QAccessible::CharBoundary:
        return {.start = offset, .end = std::min(offset + 1, length)};
    case QAccessible::WordBoundary: {
        const qsizetype start{view()->wordBoundary(std::min(offset + 1, length), false)};
        return {.start = start, .end = std::max(view()->wordBoundary(start, true), offset)};
    }
    case QAccessible::NoBoundary:
        return {.start = 0, .end = length};
    case QAccessible::SentenceBoundary:
    case QAccessible::ParagraphBoundary:
    case QAccessible::LineBoundary:
    default: {
        // A line with its newline.
        const qsizetype line{buffer.lineAt(offset)};
        const qsizetype next{line + 1 < buffer.lineCount() ? buffer.lineStart(line + 1) : length};
        return {.start = buffer.lineStart(line), .end = next};
    }
    }
}

QString EditorAccessible::textOf(Range range, int *startOffset, int *endOffset) const
{
    *startOffset = static_cast<int>(range.start);
    *endOffset = static_cast<int>(range.end);
    return view()->buffer().text(range.start, range.end - range.start);
}

QString
EditorAccessible::textAtOffset(int offset, QAccessible::TextBoundaryType boundaryType, int *startOffset, int *endOffset)
    const
{
    return textOf(unitAt(offset, boundaryType), startOffset, endOffset);
}

QString EditorAccessible::textBeforeOffset(
    int offset,
    QAccessible::TextBoundaryType boundaryType,
    int *startOffset,
    int *endOffset
) const
{
    const Range at{unitAt(offset, boundaryType)};
    if (at.start == 0) {
        return textOf({}, startOffset, endOffset);
    }
    return textOf(unitAt(at.start - 1, boundaryType), startOffset, endOffset);
}

QString EditorAccessible::textAfterOffset(
    int offset,
    QAccessible::TextBoundaryType boundaryType,
    int *startOffset,
    int *endOffset
) const
{
    const Range at{unitAt(offset, boundaryType)};
    const qsizetype length{view()->buffer().length()};
    if (at.end >= length) {
        return textOf({.start = length, .end = length}, startOffset, endOffset);
    }
    return textOf(unitAt(at.end, boundaryType), startOffset, endOffset);
}

int EditorAccessible::characterCount() const
{
    return static_cast<int>(view()->length());
}

QRect EditorAccessible::characterRect(int offset) const
{
    const EditorView *const editor{view()};
    if (editor->window() == nullptr) {
        return {};
    }
    const QRectF here{editor->positionRect(offset)};
    const QRectF next{editor->positionRect(std::min(qsizetype{offset} + 1, editor->buffer().length()))};
    // The next character's x, when it is on the same visual line.
    const qreal width{
        qFuzzyCompare(next.y(), here.y()) && next.x() > here.x() ? next.x() - here.x() : editor->m_charWidth
    };
    const QPointF topLeft{editor->mapToGlobal(here.topLeft() - QPointF{editor->m_contentX, editor->m_contentY})};
    return QRectF{topLeft, QSizeF{width, editor->m_lineHeight}}.toRect();
}

int EditorAccessible::offsetAtPoint(const QPoint &point) const
{
    return static_cast<int>(view()->positionAt(view()->mapFromGlobal(QPointF{point})));
}

void EditorAccessible::scrollToSubstring(int startIndex, int /*endIndex*/)
{
    view()->setCursorPosition(startIndex);
}

QString EditorAccessible::attributes(int offset, int *startOffset, int *endOffset) const
{
    *startOffset = offset;
    *endOffset = offset;
    return {};
}

void EditorAccessible::deleteText(int startOffset, int endOffset)
{
    view()->replace(startOffset, endOffset - startOffset, {}, false);
}

void EditorAccessible::insertText(int offset, const QString &text)
{
    view()->replace(offset, 0, text, false);
}

void EditorAccessible::replaceText(int startOffset, int endOffset, const QString &text)
{
    view()->replace(startOffset, endOffset - startOffset, text, false);
}

} // namespace kode
