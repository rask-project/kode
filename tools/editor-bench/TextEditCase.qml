import QtQuick

// The Stage 1 editor: Qt Quick's TextEdit in a Flickable (no wrapping, to
// compare with EditorView, which does not wrap yet).
Flickable {
    id: root

    function setText(text) {
        edit.text = text;
    }

    function prepareTyping() {
        edit.cursorPosition = edit.length / 2;
        edit.forceActiveFocus();
    }

    function scrollTo(fraction) {
        contentY = Math.max(0, contentHeight - height) * fraction;
    }

    width: 900
    height: 700
    contentWidth: width
    contentHeight: edit.height
    clip: true

    TextEdit {
        id: edit

        width: root.width
        height: Math.max(implicitHeight, root.height)
        leftPadding: 16
        topPadding: 8
        font.family: "Menlo"
        font.pointSize: 13
        wrapMode: TextEdit.NoWrap
    }
}
