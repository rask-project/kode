import QtQuick

// The prototype: EditorView in a Flickable, kept in view as it scrolls.
Flickable {
    id: root

    function setText(text) {
        view.setText(text);
    }

    function prepareTyping() {
        view.cursorPosition = view.length / 2;
        view.forceActiveFocus();
    }

    function scrollTo(fraction) {
        contentY = Math.max(0, contentHeight - height) * fraction;
    }

    width: 900
    height: 700
    contentWidth: width
    contentHeight: view.contentHeight
    clip: true

    EditorView {
        id: view

        y: root.contentY
        width: root.width
        height: root.height
        contentY: root.contentY
        textColor: "black"
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
}
