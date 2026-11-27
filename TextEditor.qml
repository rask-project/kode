pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Rask.Controls
import Rask.Platform

// The editing area: an EditorView filling a Flickable's viewport, with
// scroll bars, keeping the caret in view and colored with the system colors.
Flickable {
    id: root

    readonly property alias editor: editorView
    readonly property alias completion: completionModel
    // The document shown, for its language server's completions.
    property Document document: null
    // Behind the text; transparent lets the window's background and blur show.
    property color backgroundColor: "transparent"
    property bool showMinimap: false

    // Scrolls just enough to show `rect` (content coordinates); horizontally,
    // the gutter covers the left of the viewport.
    function ensureVisible(rect) {
        // The horizontal scroll bar covers the bottom of the viewport.
        const visibleHeight = height - (horizontalBar.visible ? horizontalBar.height : 0);
        if (contentY > rect.y)
            contentY = rect.y;
        else if (contentY + visibleHeight < rect.y + rect.height)
            contentY = rect.y + rect.height - visibleHeight;
        const left = contentX + editorView.gutterWidth;
        if (left > rect.x)
            contentX = Math.max(0, rect.x - editorView.gutterWidth - 24);
        else if (contentX + editorView.width < rect.x + rect.width)
            contentX = rect.x + rect.width - editorView.width + 24;
    }

    // Scrolls `rect` (content coordinates) to the middle of the view.
    function center(rect) {
        contentY = Math.max(0, Math.min(rect.y + rect.height / 2 - height / 2, contentHeight - height));
        ensureVisible(rect);
    }

    contentWidth: Math.max(width, editorView.contentWidth + (minimap.visible ? minimap.width : 0))
    contentHeight: Math.max(height, editorView.contentHeight)
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
        id: editorView

        Accessible.role: Accessible.EditableText
        Accessible.name: qsTr("Editor")

        x: root.contentX
        y: root.contentY

        width: root.width - (minimap.visible ? minimap.width : 0)
        height: root.height

        contentX: root.contentX
        contentY: root.contentY
        focus: true

        textColor: SystemColors.text
        // The accent color throughout, as the sidebar's selection: lighter when the window is inactive.
        selectionColor: Qt.alpha(SystemColors.controlAccent, Window.active ? 0.4 : 0.2)
        caretColor: SystemColors.textInsertionPoint
        currentLineColor: Qt.alpha(SystemColors.text, 0.05)
        lineNumberColor: SystemColors.tertiaryLabel
        bracketColor: Qt.alpha(SystemColors.controlAccent, 0.35)
        guideColor: Qt.alpha(SystemColors.text, 0.12)
        scopeColor: Qt.alpha(SystemColors.controlAccent, 0.75)
        wordColor: Qt.alpha(SystemColors.text, 0.1)
        matchColor: Qt.alpha(SystemColors.findHighlight, 0.45)
        // The system's red, orange and blue.
        errorColor: Appearance.dark ? "#ff453a" : "#ff3b30"
        warningColor: Appearance.dark ? "#ff9f0a" : "#ff9500"
        informationColor: Appearance.dark ? "#0a84ff" : "#007aff"
        hintColor: SystemColors.tertiaryLabel

        // While the completion list shows, the keys that move in it and choose.
        Keys.priority: Keys.BeforeItem
        Keys.onPressed: event => {
            event.accepted = false;
            if (!completionModel.active)
                return;
            switch (event.key) {
            case Qt.Key_Up:
                completionModel.move(-1);
                break;
            case Qt.Key_Down:
                completionModel.move(1);
                break;
            case Qt.Key_PageUp:
                completionModel.move(-completionList.rows);
                break;
            case Qt.Key_PageDown:
                completionModel.move(completionList.rows);
                break;
            case Qt.Key_Return:
            case Qt.Key_Enter:
            case Qt.Key_Tab:
                completionModel.accept();
                break;
            case Qt.Key_Escape:
                completionModel.cancel();
                break;
            default:
                return;
            }
            event.accepted = true;
        }

        onCaretMoved: root.ensureVisible(editorView.cursorRectangle)
        onScrollRequested: contentY => root.contentY = Math.max(0, Math.min(contentY, root.contentHeight - root.height))
    }

    // A diagnostic's message where the pointer rests on it.
    Repeater {
        model: editorView.diagnosticAreas

        delegate: Item {
            required property var modelData

            x: modelData.x
            y: modelData.y
            width: modelData.width
            height: modelData.height

            HelpTag {
                text: parent.modelData.message
                delay: 500
            }
        }
    }

    Completion {
        id: completionModel

        editor: editorView
        document: root.document
        servers: OpenDocuments.languageServers
    }

    // The completions under the caret (above it when there is no room below).
    Rectangle {
        id: completionList

        readonly property int rowHeight: 22
        readonly property int rows: 9
        readonly property real below: editorView.cursorRectangle.y + editorView.cursorRectangle.height

        visible: completionModel.active
        z: 2
        x: Math.max(root.contentX + editorView.gutterWidth, Math.min(editorView.cursorRectangle.x - 30, root.contentX + root.width - width - 8))
        y: below + height + 4 <= root.contentY + root.height ? below + 2 : editorView.cursorRectangle.y - height - 2
        width: 460
        height: Math.min(completionView.count, rows) * rowHeight + 8
        radius: 8
        color: Appearance.dark ? "#2c2c2e" : "#ffffff"
        border.width: 1
        border.color: Qt.alpha(SystemColors.label, 0.15)

        ListView {
            id: completionView

            anchors.fill: parent
            anchors.margins: 4
            clip: true
            model: completionModel
            currentIndex: completionModel.currentIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds

            delegate: Item {
                id: completionRow

                required property int index
                required property string label
                required property string detail
                required property int kind

                // The protocol's CompletionItemKind (1 Text … 25 TypeParameter): a letter and a hue.
                readonly property string letter: ["", "T", "M", "F", "C", "P", "V", "C", "I", "M", "P", "U", "V", "E", "K", "S", "C", "F", "R", "F", "E", "C", "S", "E", "O", "T"][kind] ?? "?"
                readonly property color hue: [2, 3, 4].includes(kind) ? "#5e5ce6" : [5, 6, 10].includes(kind) ? "#30b0c7" : [7, 8, 22, 25].includes(kind) ? "#bf5af2" : [13, 20, 21].includes(kind) ? "#ff9f0a" : kind === 14 ? "#ff375f" : "#8e8e93"
                readonly property bool current: index === completionModel.currentIndex

                width: ListView.view.width
                height: completionList.rowHeight

                Rectangle {
                    anchors.fill: parent
                    radius: 5
                    visible: completionRow.current
                    color: SystemColors.controlAccent
                }

                Rectangle {
                    id: badge

                    anchors.left: parent.left
                    anchors.leftMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    radius: 4
                    color: completionRow.hue

                    Text {
                        anchors.centerIn: parent
                        text: completionRow.letter
                        color: "white"
                        font.pixelSize: 10
                        font.bold: true
                    }
                }

                Text {
                    id: completionLabel

                    anchors.left: badge.right
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, parent.width - badge.width - 20)
                    elide: Text.ElideRight
                    text: completionRow.label
                    font.family: editorView.font.family
                    font.pixelSize: 12
                    color: completionRow.current ? "white" : SystemColors.label
                }

                Text {
                    anchors.left: completionLabel.right
                    anchors.leftMargin: 12
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                    text: completionRow.detail
                    font.pixelSize: 11
                    color: completionRow.current ? Qt.alpha("white", 0.75) : SystemColors.secondaryLabel
                }

                TapHandler {
                    onTapped: {
                        completionModel.currentIndex = completionRow.index;
                        completionModel.accept();
                    }
                }
            }
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
        sliderColor: Qt.alpha(SystemColors.text, 0.06)
        sliderActiveColor: Qt.alpha(SystemColors.text, 0.12)

        onScrollRequested: contentY => root.contentY = Math.max(0, Math.min(contentY, root.contentHeight - root.height))
    }
}
