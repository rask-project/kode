import QtQuick
import QtQuick.Controls
import Rask.Platform

// The editing area: an EditorView filling a Flickable's viewport, with
// scroll bars, keeping the caret in view and colored with the system colors.
Flickable {
    id: root

    readonly property alias editor: view
    // Behind the text; transparent lets the window's background and blur show.
    property color backgroundColor: "transparent"

    // Scrolls just enough to show `rect` (content coordinates); horizontally,
    // the gutter covers the left of the viewport.
    function ensureVisible(rect) {
        // The horizontal scroll bar covers the bottom of the viewport.
        const visibleHeight = height - (horizontalBar.visible ? horizontalBar.height : 0);
        if (contentY > rect.y)
            contentY = rect.y;
        else if (contentY + visibleHeight < rect.y + rect.height)
            contentY = rect.y + rect.height - visibleHeight;
        const left = contentX + view.gutterWidth;
        if (left > rect.x)
            contentX = Math.max(0, rect.x - view.gutterWidth - 24);
        else if (contentX + width < rect.x + rect.width)
            contentX = rect.x + rect.width - width + 24;
    }

    // Scrolls `rect` (content coordinates) to the middle of the view.
    function center(rect) {
        contentY = Math.max(0, Math.min(rect.y + rect.height / 2 - height / 2, contentHeight - height));
        ensureVisible(rect);
    }

    contentWidth: Math.max(width, view.contentWidth)
    contentHeight: Math.max(height, view.contentHeight)
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    // Scrolls by wheel and trackpad only: a mouse drag selects text.
    acceptedButtons: Qt.NoButton

    ScrollBar.vertical: OverlayScrollBar {
        id: verticalBar
    }

    ScrollBar.horizontal: OverlayScrollBar {
        id: horizontalBar
    }

    // Fills the viewport as the view does (both follow the scroll).
    Rectangle {
        x: root.contentX
        y: root.contentY
        width: root.width
        height: root.height
        visible: root.backgroundColor.a > 0
        color: root.backgroundColor
    }

    EditorView {
        id: view

        Accessible.role: Accessible.EditableText
        Accessible.name: qsTr("Editor")

        x: root.contentX
        y: root.contentY

        width: root.width
        height: root.height

        contentX: root.contentX
        contentY: root.contentY
        focus: true

        textColor: SystemColors.text
        selectionColor: Window.active ? SystemColors.selectedTextBackground : SystemColors.unemphasizedSelectedTextBackground
        caretColor: SystemColors.textInsertionPoint
        currentLineColor: Qt.alpha(SystemColors.text, 0.05)
        lineNumberColor: SystemColors.tertiaryLabel
        bracketColor: Qt.alpha(SystemColors.controlAccent, 0.35)
        wordColor: Qt.alpha(SystemColors.text, 0.1)
        matchColor: Qt.alpha(SystemColors.findHighlight, 0.45)

        onCaretMoved: root.ensureVisible(view.cursorRectangle)
        onScrollRequested: contentY => root.contentY = Math.max(0, Math.min(contentY, root.contentHeight - root.height))
    }
}
