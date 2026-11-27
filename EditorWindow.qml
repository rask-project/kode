pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

import Rask.Application
import Rask.Dialogs
import Rask.Platform
import Rask.Window

import Kode

// An editor window: the sidebar with its folder (hidden without one), its
// splits up to the top (the bars there fill the title bar, after the window
// buttons and the sidebar button), the editor splits (Splits, PaneLayout) each showing one of the open
// documents, the menus, and the file dialogs and "Save changes?" sheet.
// Main.qml keeps the window list and the documents shared by all windows.
ApplicationWindow {
    id: root

    required property Main app
    // Unsaved changes were reviewed: the window closes without asking.
    property bool closeApproved: false
    // Documents left to review before `reviewDone` runs (see review()).
    property list<Document> reviewQueue: []
    property var reviewDone: null
    // The folder the sidebar shows; none for a window of files alone.
    property url folder
    property bool sidebarVisible: false
    property real sidebarWidth: 240

    readonly property Splits splits: windowSplits
    readonly property EditorPane activeView: windowSplits.activeView as EditorPane
    readonly property Document activeDocument: activeView ? activeView.document : null
    // Null on the start page.
    readonly property EditorView editor: activeDocument ? activeView.editor : null

    // Shows `document` in `view` (the active split); an untouched new
    // document it replaces is closed when no other split shows it.
    function showDocument(document: Document, view: EditorPane) {
        const target = view ?? root.activeView;
        if (!target || !document)
            return;
        const replaced = target.untouched && target.document !== document ? target.document : null;
        target.document = document;
        OpenDocuments.touch(document);
        windowSplits.activate(target);
        target.focusEditor();
        if (replaced && !root.app.isShown(replaced))
            OpenDocuments.close(replaced);
    }

    function newDocument() {
        root.showDocument(OpenDocuments.create(), null);
    }

    // Saves `document`, asking for a file when it has none, then runs `then`.
    // With Re-indent on save, a split showing it re-indents the lines edited
    // since the last save first (one that is shown nowhere is saved as it is).
    function save(document: Document, then) {
        if (Preferences.reindentOnSave) {
            const view = root.activeDocument === document ? root.activeView : root.app.viewsOf(document)[0] ?? null;
            view?.editor.reindentEdited();
        }
        if (document.fileUrl.toString() === "") {
            root.saveAs(document, then);
        } else if (document.save() && then) {
            then();
        }
    }

    function saveAs(document: Document, then) {
        saveDialog.document = document;
        saveDialog.then = then;
        saveDialog.selectedFile = document.fileUrl;
        saveDialog.open();
    }

    // Runs `then` once every modified document of `documents` is saved or
    // its changes discarded, asking about each in turn; Cancel stops.
    function review(documents: list<Document>, then) {
        root.reviewQueue = documents.filter(document => document.modified);
        root.reviewDone = then;
        root.reviewNext();
    }

    function reviewNext() {
        if (root.reviewQueue.length === 0) {
            const done = root.reviewDone;
            root.reviewDone = null;
            if (done)
                done();
            return;
        }
        const document = root.reviewQueue[0];
        // The sheet is about the document in view.
        if (root.activeDocument !== document)
            root.showDocument(document, null);
        saveAlert.document = document;
        saveAlert.open();
    }

    function cancelReview() {
        root.reviewQueue = [];
        root.reviewDone = null;
    }

    function closeDocument(document: Document) {
        root.review([document], () => OpenDocuments.close(document));
    }

    // Every open document, unsaved ones asked about first; the windows stay, on the start page.
    function closeAllDocuments() {
        // A copy: the property read stays bound to the list, which shrinks as they close.
        const documents = OpenDocuments.documents.slice();
        root.review(documents, () => {
            for (const document of documents)
                OpenDocuments.close(document as Document);
        });
    }

    // The saved geometry and splits, for the next launch.
    function state(): var {
        return {
            x: root.x,
            y: root.y,
            width: root.width,
            height: root.height,
            visibility: root.visibility === Window.FullScreen ? "fullscreen" : (root.visibility === Window.Maximized ? "maximized" : "normal"),
            splits: windowSplits.state(),
            folder: root.folder.toString(),
            sidebar: root.sidebarVisible,
            sidebarWidth: sidebar.SplitView.preferredWidth
        };
    }

    width: 900
    height: 640
    minimumWidth: 400
    minimumHeight: 240
    // Feeds the Window menu and the window switcher; the title bar shows none.
    title: activeDocument ? activeDocument.fileName : (folder.toString() !== "" ? decodeURIComponent(folder.toString().split("/").pop()) : "Kode")
    flags: Qt.Window | Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint
    color: "transparent"
    // ApplicationWindow pads its content by the safe area; the title bar draws there itself.
    topPadding: 0

    onClosing: close => {
        // Other windows keep the documents open; the last one ends the app.
        if (root.closeApproved || QuitGuard.quitting || root.app.windows.length > 1 || OpenDocuments.modifiedDocuments().length === 0) {
            root.app.windowClosed(root);
            return;
        }
        close.accepted = false;
        root.review(OpenDocuments.modifiedDocuments(), () => {
            root.closeApproved = true;
            root.close();
        });
    }

    onActiveChanged: {
        if (active)
            root.app.noteActiveWindow(root);
    }

    NativeWindow {
        id: windowStyle

        window: root
        color: Appearance.dark ? "#1e1e1e" : "#fbfbfb"
        alpha: Preferences.windowOpacity
        blur: Preferences.windowBlur
        unifiedToolbar: root.visibility !== Window.FullScreen
        // Not by the title bar while the pointer is on one of its buttons.
        movable: TitleBarButtons.hovered === 0
        representedFile: root.activeDocument ? root.activeDocument.fileUrl : ""
        documentEdited: root.activeDocument ? root.activeDocument.modified : false
    }

    Splits {
        id: windowSplits

        // The splits' own, not root.activeView: that binding may not have followed yet.
        onActiveViewChanged: {
            const view = windowSplits.activeView as EditorPane;
            if (!view)
                return;
            view.focusEditor();
            if (view.document)
                OpenDocuments.touch(view.document);
        }
    }

    // The editor's right-click menu: the menu bar's actions, with their shortcuts.
    Menu {
        id: editorMenu

        readonly property url file: root.activeDocument ? root.activeDocument.fileUrl : ""

        popupType: Popup.Native

        MenuItem {
            action: goToDefinitionAction
        }
        MenuItem {
            action: switchHeaderSourceAction
        }
        MenuSeparator {}
        MenuItem {
            action: cutAction
        }
        MenuItem {
            action: copyAction
        }
        MenuItem {
            action: pasteAction
        }
        MenuSeparator {}
        MenuItem {
            action: selectNextOccurrenceAction
        }
        MenuItem {
            action: selectBlockAction
        }
        MenuItem {
            action: selectAllAction
        }
        MenuSeparator {}
        MenuItem {
            action: useSelectionForFindAction
        }
        MenuSeparator {}
        MenuItem {
            action: lineCommentAction
        }
        MenuItem {
            action: blockCommentAction
        }
        MenuItem {
            action: reindentAction
        }
        MenuSeparator {}
        MenuItem {
            action: foldAction
        }
        MenuItem {
            action: unfoldAction
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Copy Path")
            enabled: editorMenu.file.toString() !== ""
            onTriggered: sidebar.copyPath(editorMenu.file)
        }
        MenuItem {
            text: qsTr("Reveal in Finder")
            enabled: editorMenu.file.toString() !== ""
            onTriggered: sidebar.revealInFinder(editorMenu.file)
        }
    }

    FileDialog {
        id: openDialog

        // Else the process's working directory: / from the Finder.
        currentFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)
        title: qsTr("Open")
        fileMode: FileDialog.OpenFiles
        onAccepted: root.app.openFiles(selectedFiles, root)
    }

    FolderDialog {
        id: folderDialog

        currentFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)
        title: qsTr("Open Folder")
        onAccepted: root.app.openFiles([selectedFolder], root)
    }

    FileDialog {
        id: saveDialog

        property Document document: null
        // Run after a successful save (closing, quitting…).
        property var then: null

        currentFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)
        title: qsTr("Save As")
        fileMode: FileDialog.SaveFile
        onAccepted: {
            const then = saveDialog.then;
            const document = saveDialog.document;
            saveDialog.then = null;
            saveDialog.document = null;
            if (document && document.saveAs(selectedFile)) {
                RecentDocuments.add(selectedFile);
                if (then)
                    then();
            }
        }
        onRejected: {
            saveDialog.then = null;
            saveDialog.document = null;
            root.cancelReview();
        }
    }

    Alert {
        id: saveAlert

        property Document document: null

        window: root
        style: Alert.Warning
        text: qsTr("Do you want to save the changes made to “%1”?").arg(document ? document.fileName : "")
        informativeText: qsTr("Your changes will be lost if you don’t save them.")
        buttons: [qsTr("Save"), qsTr("Don’t Save"), qsTr("Cancel")]
        onFinished: button => {
            const document = saveAlert.document;
            saveAlert.document = null;
            if (button === 2 || !document) {
                root.cancelReview();
                return;
            }
            const next = () => {
                root.reviewQueue = root.reviewQueue.filter(queued => queued !== document);
                root.reviewNext();
            };
            if (button === 0)
                root.save(document, next);
            else
                next();
        }
    }

    menuBar: MenuBar {
        Menu {
            title: qsTr("File")

            MenuItem {
                action: Action {
                    text: qsTr("New")
                    shortcut: StandardKey.New
                    onTriggered: root.newDocument()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("New Window")
                    shortcut: "Ctrl+Shift+N"
                    onTriggered: root.app.openWindow(null)
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Open…")
                    shortcut: StandardKey.Open
                    onTriggered: openDialog.open()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Open Folder…")
                    shortcut: "Ctrl+Shift+O"
                    onTriggered: folderDialog.open()
                }
            }
            Menu {
                id: recentMenu

                title: qsTr("Open Recent")

                Instantiator {
                    model: RecentDocuments.files

                    // Its own Action: Qt syncs a native item added after
                    // the menu was built only when its action changes.
                    delegate: MenuItem {
                        id: recentItem

                        required property url modelData

                        action: Action {
                            onTriggered: root.app.openFiles([recentItem.modelData], root)
                        }
                    }

                    onObjectAdded: (index, object) => {
                        recentMenu.insertItem(index, object);
                        const item = object as MenuItem;
                        item.action.text = Qt.binding(() => decodeURIComponent(item.modelData.toString().split("/").pop()));
                    }
                    onObjectRemoved: (index, object) => recentMenu.removeItem(object)
                }
                MenuSeparator {}
                MenuItem {
                    action: Action {
                        text: qsTr("Clear Menu")
                        enabled: RecentDocuments.files.length > 0
                        onTriggered: RecentDocuments.clear()
                    }
                }
            }
            MenuSeparator {}
            MenuItem {
                action: Action {
                    text: qsTr("Save")
                    shortcut: StandardKey.Save
                    enabled: root.activeDocument !== null
                    onTriggered: root.save(root.activeDocument, null)
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Save As…")
                    shortcut: StandardKey.SaveAs
                    enabled: root.activeDocument !== null
                    onTriggered: root.saveAs(root.activeDocument, null)
                }
            }
            MenuSeparator {}
            MenuItem {
                action: Action {
                    text: qsTr("Close Document")
                    shortcut: StandardKey.Close
                    enabled: root.activeDocument !== null
                    onTriggered: root.closeDocument(root.activeDocument)
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Close All Documents")
                    shortcut: "Ctrl+Shift+W"
                    enabled: OpenDocuments.count > 0
                    onTriggered: root.closeAllDocuments()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Close Folder")
                    enabled: root.folder.toString() !== ""
                    onTriggered: {
                        root.app.saveFolder(root);
                        root.folder = "";
                        root.sidebarVisible = false;
                    }
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Close Window")
                    // ⇧⌘W closes all documents.
                    shortcut: "Ctrl+Alt+W"
                    onTriggered: root.close()
                }
            }
        }
        Menu {
            title: qsTr("Edit")

            MenuItem {
                action: Action {
                    text: qsTr("Undo")
                    enabled: root.editor?.canUndo ?? false
                    shortcut: StandardKey.Undo
                    onTriggered: root.editor.undo()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Redo")
                    enabled: root.editor?.canRedo ?? false
                    shortcut: StandardKey.Redo
                    onTriggered: root.editor.redo()
                }
            }
            MenuSeparator {}
            MenuItem {
                action: Action {
                    id: cutAction

                    text: qsTr("Cut")
                    // The sidebar's file while its tree has the focus, as in the Finder.
                    enabled: sidebar.treeFocused ? sidebar.canCut : (root.editor?.hasSelection ?? false)
                    shortcut: StandardKey.Cut
                    onTriggered: sidebar.treeFocused ? sidebar.cutSelected(true) : root.editor.cut()
                }
            }
            MenuItem {
                action: Action {
                    id: copyAction

                    text: qsTr("Copy")
                    enabled: sidebar.treeFocused || (root.editor?.hasSelection ?? false)
                    shortcut: StandardKey.Copy
                    onTriggered: sidebar.treeFocused ? sidebar.cutSelected(false) : root.editor.copy()
                }
            }
            MenuItem {
                action: Action {
                    id: pasteAction

                    text: qsTr("Paste")
                    enabled: sidebar.treeFocused ? sidebar.canPaste : (root.editor?.canPaste ?? false)
                    shortcut: StandardKey.Paste
                    onTriggered: sidebar.treeFocused ? sidebar.pasteHere() : root.editor.paste()
                }
            }
            MenuItem {
                action: Action {
                    id: selectAllAction

                    text: qsTr("Select All")
                    enabled: root.editor !== null
                    shortcut: StandardKey.SelectAll
                    onTriggered: root.editor.selectAll()
                }
            }
            MenuItem {
                action: Action {
                    id: selectNextOccurrenceAction

                    // ⌃D: ⌘D splits. Typing then happens at every one.
                    text: qsTr("Select Next Occurrence")
                    enabled: root.editor !== null
                    shortcut: "Meta+D"
                    onTriggered: root.editor.selectNextOccurrence()
                }
            }
            MenuItem {
                // As Qt Creator's Select Block Up; again for the block around it.
                action: Action {
                    id: selectBlockAction

                    text: qsTr("Select Block")
                    enabled: root.editor !== null
                    shortcut: "Ctrl+U"
                    onTriggered: root.editor.selectBlock()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Unselect Block")
                    enabled: root.editor !== null
                    shortcut: "Ctrl+Shift+U"
                    onTriggered: root.editor.unselectBlock()
                }
            }
            MenuSeparator {}
            MenuItem {
                action: Action {
                    text: qsTr("Shift Right")
                    enabled: root.editor !== null
                    shortcut: "Ctrl+]"
                    onTriggered: root.editor.indent()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Shift Left")
                    enabled: root.editor !== null
                    shortcut: "Ctrl+["
                    onTriggered: root.editor.outdent()
                }
            }
            MenuItem {
                // ⌘/ as in Xcode, VS Code and Qt Creator.
                action: Action {
                    id: lineCommentAction

                    text: qsTr("Toggle Line Comment")
                    enabled: root.editor !== null
                    shortcut: "Ctrl+/"
                    onTriggered: root.editor.toggleLineComment()
                }
            }
            MenuItem {
                action: Action {
                    id: blockCommentAction

                    text: qsTr("Toggle Block Comment")
                    enabled: root.editor !== null
                    shortcut: "Ctrl+Alt+/"
                    onTriggered: root.editor.toggleBlockComment()
                }
            }
            MenuItem {
                // As in Xcode.
                action: Action {
                    id: reindentAction

                    text: qsTr("Re-Indent")
                    enabled: root.editor !== null
                    shortcut: "Meta+I"
                    onTriggered: root.editor.reindent()
                }
            }
            MenuSeparator {}
            MenuItem {
                action: Action {
                    text: qsTr("Go to Line…")
                    enabled: root.activeDocument !== null
                    shortcut: "Ctrl+L"
                    onTriggered: root.activeView.showGoToLine()
                }
            }
            Menu {
                title: qsTr("Find")

                MenuItem {
                    action: Action {
                        text: qsTr("Find…")
                        enabled: root.activeDocument !== null
                        shortcut: StandardKey.Find
                        onTriggered: root.activeView.showSearch(false)
                    }
                }
                MenuItem {
                    action: Action {
                        text: qsTr("Find and Replace…")
                        enabled: root.activeDocument !== null
                        shortcut: "Ctrl+R"
                        onTriggered: root.activeView.showSearch(true)
                    }
                }
                MenuItem {
                    action: Action {
                        text: qsTr("Find Next")
                        enabled: (root.editor?.searchMatchCount ?? 0) > 0
                        shortcut: StandardKey.FindNext
                        onTriggered: root.editor.findNext()
                    }
                }
                MenuItem {
                    action: Action {
                        text: qsTr("Find Previous")
                        enabled: (root.editor?.searchMatchCount ?? 0) > 0
                        shortcut: StandardKey.FindPrevious
                        onTriggered: root.editor.findPrevious()
                    }
                }
                MenuItem {
                    action: Action {
                        id: useSelectionForFindAction

                        text: qsTr("Use Selection for Find")
                        enabled: root.editor?.hasSelection ?? false
                        shortcut: "Ctrl+E"
                        onTriggered: root.activeView.useSelectionForFind()
                    }
                }
            }
        }
        Menu {
            title: qsTr("View")

            MenuItem {
                // ⌃⌘S as the system's apps (⌘0, Xcode's, is Actual Size here).
                action: Action {
                    text: root.sidebarVisible ? qsTr("Hide Sidebar") : qsTr("Show Sidebar")
                    shortcut: "Ctrl+Meta+S"
                    onTriggered: root.sidebarVisible = !root.sidebarVisible
                }
            }
            MenuItem {
                // ⇧⌘. as in the Finder.
                action: Action {
                    text: qsTr("Show Hidden Files")
                    checkable: true
                    checked: Preferences.showHiddenFiles
                    shortcut: "Ctrl+Shift+."
                    onTriggered: Preferences.showHiddenFiles = checked
                }
            }
            MenuItem {
                action: Action {
                    id: collapseAllAction

                    text: qsTr("Collapse All Folders")
                    shortcut: "Ctrl+Alt+-"
                    enabled: root.sidebarVisible && root.folder.toString() !== ""
                    onTriggered: sidebar.collapseAll()
                }
            }
            MenuSeparator {}
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
            MenuItem {
                action: Action {
                    text: qsTr("Show Indent Guides")
                    checkable: true
                    checked: Preferences.showIndentGuides
                    onTriggered: Preferences.showIndentGuides = checked
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Show Minimap")
                    checkable: true
                    checked: Preferences.showMinimap
                    onTriggered: Preferences.showMinimap = checked
                }
            }
            // As in Xcode's Editor > Code Folding.
            Menu {
                title: qsTr("Code Folding")

                MenuItem {
                    action: Action {
                        id: foldAction

                        text: qsTr("Fold")
                        shortcut: "Ctrl+Alt+Left"
                        enabled: root.editor !== null
                        onTriggered: root.editor.fold()
                    }
                }
                MenuItem {
                    action: Action {
                        id: unfoldAction

                        text: qsTr("Unfold")
                        shortcut: "Ctrl+Alt+Right"
                        enabled: root.editor !== null
                        onTriggered: root.editor.unfold()
                    }
                }
                MenuItem {
                    action: Action {
                        text: qsTr("Unfold All")
                        shortcut: "Ctrl+Alt+Shift+Right"
                        enabled: root.editor !== null
                        onTriggered: root.editor.unfoldAll()
                    }
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
                        checked: Syntaxes.themeFamily(Preferences.theme) === modelData
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
        Menu {
            title: qsTr("Navigate")

            MenuItem {
                action: Action {
                    id: goToDefinitionAction

                    text: qsTr("Go to Definition")
                    // F2 renames in the sidebar while it has the focus.
                    enabled: (root.editor?.document?.languageServer ?? "") !== "" && !sidebar.treeFocused
                    shortcut: "F2"
                    onTriggered: root.activeView.goToDefinition(root.editor.cursorPosition)
                }
            }
            MenuItem {
                action: Action {
                    id: switchHeaderSourceAction

                    text: qsTr("Switch Header/Source")
                    enabled: (root.activeDocument?.fileUrl.toString() ?? "") !== "" && ["C", "C++", "ISO C++", "Objective-C", "Objective-C++"].includes(root.editor?.syntax ?? "")
                    shortcut: "F4"
                    onTriggered: root.activeView.switchHeaderSource()
                }
            }
            MenuSeparator {}

            MenuItem {
                action: Action {
                    text: qsTr("Go Back")
                    enabled: root.activeView?.history.canGoBack ?? false
                    shortcut: "Meta+Ctrl+Left"
                    onTriggered: root.activeView.goBack()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Go Forward")
                    enabled: root.activeView?.history.canGoForward ?? false
                    shortcut: "Meta+Ctrl+Right"
                    onTriggered: root.activeView.goForward()
                }
            }
            MenuSeparator {}
            MenuItem {
                action: Action {
                    text: qsTr("Next Document")
                    enabled: OpenDocuments.count > 1
                    shortcut: "Ctrl+}"
                    onTriggered: root.app.showAdjacentDocument(root, 1)
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Previous Document")
                    enabled: OpenDocuments.count > 1
                    shortcut: "Ctrl+{"
                    onTriggered: root.app.showAdjacentDocument(root, -1)
                }
            }
        }
        Menu {
            id: windowMenu

            // Where the list of windows starts: after the items above it.
            readonly property int windowListOffset: 9

            title: qsTr("Window")

            MenuItem {
                action: Action {
                    text: qsTr("Minimize")
                    shortcut: "Ctrl+M"
                    onTriggered: root.showMinimized()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Zoom")
                    onTriggered: root.visibility === Window.Maximized ? root.showNormal() : root.showMaximized()
                }
            }
            MenuSeparator {}
            MenuItem {
                action: Action {
                    text: qsTr("Split Side by Side")
                    enabled: root.activeView !== null
                    shortcut: "Ctrl+D"
                    onTriggered: windowSplits.split(root.activeView, Qt.Horizontal, {
                        document: root.activeDocument
                    })
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Split Top and Bottom")
                    enabled: root.activeView !== null
                    shortcut: "Ctrl+Shift+D"
                    onTriggered: windowSplits.split(root.activeView, Qt.Vertical, {
                        document: root.activeDocument
                    })
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Remove Split")
                    enabled: windowSplits.count > 1
                    shortcut: "Ctrl+Alt+D"
                    onTriggered: windowSplits.remove(root.activeView)
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Next Split")
                    enabled: windowSplits.count > 1
                    shortcut: "Ctrl+Alt+]"
                    onTriggered: windowSplits.activateAdjacent(1)
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Previous Split")
                    enabled: windowSplits.count > 1
                    shortcut: "Ctrl+Alt+["
                    onTriggered: windowSplits.activateAdjacent(-1)
                }
            }
            MenuSeparator {}

            // Every window by name, the active one checked.
            Instantiator {
                model: root.app.windows

                delegate: MenuItem {
                    id: windowEntry

                    required property EditorWindow modelData

                    text: modelData.title
                    checkable: true
                    checked: modelData === root
                    onTriggered: {
                        windowEntry.checked = Qt.binding(() => windowEntry.modelData === root);
                        windowEntry.modelData.raise();
                        windowEntry.modelData.requestActivate();
                    }
                }

                onObjectAdded: (index, object) => windowMenu.insertItem(windowMenu.windowListOffset + index, object)
                onObjectRemoved: (index, object) => windowMenu.removeItem(object)
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
                    onTriggered: root.app.showSettings()
                }
            }
            MenuItem {
                text: "About Kode"
                onTriggered: root.app.showAbout()
            }
            MenuItem {
                text: "About Qt"
                onTriggered: root.app.showAboutQt()
            }
        }
    }

    SplitView {
        id: content

        // The title bar's height: the sidebar and the splits reach the window's top.
        readonly property real titleBarHeight: Math.max(root.SafeArea.margins.top, 34)

        anchors.fill: parent
        orientation: Qt.Horizontal

        handle: DividerHandle {
            color: Qt.alpha(SystemColors.label, 0.12)
        }

        Sidebar {
            id: sidebar

            visible: root.sidebarVisible
            SplitView.preferredWidth: root.sidebarWidth
            SplitView.minimumWidth: 160
            SplitView.maximumWidth: 480
            folder: root.folder
            currentFile: root.activeDocument ? root.activeDocument.fileUrl : ""
            currentText: root.activeDocument ? root.activeDocument.textDocument : null
            topInset: content.titleBarHeight
            nativeWindow: windowStyle
            cutAction: cutAction
            copyAction: copyAction
            pasteAction: pasteAction
            collapseAllAction: collapseAllAction
            onFileChosen: file => root.app.openFiles([file], root)
            onOpenFolderRequested: folderDialog.open()
            onFolderChosen: folder => root.app.openFolder(folder, root)
            // Its documents close first (unsaved ones asked about); Cancel keeps it all.
            onTrashRequested: url => {
                const documents = OpenDocuments.documentsIn(url);
                root.review(documents, () => {
                    if (!sidebar.trash(url))
                        return;
                    for (const document of documents)
                        OpenDocuments.close(document as Document);
                });
            }
        }

        PaneLayout {
            SplitView.fillWidth: true
            splits: windowSplits
            // Those at the top fill the title bar; at its left, after the sidebar button.
            topInset: content.titleBarHeight
            leadingInset: root.sidebarVisible ? 0 : sidebarButton.x + sidebarButton.width - 6
            nativeWindow: windowStyle
            onDocumentChosen: (view, document) => root.showDocument(document, view)
            onReviewRequested: (document, action) => root.review([document], action)
            onCloseDocumentRequested: document => root.closeDocument(document)
            onNewDocumentRequested: root.newDocument()
            onOpenRequested: openDialog.open()
            onOpenFolderRequested: folderDialog.open()
            onContextMenuRequested: editorMenu.popup()
        }
    }

    // After the window buttons, as Xcode's: it stays there with the sidebar hidden.
    BarButton {
        id: sidebarButton

        x: 8 + (root.visibility === Window.FullScreen ? 0 : windowStyle.leadingInset)
        y: (content.titleBarHeight - height) / 2
        icon.name: "sidebar.left"
        Accessible.name: root.sidebarVisible ? qsTr("Hide sidebar") : qsTr("Show sidebar")
        toolTip: root.sidebarVisible ? qsTr("Hide Sidebar") : qsTr("Show Sidebar")
        onClicked: root.sidebarVisible = !root.sidebarVisible
    }

    // Ctrl+Tab, Ctrl+Shift+Tab: the active split's switcher (Meta is Control on macOS).
    Shortcut {
        sequences: ["Meta+Tab"]
        enabled: root.activeView !== null
        onActivated: root.activeView.switchDocuments(1)
    }
    Shortcut {
        sequences: ["Meta+Shift+Tab", "Meta+Backtab", "Meta+Shift+Backtab"]
        enabled: root.activeView !== null
        onActivated: root.activeView.switchDocuments(-1)
    }

    // Files dropped on the window open in the split they were dropped on.
    DropArea {
        id: dropArea

        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: drop => {
            if (!drop.hasUrls)
                return;
            const target = windowSplits.views.find(view => {
                const pane = view as EditorPane;
                return pane.contains(pane.mapFromItem(dropArea, drop.x, drop.y));
            });
            if (target)
                windowSplits.activate(target);
            root.app.openFiles(drop.urls, root);
            drop.acceptProposedAction();
        }
    }
}
