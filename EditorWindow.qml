pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import Rask.Application
import Rask.Controls
import Rask.Dialogs
import Rask.Platform
import Rask.Window

import Tekst

// An editor window: tabs in the title bar (Rask.Controls' TabStrip), each a
// tree of split panes (TabManager, PaneLayout), the menus, and the file
// dialogs and "Save changes?" sheet for its panes. Main.qml keeps the
// window list and moves tabs and panes between windows.
ApplicationWindow {
    id: root

    required property Main app
    // Unsaved changes were reviewed: the window closes without asking.
    property bool closeApproved: false
    // Panes left to review before `reviewDone` runs (see review()).
    property list<EditorPane> reviewQueue: []
    property var reviewDone: null

    readonly property TabManager tabs: tabManager
    readonly property EditorPane activePane: tabManager.activePane as EditorPane
    readonly property EditorView editor: activePane ? activePane.editor : null

    // The pane header dragged from this window, drawn under the pointer.
    property EditorPane paneDragPane: null
    property real paneDragGrabX: 0
    property real paneDragGrabY: 0
    property real paneDragWidth: 0
    property real paneDragGlobalX: 0
    property real paneDragGlobalY: 0

    // Saves `pane`, asking for a file when it has none, then runs `then`.
    function save(pane: EditorPane, then) {
        if (pane.fileUrl.toString() === "") {
            root.saveAs(pane, then);
        } else if (pane.document.save() && then) {
            then();
        }
    }

    function saveAs(pane: EditorPane, then) {
        saveDialog.pane = pane;
        saveDialog.then = then;
        saveDialog.selectedFile = pane.fileUrl;
        saveDialog.open();
    }

    // Runs `then` once every modified pane of `panes` is saved or its
    // changes discarded, asking about each in turn; Cancel stops.
    function review(panes: list<EditorPane>, then) {
        root.reviewQueue = panes.filter(pane => pane.modified);
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
        const pane = root.reviewQueue[0];
        tabManager.activatePane(pane);
        saveAlert.pane = pane;
        saveAlert.open();
    }

    function allPanes(): list<EditorPane> {
        return tabManager.panes.map(pane => pane as EditorPane);
    }

    function closePane(pane: EditorPane) {
        root.review([pane], () => tabManager.closePane(pane));
    }

    function closeTab(index: int) {
        const tab = tabManager.tabs[index];
        const panes = tabManager.tabPanes(index).map(pane => pane as EditorPane);
        // Found again afterwards: the tabs may move while the sheets show.
        root.review(panes, () => tabManager.closeTab(tabManager.tabs.indexOf(tab)));
    }

    function newTab(): EditorPane {
        return tabManager.addTab({}) as EditorPane;
    }

    // The saved geometry, tabs and files, for the next launch.
    function state(): var {
        return {
            x: root.x,
            y: root.y,
            width: root.width,
            height: root.height,
            visibility: root.visibility === Window.FullScreen ? "fullscreen" : (root.visibility === Window.Maximized ? "maximized" : "normal"),
            tabs: tabManager.state()
        };
    }

    function tabInsertionIndexAtGlobal(globalX: real, globalY: real): int {
        return tabStrip.insertionIndexAtGlobal(globalX, globalY);
    }

    // What a pane dragged to a global point lands on here: the tab strip (a
    // tab of its own at `index`) or a pane, split on the edge nearest the
    // pointer. Null over the dragged pane or elsewhere.
    function paneDropTarget(globalX: real, globalY: real, pane: EditorPane): var {
        const point = content.mapFromGlobal(globalX, globalY);
        if (point.x < 0 || point.y < 0 || point.x >= content.width || point.y >= content.height)
            return null;
        if (point.y < tabStrip.height) {
            const index = tabStrip.insertionIndexAtGlobal(globalX, globalY);
            return {
                kind: "tab",
                index: index,
                rect: tabStrip.insertionMarkerRect(index)
            };
        }
        const page = pages.itemAt(tabManager.currentIndex) as PaneLayout;
        const leaf = page ? page.leafAtGlobal(globalX, globalY) : null;
        const target = leaf ? leaf.pane : null;
        if (!target || target === pane)
            return null;
        const local = leaf.mapFromGlobal(globalX, globalY);
        const dx = local.x / leaf.width - 0.5;
        const dy = local.y / leaf.height - 0.5;
        const edge = Math.abs(dx) > Math.abs(dy) ? (dx < 0 ? TabManager.EdgeLeft : TabManager.EdgeRight) : (dy < 0 ? TabManager.EdgeTop : TabManager.EdgeBottom);
        const area = leaf.mapToItem(content, 0, 0, leaf.width, leaf.height);
        let rect;
        switch (edge) {
        case TabManager.EdgeLeft:
            rect = Qt.rect(area.x, area.y, area.width / 2, area.height);
            break;
        case TabManager.EdgeRight:
            rect = Qt.rect(area.x + area.width / 2, area.y, area.width / 2, area.height);
            break;
        case TabManager.EdgeTop:
            rect = Qt.rect(area.x, area.y, area.width, area.height / 2);
            break;
        default:
            rect = Qt.rect(area.x, area.y + area.height / 2, area.width, area.height / 2);
            break;
        }
        return {
            kind: "pane",
            pane: target,
            edge: edge,
            rect: rect
        };
    }

    // Highlights where a dragged pane lands (null hides it).
    function showPaneDrop(drop) {
        paneDropIndicator.drop = drop;
    }

    width: 900
    height: 640
    minimumWidth: 400
    minimumHeight: 240
    // Feeds the Window menu and the window switcher; the tabs show in the title bar.
    title: activePane ? activePane.title : "Tekst"
    flags: Qt.Window | Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint
    color: "transparent"
    // ApplicationWindow pads its content by the safe area; the tab strip draws there itself.
    topPadding: 0

    onClosing: close => {
        if (root.closeApproved || QuitGuard.quitting || !root.allPanes().some(pane => pane.modified)) {
            root.app.windowClosed(root);
            return;
        }
        close.accepted = false;
        root.review(root.allPanes(), () => {
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
        representedFile: root.activePane ? root.activePane.fileUrl : ""
        documentEdited: root.activePane ? root.activePane.modified : false
    }

    TabManager {
        id: tabManager

        // The last tab went: everything in it was reviewed already.
        onEmptied: {
            root.closeApproved = true;
            root.close();
        }
        // The manager's pane, not root.activePane: that binding may not
        // have followed yet.
        onActivePaneChanged: {
            const pane = tabManager.activePane as EditorPane;
            if (pane)
                pane.focusEditor();
        }
    }

    FileDialog {
        id: openDialog

        title: qsTr("Open")
        fileMode: FileDialog.OpenFiles
        onAccepted: root.app.openFiles(selectedFiles, root)
    }

    FileDialog {
        id: saveDialog

        property EditorPane pane: null
        // Run after a successful save (closing, quitting…).
        property var then: null

        title: qsTr("Save As")
        fileMode: FileDialog.SaveFile
        onAccepted: {
            const then = saveDialog.then;
            const pane = saveDialog.pane;
            saveDialog.then = null;
            saveDialog.pane = null;
            if (pane && pane.document.saveAs(selectedFile)) {
                RecentDocuments.add(selectedFile);
                if (then)
                    then();
            }
        }
        onRejected: {
            saveDialog.then = null;
            saveDialog.pane = null;
            root.reviewQueue = [];
            root.reviewDone = null;
        }
    }

    Alert {
        id: saveAlert

        property EditorPane pane: null

        window: root
        style: Alert.Warning
        text: qsTr("Do you want to save the changes made to “%1”?").arg(pane ? pane.title : "")
        informativeText: qsTr("Your changes will be lost if you don’t save them.")
        buttons: [qsTr("Save"), qsTr("Don’t Save"), qsTr("Cancel")]
        onFinished: button => {
            const pane = saveAlert.pane;
            saveAlert.pane = null;
            if (button === 2 || !pane) {
                root.reviewQueue = [];
                root.reviewDone = null;
                return;
            }
            const next = () => {
                root.reviewQueue = root.reviewQueue.filter(queued => queued !== pane);
                root.reviewNext();
            };
            if (button === 0)
                root.save(pane, next);
            else
                next();
        }
    }

    Instantiator {
        model: 9

        delegate: Shortcut {
            required property int index

            sequence: "Ctrl+" + (index + 1)
            context: Qt.WindowShortcut
            // ⌘9 is the last tab, as in Safari.
            onActivated: tabManager.currentIndex = index === 8 ? tabManager.tabs.length - 1 : index
        }
    }

    // Control-Tab and Control-Shift-Tab go through the tabs too.
    Shortcut {
        sequence: "Meta+Tab"
        context: Qt.WindowShortcut
        onActivated: tabManager.selectAdjacentTab(1)
    }
    Shortcut {
        sequence: "Meta+Shift+Tab"
        context: Qt.WindowShortcut
        onActivated: tabManager.selectAdjacentTab(-1)
    }

    menuBar: MenuBar {
        Menu {
            title: qsTr("File")

            MenuItem {
                action: Action {
                    text: qsTr("New Window")
                    shortcut: StandardKey.New
                    onTriggered: root.app.openWindow(null)
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("New Tab")
                    shortcut: StandardKey.AddTab
                    onTriggered: root.newTab()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Open…")
                    shortcut: StandardKey.Open
                    onTriggered: openDialog.open()
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
                    enabled: root.activePane !== null
                    onTriggered: root.save(root.activePane, null)
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Save As…")
                    shortcut: StandardKey.SaveAs
                    enabled: root.activePane !== null
                    onTriggered: root.saveAs(root.activePane, null)
                }
            }
            MenuSeparator {}
            MenuItem {
                action: Action {
                    text: qsTr("Close")
                    shortcut: StandardKey.Close
                    enabled: root.activePane !== null
                    onTriggered: root.closePane(root.activePane)
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Close Tab")
                    shortcut: "Ctrl+Alt+W"
                    enabled: tabManager.currentIndex >= 0
                    onTriggered: root.closeTab(tabManager.currentIndex)
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Close Window")
                    shortcut: "Ctrl+Shift+W"
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
                    text: qsTr("Cut")
                    enabled: root.editor?.hasSelection ?? false
                    shortcut: StandardKey.Cut
                    onTriggered: root.editor.cut()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Copy")
                    enabled: root.editor?.hasSelection ?? false
                    shortcut: StandardKey.Copy
                    onTriggered: root.editor.copy()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Paste")
                    enabled: root.editor?.canPaste ?? false
                    shortcut: StandardKey.Paste
                    onTriggered: root.editor.paste()
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Select All")
                    enabled: root.editor !== null
                    shortcut: StandardKey.SelectAll
                    onTriggered: root.editor.selectAll()
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
            MenuSeparator {}
            MenuItem {
                action: Action {
                    text: qsTr("Go to Line…")
                    enabled: root.activePane !== null
                    shortcut: "Ctrl+L"
                    onTriggered: root.activePane.showGoToLine()
                }
            }
            Menu {
                title: qsTr("Find")

                MenuItem {
                    action: Action {
                        text: qsTr("Find…")
                        enabled: root.activePane !== null
                        shortcut: StandardKey.Find
                        onTriggered: root.activePane.showSearch(false)
                    }
                }
                MenuItem {
                    action: Action {
                        text: qsTr("Find and Replace…")
                        enabled: root.activePane !== null
                        shortcut: "Ctrl+R"
                        onTriggered: root.activePane.showSearch(true)
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
                        text: qsTr("Use Selection for Find")
                        enabled: root.editor?.hasSelection ?? false
                        shortcut: "Ctrl+E"
                        onTriggered: root.activePane.useSelectionForFind()
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
            id: windowMenu

            // Where the list of windows starts: after the items above it.
            readonly property int windowListOffset: 10

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
                    text: qsTr("Split Horizontally")
                    enabled: root.activePane !== null
                    shortcut: "Ctrl+D"
                    onTriggered: tabManager.split(root.activePane, Qt.Horizontal)
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Split Vertically")
                    enabled: root.activePane !== null
                    shortcut: "Ctrl+Shift+D"
                    onTriggered: tabManager.split(root.activePane, Qt.Vertical)
                }
            }
            MenuSeparator {}
            MenuItem {
                action: Action {
                    text: qsTr("Show Previous Tab")
                    enabled: tabManager.tabs.length > 1
                    shortcut: "Ctrl+{"
                    onTriggered: tabManager.selectAdjacentTab(-1)
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Show Next Tab")
                    enabled: tabManager.tabs.length > 1
                    shortcut: "Ctrl+}"
                    onTriggered: tabManager.selectAdjacentTab(1)
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Move Tab to New Window")
                    enabled: tabManager.tabs.length > 1
                    onTriggered: root.app.moveTab(root, tabManager.currentIndex, null, 0)
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
                text: "About Tekst"
                onTriggered: root.app.showAbout()
            }
            MenuItem {
                text: "About Qt"
                onTriggered: root.app.showAboutQt()
            }
        }
    }

    // A tab's context menu.
    Menu {
        id: tabMenu

        property int index: -1

        popupType: Popup.Native

        MenuItem {
            text: qsTr("New Tab")
            onTriggered: root.newTab()
        }
        MenuItem {
            text: qsTr("Close Tab")
            onTriggered: root.closeTab(tabMenu.index)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Move to New Window")
            // A lone tab already is its own window.
            enabled: tabManager.tabs.length > 1
            // Deferred: the move rebuilds the tabs while this menu runs.
            onTriggered: {
                const index = tabMenu.index;
                Qt.callLater(() => root.app.moveTab(root, index, null, 0));
            }
        }
        Menu {
            id: moveMenu

            title: qsTr("Move to Window")
            enabled: root.app.windows.length > 1

            Instantiator {
                model: root.app.windows.filter(window => window !== root)

                delegate: MenuItem {
                    required property EditorWindow modelData

                    text: modelData.title
                    onTriggered: {
                        const index = tabMenu.index;
                        const target = modelData;
                        Qt.callLater(() => root.app.moveTab(root, index, target, -1));
                    }
                }

                onObjectAdded: (index, object) => moveMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => moveMenu.removeItem(object)
            }
        }
    }

    Item {
        id: content

        anchors.fill: parent

        TabStrip {
            id: tabStrip

            anchors.left: parent.left
            anchors.right: parent.right
            height: requiredHeight
            baseSafeTop: root.SafeArea.margins.top
            leadingInset: root.visibility === Window.FullScreen ? 0 : Math.max(76, windowStyle.leadingInset)
            nativeWindow: windowStyle
            tabs: tabManager.tabs
            currentIndex: tabManager.currentIndex
            onActivateRequested: index => tabManager.currentIndex = index
            onCloseRequested: index => root.closeTab(index)
            onReorderRequested: (fromIndex, toIndex) => tabManager.moveTab(fromIndex, toIndex)
            onAddRequested: root.newTab()
            onContextMenuRequested: index => {
                tabMenu.index = index;
                tabMenu.popup();
            }
            onWindowDropRequested: (index, globalX, globalY) => root.app.dropTab(root, index, globalX, globalY)
        }

        StackLayout {
            id: stack

            anchors.top: tabStrip.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            currentIndex: tabManager.currentIndex

            Repeater {
                id: pages

                model: tabManager.tabs

                delegate: PaneLayout {
                    required property Tab modelData

                    tabs: tabManager
                    tab: modelData
                    onReviewRequested: (pane, action) => root.review([pane], action)
                    onCloseRequested: pane => root.closePane(pane)
                    onPaneDragStarted: (pane, grabX, grabY, headerWidth) => {
                        root.paneDragGrabX = grabX;
                        root.paneDragGrabY = grabY;
                        root.paneDragWidth = headerWidth;
                        root.paneDragPane = pane;
                    }
                    onPaneDragMoved: (globalX, globalY) => {
                        root.paneDragGlobalX = globalX;
                        root.paneDragGlobalY = globalY;
                        if (root.paneDragPane)
                            root.app.hoverPane(root.paneDragPane, root, globalX, globalY);
                    }
                    onPaneDragFinished: (pane, globalX, globalY) => {
                        root.paneDragPane = null;
                        root.app.dropPane(pane, root, globalX, globalY);
                    }
                    onPaneDragCanceled: {
                        root.paneDragPane = null;
                        root.app.clearPaneDrops();
                    }
                }
            }
        }

        // Files dropped on the window open in tabs.
        DropArea {
            anchors.fill: parent
            keys: ["text/uri-list"]
            onDropped: drop => {
                if (!drop.hasUrls)
                    return;
                root.app.openFiles(drop.urls, root);
                drop.acceptProposedAction();
            }
        }

        SystemPalette {
            id: windowPalette

            colorGroup: SystemPalette.Active
        }

        // Where a dragged pane lands: a tinted half of the target pane, or a
        // bar between the tabs for a tab of its own.
        Rectangle {
            id: paneDropIndicator

            property var drop: null
            readonly property bool overTabs: drop !== null && drop.kind === "tab"

            visible: drop !== null
            z: 20
            x: drop ? drop.rect.x : 0
            y: drop ? drop.rect.y : 0
            width: drop ? drop.rect.width : 0
            height: drop ? drop.rect.height : 0
            radius: overTabs ? width / 2 : 10
            color: overTabs ? windowPalette.accent : Qt.alpha(windowPalette.accent, 0.18)
            border.width: overTabs ? 0 : 2
            border.color: windowPalette.accent
        }
    }

    // The dragged header follows the pointer in a frameless window of its
    // own, so it shows over other windows and the desktop too.
    Loader {
        active: root.paneDragPane !== null

        sourceComponent: Window {
            flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
            color: "transparent"
            visible: true
            x: root.paneDragGlobalX - Math.min(root.paneDragGrabX, width - 16)
            y: root.paneDragGlobalY - root.paneDragGrabY
            width: Math.max(120, Math.min(root.paneDragWidth, 320))
            height: 28

            Rectangle {
                anchors.fill: parent
                radius: height / 2
                color: Qt.alpha(windowPalette.window, 0.92)
                border.width: 1
                border.color: windowPalette.accent
            }

            Text {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                text: root.paneDragPane ? root.paneDragPane.title : ""
                color: windowPalette.windowText
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideMiddle
            }
        }
    }
}
