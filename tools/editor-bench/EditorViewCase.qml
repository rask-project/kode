import QtQuick

// EditorView in a Flickable, kept in view as it scrolls, highlighting C++;
// with the minimap beside it when showMinimap.
Flickable {
    id: root

    property bool showMinimap: false

    function setText(text) {
        editorView.setText(text);
    }

    function prepareTyping() {
        editorView.cursorPosition = editorView.length / 2;
        editorView.forceActiveFocus();
    }

    function scrollTo(fraction) {
        contentY = Math.max(0, contentHeight - height) * fraction;
    }

    function scrollBy(pixels) {
        contentY = Math.max(0, Math.min(contentY + pixels, contentHeight - height));
    }

    width: 900
    height: 700
    contentWidth: Math.max(width, editorView.contentWidth)
    contentHeight: editorView.contentHeight
    clip: true

    EditorView {
        id: editorView

        x: root.contentX
        y: root.contentY
        width: root.width - (minimap.visible ? minimap.width : 0)
        height: root.height
        contentX: root.contentX
        contentY: root.contentY
        textColor: "black"
        syntax: "C++"
        font.family: "Menlo"
        font.pointSize: 13
        onCursorRectangleChanged: {
            const rect = cursorRectangle;
            if (root.contentY > rect.y)
                root.contentY = rect.y;
            else if (root.contentY + root.height < rect.y + rect.height)
                root.contentY = rect.y + rect.height - root.height;
        }
    }

    Minimap {
        id: minimap

        x: root.contentX + root.width - width
        y: root.contentY
        width: 100
        height: root.height
        visible: root.showMinimap
        clip: true
        view: editorView
        sliderColor: Qt.rgba(0, 0, 0, 0.06)
        onScrollRequested: contentY => root.contentY = contentY
    }
}
