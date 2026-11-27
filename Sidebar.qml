pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl

import Rask.Controls
import Rask.Dialogs
import Rask.Platform
import Rask.Window

import Kode

// The window's sidebar, as Xcode's navigator: under the title bar (the
// window buttons and the sidebar button sit over its top) the window's
// folder as a tree, as VS Code's explorer, with the system's icons; the
// file in view is revealed (its folders opened, scrolled to) while syncing
// with the editor, as Qt Creator's. A click selects; a double click opens a
// file or opens or closes a folder, as its arrow does. As Neovim's file
// trees, the root can go up a folder or move to a folder in the tree. A
// right click offers what a file tree needs (new, rename in place,
// duplicate, trash, cut/copy/paste, copy path, reveal in Finder).
Item {
    id: sidebar

    property url folder
    // The file of the document in view, marked in the tree.
    property url currentFile
    // The active split's text: its language server shows in the footer.
    property TextDocument currentText: null
    // The title bar's height: the tree starts below it.
    property real topInset: 0
    property NativeWindow nativeWindow: null
    // The entry clicked last.
    property url selectedFile
    // The file in view is not listed yet: reveal() runs again as entries arrive.
    property bool revealPending: false
    readonly property bool syncing: Preferences.syncWithEditor
    // The entry being renamed in place; a new file opens once named.
    property url renaming
    property bool openAfterRename: false
    // What the file operations act on: the selection, else the folder.
    readonly property url target: selectedFile.toString() !== "" ? selectedFile : folder
    readonly property bool targetIsRoot: selectedFile.toString() === ""
    readonly property bool targetIsFolder: targetIsRoot || OpenDocuments.isFolder(selectedFile)
    // Keys go to the tree (not to a name being typed): its shortcuts apply.
    readonly property bool treeFocused: tree.contentItem.activeFocus && renaming.toString() === "" && folder.toString() !== ""
    readonly property bool canCut: !targetIsRoot
    readonly property bool canPaste: entryMenu.pasteAvailable
    // The window's Edit and View actions, for the right-click menu (their shortcuts show).
    property Action cutAction
    property Action copyAction
    property Action pasteAction
    property Action collapseAllAction
    // Settings > Files > Sidebar size: small, medium, large.
    readonly property int fontSize: [12, 13, 15][Preferences.sidebarSize]
    readonly property int iconSize: [14, 16, 20][Preferences.sidebarSize]
    readonly property int rowHeight: [22, 24, 30][Preferences.sidebarSize]
    readonly property int indentation: [12, 14, 17][Preferences.sidebarSize]
    // Bumped when the system's colors change: the icons load again (folders take the accent color).
    property int iconVersion: 0

    signal fileChosen(url file)
    signal openFolderRequested
    // The tree's root moves to `folder` (the window's folder).
    signal folderChosen(url folder)
    // Move `url` to the Trash once the documents of it are closed (see trash()).
    signal trashRequested(url url)

    // Return on the selection, as a double click: opens a file, opens or closes a folder.
    function openSelected() {
        const url = sidebar.selectedFile;
        if (url.toString() === "")
            return;
        if (OpenDocuments.isFolder(url)) {
            const row = tree.rowAtIndex(fileTree.indexOf(url));
            if (row >= 0)
                tree.toggleExpanded(row);
            return;
        }
        sidebar.selectedFile = "";
        sidebar.fileChosen(url);
    }

    // Asks first; the window then closes the documents of it (trashRequested).
    function confirmTrash(url: url) {
        if (url.toString() === "")
            return;
        trashAlert.target = url;
        trashAlert.open();
    }

    function cutSelected(cut: bool) {
        fileTree.copy(sidebar.target, cut);
        sidebar.refreshPaste();
    }

    function pasteHere() {
        const pasted = fileTree.paste(fileTree.folderFor(sidebar.target));
        if (pasted.toString() === "")
            return;
        sidebar.selectedFile = pasted;
        sidebar.show(pasted);
    }

    function refreshPaste() {
        entryMenu.pasteAvailable = fileTree.canPaste();
    }

    onTreeFocusedChanged: {
        if (treeFocused)
            refreshPaste();
    }

    function renameSelected() {
        if (sidebar.selectedFile.toString() === "")
            return;
        sidebar.openAfterRename = false;
        sidebar.renaming = sidebar.selectedFile;
    }

    // Moves `url` to the Trash; true when it went.
    function trash(url: url): bool {
        if (url.toString() === "")
            return false;
        if (sidebar.selectedFile.toString() === url.toString())
            sidebar.selectedFile = "";
        return fileTree.moveToTrash(url);
    }

    // Closes every folder, for a cleaner view; revealing a file opens its own again.
    function collapseAll() {
        // Each top-level row (subfolders too, even hidden ones), from the
        // bottom: rows above stay put. collapseRecursively() without a row
        // walks the model's root, not the rootIndex, and closes nothing.
        for (let row = tree.rows - 1; row >= 0; --row) {
            if (tree.depth(row) === 0)
                tree.collapseRecursively(row);
        }
        tree.contentY = tree.originY;
    }

    // For the editor's menu too: any file, in the folder or not.
    function copyPath(url: url) {
        fileTree.copyPath(url);
    }

    function revealInFinder(url: url) {
        fileTree.revealInFinder(url);
    }

    // Keys to the tree (Return opens), ending a rename: the tree is a focus
    // scope, so focusing it alone would leave the name field focused inside.
    function focusTree() {
        tree.contentItem.forceActiveFocus();
    }

    // Opens the folders down to `url` and scrolls to it.
    function show(url: url) {
        const index = fileTree.indexOf(url);
        if (!fileTree.isValid(index))
            return;
        tree.expandToIndex(index);
        tree.forceLayout();
        const row = tree.rowAtIndex(index);
        if (row >= 0)
            tree.positionViewAtRow(row, TableView.Contain);
    }

    // A new file (folder) next to `url`, named in place.
    function createEntry(url: url, folder: bool) {
        const target = fileTree.folderFor(url);
        const made = folder ? fileTree.createFolder(target) : fileTree.createFile(target);
        if (made.toString() === "")
            return;
        sidebar.selectedFile = made;
        sidebar.renaming = made;
        sidebar.openAfterRename = !folder;
        sidebar.show(made);
    }

    // The name typed in place (`original` when unchanged or cancelled).
    function commitRename(url: url, name: string, original: string) {
        // Already done (Return, then the field losing focus as it goes). Not
        // compared with `url`: a typed url argument's text is percent-encoded.
        if (sidebar.renaming.toString() === "")
            return;
        sidebar.renaming = "";
        sidebar.focusTree();
        const renamed = name === original ? url : fileTree.rename(url, name);
        if (renamed.toString() === "")
            return;
        sidebar.selectedFile = renamed;
        if (sidebar.openAfterRename) {
            sidebar.openAfterRename = false;
            sidebar.selectedFile = "";
            sidebar.fileChosen(renamed);
        }
    }

    // Opens the folders down to the file in view and scrolls to it. Its
    // entry may not be listed yet (folders are read in another thread):
    // then again as entries arrive.
    function reveal() {
        sidebar.revealPending = false;
        if (!Preferences.syncWithEditor || !sidebar.visible || !fileTree.contains(sidebar.currentFile))
            return;
        const index = fileTree.indexOf(sidebar.currentFile);
        if (!fileTree.isValid(index)) {
            sidebar.revealPending = true;
            return;
        }
        tree.expandToIndex(index);
        tree.forceLayout();
        const row = tree.rowAtIndex(index);

        if (row < 0) {
            sidebar.revealPending = true;
            return;
        }
        tree.positionViewAtRow(row, TableView.Contain);
    }

    onCurrentFileChanged: Qt.callLater(sidebar.reveal)
    onSyncingChanged: Qt.callLater(sidebar.reveal)
    onFolderChanged: Qt.callLater(sidebar.reveal)
    onVisibleChanged: Qt.callLater(sidebar.reveal)
    Component.onCompleted: Qt.callLater(sidebar.reveal)

    Connections {
        target: SystemColors

        function onChanged() {
            sidebar.iconVersion += 1;
        }
    }

    FileTree {
        id: fileTree

        folder: sidebar.folder
        showHidden: Preferences.showHiddenFiles
        // Entries arrive: they may shift the file's row, or list it at last.
        onFolderLoaded: url => {
            if (sidebar.revealPending || sidebar.currentFile.toString().startsWith(url.toString() + "/"))
                Qt.callLater(sidebar.reveal);
        }
        onRowsInserted: {
            if (sidebar.revealPending)
                Qt.callLater(sidebar.reveal);
        }
        onErrorOccurred: message => {
            errorAlert.informativeText = message;
            errorAlert.open();
        }
        onMoved: (from, to) => OpenDocuments.fileMoved(from, to)
    }

    Alert {
        id: errorAlert

        window: sidebar.Window.window
        style: Alert.Critical
        text: qsTr("The operation could not be completed.")
    }

    Alert {
        id: trashAlert

        property url target

        window: sidebar.Window.window
        style: Alert.Warning
        text: qsTr("Move “%1” to the Trash?").arg(decodeURIComponent(target.toString().split("/").pop()))
        informativeText: qsTr("You can restore it from the Trash.")
        buttons: [qsTr("Move to Trash"), qsTr("Cancel")]
        onFinished: button => {
            if (button === 0)
                sidebar.trashRequested(trashAlert.target);
        }
    }

    // Right click on an entry, or where none is (the root): its file operations.
    Menu {
        id: entryMenu

        property bool pasteAvailable: false

        function show() {
            sidebar.refreshPaste();
            entryMenu.popup();
        }

        popupType: Popup.Native

        MenuItem {
            text: qsTr("New File")
            onTriggered: sidebar.createEntry(sidebar.target, false)
        }
        MenuItem {
            text: qsTr("New Folder")
            onTriggered: sidebar.createEntry(sidebar.target, true)
        }
        MenuSeparator {}
        MenuItem {
            action: openAction
        }
        MenuItem {
            action: renameAction
        }
        MenuItem {
            text: qsTr("Duplicate")
            enabled: !sidebar.targetIsRoot
            onTriggered: {
                const copy = fileTree.duplicate(sidebar.target);
                if (copy.toString() !== "") {
                    sidebar.selectedFile = copy;
                    sidebar.show(copy);
                }
            }
        }
        MenuItem {
            action: trashAction
        }
        MenuSeparator {}
        MenuItem {
            action: sidebar.cutAction
        }
        MenuItem {
            action: sidebar.copyAction
        }
        MenuItem {
            action: sidebar.pasteAction
        }
        MenuSeparator {}
        MenuItem {
            action: copyPathAction
        }
        MenuItem {
            action: revealAction
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Make Root")
            enabled: sidebar.targetIsFolder && !sidebar.targetIsRoot
            onTriggered: sidebar.folderChosen(sidebar.target)
        }
        MenuItem {
            action: sidebar.collapseAllAction
        }
    }

    // Their shortcuts work while the tree has the focus, as the Finder's (⌘↓,
    // ⌘Delete, ⌥⌘C) and VS Code's (F2, ⌥⌘R).
    Action {
        id: openAction

        text: qsTr("Open")
        shortcut: "Ctrl+Down"
        enabled: sidebar.treeFocused && !sidebar.targetIsFolder
        onTriggered: sidebar.openSelected()
    }
    Action {
        id: renameAction

        text: qsTr("Rename")
        shortcut: "F2"
        enabled: sidebar.treeFocused && !sidebar.targetIsRoot
        onTriggered: sidebar.renameSelected()
    }
    Action {
        id: trashAction

        text: qsTr("Move to Trash")
        shortcut: "Ctrl+Backspace"
        enabled: sidebar.treeFocused && !sidebar.targetIsRoot
        onTriggered: sidebar.confirmTrash(sidebar.target)
    }
    Action {
        id: copyPathAction

        text: qsTr("Copy Path")
        shortcut: "Ctrl+Alt+C"
        enabled: sidebar.treeFocused
        onTriggered: fileTree.copyPath(sidebar.target)
    }
    Action {
        id: revealAction

        text: qsTr("Reveal in Finder")
        shortcut: "Ctrl+Alt+R"
        enabled: sidebar.treeFocused
        onTriggered: fileTree.revealInFinder(sidebar.target)
    }

    // The filter button's menu.
    Menu {
        id: filterMenu

        popupType: Popup.Native

        MenuItem {
            text: qsTr("Show Hidden Files")
            checkable: true
            checked: Preferences.showHiddenFiles
            onTriggered: Preferences.showHiddenFiles = checked
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.alpha(SystemColors.label, 0.035)
    }

    // The title bar: empty space does the system's double click.
    Item {
        id: titleBar

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: sidebar.topInset

        TapHandler {
            onDoubleTapped: {
                if (sidebar.nativeWindow)
                    sidebar.nativeWindow.titleBarDoubleClicked();
            }
        }
    }

    // The folder's name, then (as Qt Creator's) up a folder, the filter and sync with the editor.
    Item {
        id: header

        visible: sidebar.folder.toString() !== ""
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: visible ? 32 : 0

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: {
                sidebar.selectedFile = "";
                sidebar.focusTree();
                entryMenu.show();
            }
        }

        Label {
            anchors.left: parent.left
            anchors.right: headerButtons.left
            anchors.leftMargin: 14
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: decodeURIComponent(sidebar.folder.toString().replace(/\/$/, "").split("/").pop())
            color: SystemColors.secondaryLabel
            font.pixelSize: sidebar.fontSize - 2
            font.weight: Font.DemiBold
            elide: Text.ElideMiddle
        }

        Row {
            id: headerButtons

            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            // SF Symbols are fitted to the icon's box: a wide one gets a larger
            // box so that every symbol looks the same size.
            BarButton {
                icon.name: "arrow.up"
                icon.width: 14
                icon.height: 14
                available: fileTree.parentFolder.toString() !== ""
                Accessible.name: qsTr("Enclosing folder")
                toolTip: qsTr("Enclosing Folder")
                onClicked: {
                    if (available)
                        sidebar.folderChosen(fileTree.parentFolder);
                }
            }
            BarButton {
                id: filterButton

                icon.name: "line.3.horizontal.decrease"
                icon.width: 18
                icon.height: 18
                Accessible.name: qsTr("Filter")
                toolTip: qsTr("Filter")
                onClicked: filterMenu.popup(filterButton, 0, filterButton.height)
            }
            BarButton {
                icon.name: "link"
                icon.width: 15
                icon.height: 15
                checkable: true
                checked: Preferences.syncWithEditor
                Accessible.name: qsTr("Synchronize with editor")
                toolTip: qsTr("Synchronize with Editor")
                onToggled: Preferences.syncWithEditor = checked
            }
        }
    }

    // Once per window, level with the splits' status bars: the language
    // server of the document in view, and what it is busy with.
    Item {
        id: footer

        readonly property string server: sidebar.currentText?.languageServer ?? ""
        readonly property string activity: sidebar.currentText?.languageServerActivity ?? ""

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 26

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Qt.alpha(SystemColors.label, 0.1)
        }

        Row {
            id: footerRow

            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 5
            visible: footer.server !== ""

            BusyIndicator {
                anchors.verticalCenter: parent.verticalCenter
                width: 12
                height: 12
                padding: 0
                running: visible
                visible: footer.activity !== ""
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                width: footerRow.width - (footer.activity !== "" ? 17 : 0)
                elide: Text.ElideRight
                text: footer.activity === "" ? footer.server : qsTr("%1: %2").arg(footer.server).arg(footer.activity)
                color: SystemColors.secondaryLabel
                font.pixelSize: 11
            }
        }

        HelpTag {
            text: footer.server === "" ? "" : footer.activity === "" ? qsTr("%1 checks this file for problems as you type.").arg(footer.server) : qsTr("%1 is busy: %2. Problems show when it is done.").arg(footer.server).arg(footer.activity)
            delay: 500
        }
    }

    TreeView {
        id: tree

        visible: sidebar.folder.toString() !== ""
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: footer.top
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: fileTree
        rootIndex: fileTree.rootIndex
        // The widest entry laid out so far: names are not cut, the tree scrolls sideways.
        property real widest: 0

        columnWidthProvider: () => Math.max(tree.width, tree.widest)
        onWidthChanged: tree.forceLayout()
        onWidestChanged: Qt.callLater(tree.forceLayout)

        // Return opens, Delete trashes (⌘Delete too: trashAction); every
        // other key goes on (⇧⌘W and the like reach the menus).
        Keys.onPressed: event => {
            const plain = (event.modifiers & ~Qt.KeypadModifier) === Qt.NoModifier;
            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && plain)
                sidebar.openSelected();
            else if ((event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) && plain)
                sidebar.confirmTrash(sidebar.selectedFile);
            else
                event.accepted = false;
        }

        ScrollBar.vertical: OverlayScrollBar {}
        ScrollBar.horizontal: OverlayScrollBar {}

        Connections {
            target: sidebar

            function onFolderChanged() {
                tree.widest = 0;
            }
        }

        delegate: Item {
            id: entry

            required property TreeView treeView
            required property bool expanded
            required property int depth
            required property int row
            required property string fileName
            required property url fileUrl
            required property string iconSource
            required property bool isFolder

            readonly property bool current: entry.fileUrl.toString() === sidebar.currentFile.toString()
            readonly property bool selected: entry.fileUrl.toString() === sidebar.selectedFile.toString()
            readonly property bool renaming: entry.fileUrl.toString() === sidebar.renaming.toString()

            implicitWidth: nameLabel.x + nameLabel.implicitWidth + 12
            implicitHeight: sidebar.rowHeight
            onImplicitWidthChanged: tree.widest = Math.max(tree.widest, implicitWidth)
            Component.onCompleted: tree.widest = Math.max(tree.widest, implicitWidth)

            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                radius: 5
                // The accent color, as the editor's selection: the selection, then (lighter) the file in view.
                color: entry.selected ? Qt.alpha(SystemColors.controlAccent, entry.Window.active ? 0.4 : 0.2) : (entry.current ? Qt.alpha(SystemColors.controlAccent, 0.14) : (hover.hovered ? Qt.alpha(SystemColors.label, 0.06) : "transparent"))
            }

            IconImage {
                id: disclosure

                x: 10 + entry.depth * sidebar.indentation
                anchors.verticalCenter: parent.verticalCenter
                visible: entry.isFolder
                name: "chevron.right"
                sourceSize.width: 10
                sourceSize.height: 10
                fillMode: Image.Pad
                color: SystemColors.secondaryLabel
                rotation: entry.expanded ? 90 : 0

                // A wider target than the arrow itself.
                TapHandler {
                    margin: 5
                    onTapped: entry.treeView.toggleExpanded(entry.row)
                }
            }

            Image {
                id: icon

                x: disclosure.x + 14
                anchors.verticalCenter: parent.verticalCenter
                width: sidebar.iconSize
                height: sidebar.iconSize
                sourceSize.width: sidebar.iconSize * 2
                sourceSize.height: sidebar.iconSize * 2
                // The system's icons on the GUI thread: AppKit tints folders with the accent color,
                // and the loading thread draws with the old one after it changes. Code icons stay off it.
                asynchronous: !entry.iconSource.startsWith("image://fileicon")
                source: entry.iconSource + "?v=" + sidebar.iconVersion
            }

            Label {
                id: nameLabel

                visible: !entry.renaming
                anchors.left: icon.right
                anchors.leftMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: entry.fileName
                color: SystemColors.label
                font.pixelSize: sidebar.fontSize
            }

            // Renaming in place: Return (or clicking away) renames, Escape keeps the name.
            Loader {
                active: entry.renaming
                anchors.left: icon.right
                anchors.leftMargin: 3
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(140, tree.width - x - 10)

                sourceComponent: TextField {
                    id: nameField

                    // The entry and its name when renaming began.
                    readonly property url target: entry.fileUrl
                    readonly property string original: entry.fileName
                    // Focus arrived: losing it now means a click elsewhere.
                    property bool editing: false

                    function begin() {
                        nameField.forceActiveFocus();
                        // The name without its extension, as the Finder selects it.
                        const dot = entry.isFolder ? -1 : nameField.text.lastIndexOf(".");
                        nameField.select(0, dot > 0 ? dot : nameField.text.length);
                    }

                    font.pixelSize: sidebar.fontSize
                    text: original
                    // Not onAccepted: TextField lets Return go on, to the tree, which would open the file.
                    Keys.onReturnPressed: sidebar.commitRename(nameField.target, nameField.text, nameField.original)
                    Keys.onEnterPressed: sidebar.commitRename(nameField.target, nameField.text, nameField.original)
                    onActiveFocusChanged: {
                        if (activeFocus)
                            nameField.editing = true;
                        else if (nameField.editing)
                            Qt.callLater(sidebar.commitRename, nameField.target, nameField.text, nameField.original);
                    }
                    Keys.onEscapePressed: sidebar.commitRename(nameField.target, nameField.original, nameField.original)
                    // Not while the Loader is still making it: focus moves would come back here.
                    Component.onCompleted: Qt.callLater(nameField.begin)
                }
            }

            HoverHandler {
                id: hover
            }

            TapHandler {
                enabled: !entry.renaming
                acceptedButtons: Qt.RightButton
                onTapped: {
                    sidebar.selectedFile = entry.fileUrl;
                    sidebar.focusTree();
                    entryMenu.show();
                }
            }

            TapHandler {
                enabled: !entry.renaming
                onTapped: {
                    sidebar.selectedFile = entry.fileUrl;
                    sidebar.focusTree();
                }
                // On a folder TreeView itself opens or closes it.
                onDoubleTapped: {
                    if (entry.isFolder)
                        return;
                    // Once open it shows as the file in view, not selected.
                    sidebar.selectedFile = "";
                    sidebar.fileChosen(entry.fileUrl);
                }
            }
        }
    }

    // A click where no entry is ends a rename.
    TapHandler {
        enabled: tree.visible && sidebar.renaming.toString() !== ""
        onTapped: eventPoint => {
            const inContent = tree.contentItem.mapFromItem(sidebar, eventPoint.position);
            if (tree.cellAtPosition(inContent, false).y < 0)
                sidebar.focusTree();
        }
    }

    // A right click where no entry is: the root's menu (the entries have their own).
    TapHandler {
        enabled: tree.visible
        acceptedButtons: Qt.RightButton
        onTapped: eventPoint => {
            const inTree = tree.mapFromItem(sidebar, eventPoint.position);
            if (inTree.y < 0 || inTree.y > tree.height)
                return;
            const inContent = tree.contentItem.mapFromItem(sidebar, eventPoint.position);
            if (tree.cellAtPosition(inContent, false).y >= 0)
                return;
            sidebar.selectedFile = "";
            sidebar.focusTree();
            entryMenu.show();
        }
    }

    // No folder: how to open one.
    Column {
        visible: sidebar.folder.toString() === ""
        anchors.centerIn: parent
        width: parent.width - 32
        spacing: 10

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("No Folder Open")
            color: SystemColors.secondaryLabel
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }

        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Open Folder…")
            focusPolicy: Qt.TabFocus
            onClicked: sidebar.openFolderRequested()
        }
    }
}
