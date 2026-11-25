import QtQuick
import QtQuick.Controls

// The editing area: an EditorView filling a Flickable's viewport, with
// scroll bars, keeping the caret in view and colored from the system palette.
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

    SystemPalette {
        id: systemPalette

        colorGroup: SystemPalette.Active
    }

    EditorView {
        id: view

        x: root.contentX
        y: root.contentY
        width: root.width
        height: root.height
        contentX: root.contentX
        contentY: root.contentY
        focus: true
        textColor: systemPalette.text
        selectionColor: systemPalette.highlight
        caretColor: systemPalette.accent
        currentLineColor: Qt.rgba(systemPalette.text.r, systemPalette.text.g, systemPalette.text.b, 0.05)
        lineNumberColor: Qt.rgba(systemPalette.text.r, systemPalette.text.g, systemPalette.text.b, 0.35)
        onCursorRectangleChanged: root.ensureVisible(cursorRectangle)
    }
}
