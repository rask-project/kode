import QtQuick
import QtQuick.Controls
import Rask.Platform

// The editing area: an EditorView filling a Flickable's viewport, with
// scroll bars, keeping the caret in view and colored with the system colors.
Flickable {
    id: root

    readonly property alias editor: view

    // Scrolls just enough to show `rect` (content coordinates); horizontally,
    // the gutter covers the left of the viewport.
    function ensureVisible(rect) {
        if (contentY > rect.y)
            contentY = rect.y;
        else if (contentY + height < rect.y + rect.height)
            contentY = rect.y + rect.height - height;
        const left = contentX + view.gutterWidth;
        if (left > rect.x)
            contentX = Math.max(0, rect.x - view.gutterWidth - 24);
        else if (contentX + width < rect.x + rect.width)
            contentX = rect.x + rect.width - width + 24;
    }

    contentWidth: Math.max(width, view.contentWidth)
    contentHeight: Math.max(height, view.contentHeight)
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    ScrollBar.vertical: ScrollBar {}
    ScrollBar.horizontal: ScrollBar {}

    EditorView {
        id: view

        x: root.contentX
        y: root.contentY
        width: root.width
        height: root.height
        contentX: root.contentX
        contentY: root.contentY
        focus: true
        Accessible.role: Accessible.EditableText
        Accessible.name: qsTr("Editor")
        textColor: SystemColors.text
        selectionColor: Window.active ? SystemColors.selectedTextBackground : SystemColors.unemphasizedSelectedTextBackground
        caretColor: SystemColors.textInsertionPoint
        currentLineColor: Qt.alpha(SystemColors.text, 0.05)
        lineNumberColor: SystemColors.tertiaryLabel
        onCursorRectangleChanged: root.ensureVisible(cursorRectangle)
    }
}
