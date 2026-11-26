#pragma once

#include <QAccessible>
#include <QAccessibleObject>

namespace kode
{

class EditorView;

// What VoiceOver (and other assistive technologies) see of an EditorView:
// an editable, multi-line text with its caret and selection, read by
// character, word or line, and editable through the same undoable path as
// typing. Installed as a factory the first time an EditorView is created.
class EditorAccessible
    : public QAccessibleObject
    , public QAccessibleTextInterface
    , public QAccessibleEditableTextInterface
{
public:
    explicit EditorAccessible(EditorView *view);

    static void installFactory();

    // QAccessibleInterface
    [[nodiscard]] QWindow *window() const override;
    [[nodiscard]] QAccessibleInterface *parent() const override;
    [[nodiscard]] QAccessibleInterface *child(int index) const override;
    [[nodiscard]] int childCount() const override;
    [[nodiscard]] int indexOfChild(const QAccessibleInterface *child) const override;
    [[nodiscard]] QString text(QAccessible::Text type) const override;
    [[nodiscard]] QRect rect() const override;
    [[nodiscard]] QAccessible::Role role() const override;
    [[nodiscard]] QAccessible::State state() const override;
    void *interface_cast(QAccessible::InterfaceType type) override;

    // QAccessibleTextInterface
    void selection(int selectionIndex, int *startOffset, int *endOffset) const override;
    [[nodiscard]] int selectionCount() const override;
    void addSelection(int startOffset, int endOffset) override;
    void removeSelection(int selectionIndex) override;
    void setSelection(int selectionIndex, int startOffset, int endOffset) override;
    [[nodiscard]] int cursorPosition() const override;
    void setCursorPosition(int position) override;
    [[nodiscard]] QString text(int startOffset, int endOffset) const override;
    [[nodiscard]] QString
    textBeforeOffset(int offset, QAccessible::TextBoundaryType boundaryType, int *startOffset, int *endOffset)
        const override;
    [[nodiscard]] QString
    textAfterOffset(int offset, QAccessible::TextBoundaryType boundaryType, int *startOffset, int *endOffset)
        const override;
    [[nodiscard]] QString
    textAtOffset(int offset, QAccessible::TextBoundaryType boundaryType, int *startOffset, int *endOffset)
        const override;
    [[nodiscard]] int characterCount() const override;
    [[nodiscard]] QRect characterRect(int offset) const override;
    [[nodiscard]] int offsetAtPoint(const QPoint &point) const override;
    void scrollToSubstring(int startIndex, int endIndex) override;
    [[nodiscard]] QString attributes(int offset, int *startOffset, int *endOffset) const override;

    // QAccessibleEditableTextInterface
    void deleteText(int startOffset, int endOffset) override;
    void insertText(int offset, const QString &text) override;
    void replaceText(int startOffset, int endOffset, const QString &text) override;

private:
    // The unit of `boundaryType` holding `offset`: [start, end).
    struct Range {
        qsizetype start{0};
        qsizetype end{0};
    };
    [[nodiscard]] EditorView *view() const;
    [[nodiscard]] Range unitAt(qsizetype offset, QAccessible::TextBoundaryType boundaryType) const;
    [[nodiscard]] QString textOf(Range range, int *startOffset, int *endOffset) const;
};

} // namespace kode
