pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

import Rask.Controls
import Rask.Dialogs
import Rask.Platform
import Rask.Window

import Tekst

ApplicationWindow {
    id: root

    // A file given on the command line, opened at start.
    required property url initialFile
    // Run once the unsaved changes are saved or discarded.
    property var pendingAction: null
    property bool closeApproved: false
    // The editing surface, for the menus and the fileDocument.
    readonly property EditorView editor: textEditor.editor

    // Saves, asking for a file when the document has none, then runs `then`.
    function save(then) {
        if (fileDocument.fileUrl.toString() === "") {
            saveDialog.then = then;
            saveDialog.open();
        } else if (fileDocument.save() && then) {
            then();
        }
    }

    // Shows the find bar (with the replace row when `replacing`), focused.
    function showSearch(replacing) {
        searchBar.replacing = replacing;
        searchBar.visible = true;
        searchBar.focusFind();
    }

    // Runs `action` now, or after the user saves or discards the changes.
    function confirmThen(action) {
        if (!fileDocument.modified) {
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
    title: fileDocument.fileName + " — Tekst"
    flags: Qt.Window | Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint
    color: "transparent"
    // ApplicationWindow pads its content by the safe area; the title bar draws there itself.
    topPadding: 0

    onClosing: close => {
        if (root.closeApproved || !fileDocument.modified)
            return;
        close.accepted = false;
        root.confirmThen(() => {
            root.closeApproved = true;
            root.close();
        });
    }

    Component.onCompleted: {
        if (root.initialFile.toString() !== "")
            fileDocument.load(root.initialFile);
        root.editor.forceActiveFocus();
    }

    NativeWindow {
        id: nativeWindow

        window: root
        color: Appearance.dark ? "#1e1e1e" : "#fbfbfb"
        alpha: Preferences.windowOpacity
        blur: Preferences.windowBlur
        unifiedToolbar: root.visibility !== Window.FullScreen
    }

    SettingsWindow {
        id: settingsWindow
    }

    Document {
        id: fileDocument

        editor: root.editor
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
                    onTriggered: root.confirmThen(() => fileDocument.clear())
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
                    enabled: root.editor.canUndo
                    shortcut: StandardKey.Undo
                    onTriggered: root.editor.undo()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Redo")
                    enabled: root.editor.canRedo
                    shortcut: StandardKey.Redo
                    onTriggered: root.editor.redo()
                }
            }
            MenuSeparator {}
            MenuItem {
                action: Action {
                    text: qsTr("Cut")
                    enabled: root.editor.hasSelection
                    shortcut: StandardKey.Cut
                    onTriggered: root.editor.cut()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Copy")
                    enabled: root.editor.hasSelection
                    shortcut: StandardKey.Copy
                    onTriggered: root.editor.copy()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Paste")
                    enabled: root.editor.canPaste
                    shortcut: StandardKey.Paste
                    onTriggered: root.editor.paste()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Select All")
                    shortcut: StandardKey.SelectAll
                    onTriggered: root.editor.selectAll()
                }
            }
            MenuSeparator {}
            MenuItem {
                action: Action {
                    text: qsTr("Shift Right")
                    shortcut: "Ctrl+]"
                    onTriggered: root.editor.indent()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Shift Left")
                    shortcut: "Ctrl+["
                    onTriggered: root.editor.outdent()
                }
            }
            MenuSeparator {}
            MenuItem {
                action: Action {
                    text: qsTr("Go to Line…")
                    shortcut: "Ctrl+L"
                    onTriggered: goToLine.open()
                }
            }
            Menu {
                title: qsTr("Find")

                MenuItem {
                    action: Action {
                        text: qsTr("Find…")
                        shortcut: StandardKey.Find
                        onTriggered: root.showSearch(false)
                    }
                }
                MenuItem {
                    action: Action {
                        text: qsTr("Find and Replace…")
                        shortcut: "Ctrl+R"
                        onTriggered: root.showSearch(true)
                    }
                }
                MenuItem {
                    action: Action {
                        text: qsTr("Find Next")
                        enabled: root.editor.searchMatchCount > 0
                        shortcut: StandardKey.FindNext
                        onTriggered: root.editor.findNext()
                    }
                }
                MenuItem {
                    action: Action {
                        text: qsTr("Find Previous")
                        enabled: root.editor.searchMatchCount > 0
                        shortcut: StandardKey.FindPrevious
                        onTriggered: root.editor.findPrevious()
                    }
                }
                MenuItem {
                    action: Action {
                        text: qsTr("Use Selection for Find")
                        enabled: root.editor.hasSelection
                        shortcut: "Ctrl+E"
                        onTriggered: searchBar.text = root.editor.selectedText()
                    }
                }
            }
        }
        Menu {
            title: qsTr("View")

            MenuItem {
                action: Action {
                    text: qsTr("Wrap Lines")
                    checkable: true
                    checked: Preferences.wrap
                    onTriggered: Preferences.wrap = checked
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Show Invisibles")
                    checkable: true
                    checked: Preferences.showInvisibles
                    onTriggered: Preferences.showInvisibles = checked
                }
            }
            MenuSeparator {}
            Menu {
                id: themeMenu

                title: qsTr("Theme")

                Instantiator {
                    model: Syntaxes.themes

                    delegate: MenuItem {
                        required property string modelData

                        text: modelData
                        checkable: true
                        checked: root.editor.syntaxTheme === modelData
                        onTriggered: Preferences.theme = modelData
                    }

                    onObjectAdded: (index, object) => themeMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => themeMenu.removeItem(object)
                }
            }
            MenuSeparator {}
            MenuItem {
                action: Action {
                    text: qsTr("Bigger")
                    shortcut: StandardKey.ZoomIn
                    enabled: Preferences.fontSize < Preferences.largestFontSize
                    onTriggered: Preferences.fontSize += 1
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Smaller")
                    shortcut: StandardKey.ZoomOut
                    enabled: Preferences.fontSize > Preferences.smallestFontSize
                    onTriggered: Preferences.fontSize -= 1
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Actual Size")
                    shortcut: "Ctrl+0"
                    enabled: Preferences.fontSize !== Preferences.defaultFontSize
                    onTriggered: Preferences.fontSize = Preferences.defaultFontSize
                }
            }
        }
        // English on purpose: Qt moves these into the app menu by matching
        // "Settings" and "About".
        Menu {
            title: qsTr("Help")

            MenuItem {
                action: Action {
                    text: "Settings…"
                    shortcut: StandardKey.Preferences
                    onTriggered: {
                        settingsWindow.show();
                        settingsWindow.raise();
                        settingsWindow.requestActivate();
                    }
                }
            }
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
        onAccepted: fileDocument.load(selectedFile)
    }

    FileDialog {
        id: saveDialog

        // Run after a successful save (closing, opening another file…).
        property var then: null

        title: qsTr("Save As")
        fileMode: FileDialog.SaveFile
        selectedFile: fileDocument.fileUrl
        onAccepted: {
            const then = saveDialog.then;
            saveDialog.then = null;
            if (fileDocument.saveAs(selectedFile) && then)
                then();
        }
        onRejected: saveDialog.then = null
    }

    Alert {
        id: saveAlert

        window: root
        style: Alert.Warning
        text: qsTr("Do you want to save the changes made to “%1”?").arg(fileDocument.fileName)
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
            text: fileDocument.modified ? qsTr("%1 — Edited").arg(fileDocument.fileName) : fileDocument.fileName
            color: systemPalette.windowText
            opacity: root.active ? 1 : 0.5
            font.weight: Font.DemiBold
            elide: Text.ElideMiddle
        }
    }

    SearchBar {
        id: searchBar

        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: visible ? implicitHeight : 0
        visible: false
        matchCount: root.editor.searchMatchCount
        currentMatch: root.editor.searchCurrent
        complete: root.editor.searchComplete
        error: root.editor.searchError
        onNext: root.editor.findNext()
        onPrevious: root.editor.findPrevious()
        onReplace: root.editor.replaceCurrent(replacementText)
        onReplaceAll: root.editor.replaceAll(replacementText)
        onDone: {
            visible = false;
            root.editor.forceActiveFocus();
        }
    }

    GoToLine {
        id: goToLine

        x: (root.width - width) / 2
        y: titleBar.height + 8
        onAccepted: target => {
            if (root.editor.goToLine(target)) {
                goToLine.close();
                textEditor.center(root.editor.cursorRectangle);
            }
        }
        onClosed: root.editor.forceActiveFocus()
    }

    // The editor searches for what the bar holds while it shows.
    Binding {
        target: root.editor
        property: "searchPattern"
        value: searchBar.visible ? searchBar.text : ""
    }
    Binding {
        target: root.editor
        property: "searchCaseSensitive"
        value: searchBar.caseSensitive
    }
    Binding {
        target: root.editor
        property: "searchRegularExpression"
        value: searchBar.regularExpression
    }

    TextEditor {
        id: textEditor

        anchors.top: searchBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusBar.top
        editor.syntaxTheme: Preferences.theme !== "" ? Preferences.theme : (Appearance.dark ? "Tekst Dark" : "Tekst Light")
        editor.wrap: Preferences.wrap
        editor.showInvisibles: Preferences.showInvisibles
        editor.font: Preferences.font
        editor.defaultIndentWithTabs: Preferences.indentWithTabs
        editor.defaultIndentWidth: Preferences.indentWidth
    }

    StatusBar {
        id: statusBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        editor: root.editor
        document: fileDocument
        onReopenRequested: encoding => root.confirmThen(() => fileDocument.reopen(encoding))
    }
}
