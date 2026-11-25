pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

import Rask.Dialogs
import Rask.Platform
import Rask.Window

import Tekst

ApplicationWindow {
    id: root

    // A file given on the command line, opened at start.
    required property url initialFile
    required property font editorFont
    // Run once the unsaved changes are saved or discarded.
    property var pendingAction: null
    property bool closeApproved: false

    // Saves, asking for a file when the document has none, then runs `then`.
    function save(then) {
        if (document.fileUrl.toString() === "") {
            saveDialog.then = then;
            saveDialog.open();
        } else if (document.save() && then) {
            then();
        }
    }

    // Runs `action` now, or after the user saves or discards the changes.
    function confirmThen(action) {
        if (!document.modified) {
            action();
            return;
        }
        root.pendingAction = action;
        saveAlert.open();
    }

    width: 900
    height: 640
    minimumWidth: 400
    minimumHeight: 240
    visible: true
    // Feeds the Window menu and the window switcher; the title bar is drawn below.
    title: document.fileName + " — Tekst"
    flags: Qt.Window | Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint
    color: "transparent"
    // ApplicationWindow pads its content by the safe area; the title bar draws there itself.
    topPadding: 0

    onClosing: close => {
        if (root.closeApproved || !document.modified)
            return;
        close.accepted = false;
        root.confirmThen(() => {
            root.closeApproved = true;
            root.close();
        });
    }

    Component.onCompleted: {
        if (root.initialFile.toString() !== "")
            document.load(root.initialFile);
        editor.forceActiveFocus();
    }

    NativeWindow {
        id: nativeWindow

        window: root
        color: Appearance.dark ? "#1e1e1e" : "#fbfbfb"
        alpha: 0.96
        unifiedToolbar: root.visibility !== Window.FullScreen
    }

    Document {
        id: document

        textDocument: editor.textDocument
        onLoaded: {
            editor.cursorPosition = 0;
            flickable.contentY = 0;
        }
        onErrorOccurred: message => {
            errorAlert.informativeText = message;
            errorAlert.open();
        }
    }

    SystemPalette {
        id: systemPalette

        colorGroup: root.active ? SystemPalette.Active : SystemPalette.Inactive
    }

    menuBar: MenuBar {
        Menu {
            title: qsTr("File")

            MenuItem {
                action: Action {
                    text: qsTr("New")
                    shortcut: StandardKey.New
                    onTriggered: root.confirmThen(() => document.clear())
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Open…")
                    shortcut: StandardKey.Open
                    onTriggered: root.confirmThen(() => openDialog.open())
                }
            }
            MenuSeparator {}
            MenuItem {
                action: Action {
                    text: qsTr("Save")
                    shortcut: StandardKey.Save
                    onTriggered: root.save(null)
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Save As…")
                    shortcut: StandardKey.SaveAs
                    onTriggered: {
                        saveDialog.then = null;
                        saveDialog.open();
                    }
                }
            }
            MenuSeparator {}
            MenuItem {
                action: Action {
                    text: qsTr("Close")
                    shortcut: StandardKey.Close
                    onTriggered: root.close()
                }
            }
        }
        Menu {
            title: qsTr("Edit")

            MenuItem {
                action: Action {
                    text: qsTr("Undo")
                    enabled: editor.canUndo
                    shortcut: StandardKey.Undo
                    onTriggered: editor.undo()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Redo")
                    enabled: editor.canRedo
                    shortcut: StandardKey.Redo
                    onTriggered: editor.redo()
                }
            }
            MenuSeparator {}
            MenuItem {
                action: Action {
                    text: qsTr("Cut")
                    enabled: editor.selectedText !== ""
                    shortcut: StandardKey.Cut
                    onTriggered: editor.cut()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Copy")
                    enabled: editor.selectedText !== ""
                    shortcut: StandardKey.Copy
                    onTriggered: editor.copy()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Paste")
                    enabled: editor.canPaste
                    shortcut: StandardKey.Paste
                    onTriggered: editor.paste()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Select All")
                    shortcut: StandardKey.SelectAll
                    onTriggered: editor.selectAll()
                }
            }
        }
        // English on purpose: Qt moves these into the app menu by matching "About".
        Menu {
            title: qsTr("Help")

            MenuItem {
                text: "About Tekst"
                onTriggered: aboutDialog.open()
            }
            MenuItem {
                text: "About Qt"
                onTriggered: aboutQtDialog.open()
            }
        }
    }

    FileDialog {
        id: openDialog

        title: qsTr("Open")
        fileMode: FileDialog.OpenFile
        onAccepted: document.load(selectedFile)
    }

    FileDialog {
        id: saveDialog

        // Run after a successful save (closing, opening another file…).
        property var then: null

        title: qsTr("Save As")
        fileMode: FileDialog.SaveFile
        selectedFile: document.fileUrl
        onAccepted: {
            const then = saveDialog.then;
            saveDialog.then = null;
            if (document.saveAs(selectedFile) && then)
                then();
        }
        onRejected: saveDialog.then = null
    }

    Alert {
        id: saveAlert

        window: root
        style: Alert.Warning
        text: qsTr("Do you want to save the changes made to “%1”?").arg(document.fileName)
        informativeText: qsTr("Your changes will be lost if you don’t save them.")
        buttons: [qsTr("Save"), qsTr("Don’t Save"), qsTr("Cancel")]
        onFinished: button => {
            const action = root.pendingAction;
            root.pendingAction = null;
            if (button === 0)
                root.save(action);
            else if (button === 1)
                action();
        }
    }

    Alert {
        id: errorAlert

        window: root
        style: Alert.Critical
        text: qsTr("The document could not be opened or saved.")
    }

    AboutDialog {
        id: aboutDialog

        version: qsTr("Version %1").arg(Qt.application.version)
    }

    AboutQtDialog {
        id: aboutQtDialog
    }

    // The title bar: the document's name, with the window buttons on its left.
    Item {
        id: titleBar

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Math.max(root.SafeArea.margins.top, 40)

        TapHandler {
            onDoubleTapped: nativeWindow.titleBarDoubleClicked()
        }

        Label {
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - 2 * 90)
            text: document.modified ? qsTr("%1 — Edited").arg(document.fileName) : document.fileName
            color: systemPalette.windowText
            opacity: root.active ? 1 : 0.5
            font.weight: Font.DemiBold
            elide: Text.ElideMiddle
        }
    }

    Flickable {
        id: flickable

        // Scrolls just enough to keep the cursor in view.
        function ensureVisible(rect) {
            if (contentY >= rect.y)
                contentY = rect.y;
            else if (contentY + height <= rect.y + rect.height)
                contentY = rect.y + rect.height - height;
        }

        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        contentWidth: width
        contentHeight: editor.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {}

        TextEdit {
            id: editor

            width: flickable.width
            height: Math.max(implicitHeight, flickable.height)
            leftPadding: 16
            rightPadding: 16
            topPadding: 8
            bottomPadding: 16
            focus: true
            wrapMode: TextEdit.Wrap
            font: root.editorFont
            color: systemPalette.text
            selectionColor: systemPalette.highlight
            selectedTextColor: systemPalette.highlightedText
            selectByMouse: true
            persistentSelection: true
            onCursorRectangleChanged: flickable.ensureVisible(cursorRectangle)
        }
    }
}
