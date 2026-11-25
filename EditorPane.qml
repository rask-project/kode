pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import Rask.Application
import Rask.Controls
import Rask.Dialogs
import Rask.Platform

import Tekst

// One document in a tab: its editor, find bar and status bar, and a header
// (close button and name, dragged to move the pane) while the tab is split.
// Made by TabManager in the engine's root context, it moves between windows
// with its document: nothing here refers to the window it is in. What needs
// the window (saving, asking about unsaved changes) goes there by signals.
Item {
    id: pane

    // The file to open when the pane is made (a restored session, Open…).
    property url initialFile
    // Shows the header: the tab holds more than one pane.
    property bool split: false
    property bool active: false

    readonly property EditorView editor: textEditor.editor
    readonly property Document document: fileDocument
    readonly property url fileUrl: fileDocument.fileUrl
    readonly property string title: fileDocument.fileName
    readonly property bool modified: fileDocument.modified
    // A new document nobody typed in: opening a file replaces it.
    readonly property bool untouched: fileDocument.fileUrl.toString() === "" && !fileDocument.modified && textEditor.editor.length === 0
    readonly property bool dragging: headerDrag.dragging

    // The pane wants focus (a click, the editor focused).
    signal activated
    // Run `action` once this pane's unsaved changes are saved or discarded.
    signal reviewRequested(var action)
    signal closeRequested
    signal splitRequested(int orientation)
    // Dragging the header; positions are global, `grabX`/`grabY` in the header.
    signal dragStarted(real grabX, real grabY, real headerWidth)
    signal dragMoved(real globalX, real globalY)
    signal dragFinished(real globalX, real globalY)
    signal dragCanceled

    // Opens `file` here; false (and an alert) if it cannot be read.
    function open(file: url): bool {
        if (!fileDocument.load(file))
            return false;
        RecentDocuments.add(file);
        return true;
    }

    function focusEditor() {
        textEditor.editor.forceActiveFocus();
    }

    // Shows the find bar (with the replace row when `replacing`), focused.
    function showSearch(replacing: bool) {
        searchBar.replacing = replacing;
        searchBar.visible = true;
        searchBar.focusFind();
    }

    function useSelectionForFind() {
        searchBar.text = textEditor.editor.selectedText();
    }

    function showGoToLine() {
        goToLine.open();
    }

    Component.onCompleted: {
        if (pane.initialFile.toString() !== "")
            fileDocument.load(pane.initialFile);
    }

    SystemPalette {
        id: systemPalette

        colorGroup: SystemPalette.Active
    }

    Document {
        id: fileDocument

        editor: textEditor.editor
        onErrorOccurred: message => {
            errorAlert.informativeText = message;
            errorAlert.open();
        }
        onChangedOnDisk: changedAlert.open()
    }

    Alert {
        id: errorAlert

        window: pane.Window.window
        style: Alert.Critical
        text: qsTr("The document could not be opened or saved.")
    }

    Alert {
        id: changedAlert

        window: pane.Window.window
        style: Alert.Warning
        text: qsTr("“%1” changed on disk.").arg(fileDocument.fileName)
        informativeText: qsTr("Reload it and lose your changes here, or keep editing your version?")
        buttons: [qsTr("Reload"), qsTr("Keep Mine")]
        onFinished: button => {
            if (button === 0)
                fileDocument.reload();
        }
    }

    // While split: the header, a pill like a tab, on top.
    Rectangle {
        id: header

        anchors.top: parent.top
        anchors.topMargin: 5
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width - 10
        height: pane.split ? 28 : 0
        visible: pane.split
        radius: height / 2
        color: Qt.alpha(systemPalette.window, 0.5)
        border.width: 1
        border.color: pane.active ? SystemColors.controlAccent : Qt.alpha(SystemColors.label, 0.15)
        opacity: headerDrag.dragging ? 0.45 : 1

        // A click activates the pane; dragging moves it beside another pane,
        // into a tab strip or to a new window.
        MouseArea {
            id: headerDrag

            property real pressX: 0
            property real pressY: 0
            property bool dragging: false

            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            preventStealing: true
            cursorShape: dragging ? Qt.ClosedHandCursor : Qt.ArrowCursor

            onPressed: mouse => {
                pressX = mouse.x;
                pressY = mouse.y;
                dragging = false;
            }
            onPositionChanged: mouse => {
                if (!dragging) {
                    if (Math.hypot(mouse.x - pressX, mouse.y - pressY) < Application.styleHints.startDragDistance)
                        return;
                    dragging = true;
                    pane.activated();
                    pane.dragStarted(pressX, pressY, header.width);
                }
                const point = headerDrag.mapToGlobal(mouse.x, mouse.y);
                pane.dragMoved(point.x, point.y);
            }
            onReleased: mouse => {
                if (!dragging) {
                    pane.activated();
                    pane.focusEditor();
                    return;
                }
                dragging = false;
                const point = headerDrag.mapToGlobal(mouse.x, mouse.y);
                pane.dragFinished(point.x, point.y);
            }
            onCanceled: {
                if (!dragging)
                    return;
                dragging = false;
                pane.dragCanceled();
            }
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: {
                pane.activated();
                headerMenu.popup();
            }
        }

        Menu {
            id: headerMenu

            popupType: Popup.Native

            MenuItem {
                text: qsTr("Close")
                onTriggered: pane.closeRequested()
            }
            MenuItem {
                text: qsTr("Split Horizontally")
                onTriggered: pane.splitRequested(Qt.Horizontal)
            }
            MenuItem {
                text: qsTr("Split Vertically")
                onTriggered: pane.splitRequested(Qt.Vertical)
            }
        }

        ToolButton {
            id: closeButton

            anchors.left: parent.left
            anchors.leftMargin: 3
            anchors.verticalCenter: parent.verticalCenter
            width: 22
            height: 22
            focusPolicy: Qt.NoFocus
            Accessible.name: qsTr("Close")
            onClicked: pane.closeRequested()

            contentItem: Item {
                // The unsaved-changes dot until the pointer is on it, then ×.
                Rectangle {
                    anchors.centerIn: parent
                    width: 8
                    height: 8
                    radius: 4
                    visible: pane.modified && !closeButton.hovered
                    color: SystemColors.label
                    opacity: 0.7
                }

                Repeater {
                    model: [45, -45]

                    Rectangle {
                        required property int modelData

                        anchors.centerIn: parent
                        width: 10
                        height: 1.5
                        radius: 0.75
                        rotation: modelData
                        visible: !pane.modified || closeButton.hovered
                        color: SystemColors.label
                        opacity: 0.7
                    }
                }
            }
        }

        Label {
            anchors.left: closeButton.right
            anchors.leftMargin: 6
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: fileDocument.fileName
            color: SystemColors.label
            elide: Text.ElideMiddle
        }
    }

    SearchBar {
        id: searchBar

        anchors.top: header.bottom
        anchors.topMargin: pane.split ? 5 : 0
        anchors.left: parent.left
        anchors.right: parent.right
        height: visible ? implicitHeight : 0
        visible: false
        matchCount: textEditor.editor.searchMatchCount
        currentMatch: textEditor.editor.searchCurrent
        complete: textEditor.editor.searchComplete
        error: textEditor.editor.searchError
        onNext: textEditor.editor.findNext()
        onPrevious: textEditor.editor.findPrevious()
        onReplace: textEditor.editor.replaceCurrent(replacementText)
        onReplaceAll: textEditor.editor.replaceAll(replacementText)
        onDone: {
            visible = false;
            pane.focusEditor();
        }
    }

    GoToLine {
        id: goToLine

        x: (pane.width - width) / 2
        y: searchBar.y + searchBar.height + 8
        onAccepted: target => {
            if (textEditor.editor.goToLine(target)) {
                goToLine.close();
                textEditor.center(textEditor.editor.cursorRectangle);
            }
        }
        onClosed: pane.focusEditor()
    }

    // The editor searches for what the bar holds while it shows.
    Binding {
        target: textEditor.editor
        property: "searchPattern"
        value: searchBar.visible ? searchBar.text : ""
    }
    Binding {
        target: textEditor.editor
        property: "searchCaseSensitive"
        value: searchBar.caseSensitive
    }
    Binding {
        target: textEditor.editor
        property: "searchRegularExpression"
        value: searchBar.regularExpression
    }

    TextEditor {
        id: textEditor

        anchors.top: searchBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusBar.top
        editor.syntaxTheme: Syntaxes.themeName(Preferences.theme, Appearance.dark)
        editor.wrap: Preferences.wrap
        editor.showInvisibles: Preferences.showInvisibles
        editor.font: Preferences.font
        editor.defaultIndentWithTabs: Preferences.indentWithTabs
        editor.defaultIndentWidth: Preferences.indentWidth
        editor.onActiveFocusChanged: {
            if (textEditor.editor.activeFocus)
                pane.activated();
        }
    }

    StatusBar {
        id: statusBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        editor: textEditor.editor
        document: fileDocument
        onReopenRequested: encoding => pane.reviewRequested(() => fileDocument.reopen(encoding))
    }
}
