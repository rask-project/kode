pragma ComponentBehavior: Bound

import QtQuick

import Rask.Application
import Rask.Dialogs
import Rask.Updater

import Kode

// The app: its windows (EditorWindow) over the documents they share
// (OpenDocuments), opening files, closing documents in every split that
// shows them, each folder's session (saved when its window closes,
// reopened with the folder) and the review of unsaved documents before
// quitting. One Settings window for all of them.
Item {
    id: app

    // A file given on the command line, opened at start.
    required property url initialFile

    property list<EditorWindow> windows: []
    property EditorWindow lastActiveWindow: null

    // A window with the folder and splits of `state` (see
    // EditorWindow.state()), else one split with a new document (none in a
    // window opened on a folder: the start page).
    function openWindow(state: var): EditorWindow {
        const folder = state && OpenDocuments.isFolder(state.folder ?? "") ? state.folder : "";
        const window = windowComponent.createObject(app, {
            folder: folder,
            sidebarVisible: folder !== "" && (state.sidebar ?? true),
            sidebarWidth: state && state.sidebarWidth > 0 ? state.sidebarWidth : 240
        }) as EditorWindow;
        app.windows.push(window);
        if (state && state.width > 0) {
            window.x = state.x;
            window.y = state.y;
            window.width = state.width;
            window.height = state.height;
        }
        if (!state || !window.splits.restore(state.splits ?? []))
            window.splits.addView({
                document: folder !== "" ? null : OpenDocuments.create(),
                startEmpty: folder !== ""
            });
        if (state && state.visibility === "fullscreen")
            window.showFullScreen();
        else if (state && state.visibility === "maximized")
            window.showMaximized();
        else
            window.show();
        window.raise();
        window.requestActivate();
        app.lastActiveWindow = window;
        // Once a window's menu bar exists: it adds "Check for Updates…" to the app menu.
        Updater.start();
        return window;
    }

    // The splits showing `document`, in every window.
    function viewsOf(document: Document): list<EditorPane> {
        const views = [];
        for (const window of app.windows) {
            for (const view of window.splits.views) {
                if ((view as EditorPane).document === document)
                    views.push(view as EditorPane);
            }
        }
        return views;
    }

    function isShown(document: Document): bool {
        return app.viewsOf(document).length > 0;
    }

    // Each file in the active split of `window`. From outside the app
    // (`window` null: the Finder, the Dock) a file already in view is shown
    // where it is, else in the last active window. A folder goes to the sidebar.
    function openFiles(files: list<url>, window: EditorWindow) {
        for (const file of files) {
            if (OpenDocuments.isFolder(file)) {
                app.openFolder(file, window);
                continue;
            }
            const document = OpenDocuments.open(file);
            if (!document)
                continue;
            RecentDocuments.add(document.fileUrl);
            const shown = window ? [] : app.viewsOf(document);
            if (shown.length > 0) {
                const owner = app.windows.find(candidate => candidate.splits.views.includes(shown[0]));
                owner.splits.activate(shown[0]);
                owner.raise();
                owner.requestActivate();
                continue;
            }
            const target = window ?? app.lastActiveWindow ?? app.openWindow(null);
            target.showDocument(document, null);
            target.raise();
            target.requestActivate();
        }
    }

    // `folder` in a window's sidebar, with the documents it had open when
    // its window last closed (none the first time). From outside the app
    // (`window` null) a window already on it comes forward, else a new one
    // opens with its splits, replacing a window nobody used yet. In `window`
    // (Open Folder, the sidebar), it takes the folder, saving the one it had.
    function openFolder(given: url, window: EditorWindow) {
        const folder = OpenDocuments.cleanFolder(given);
        RecentDocuments.add(folder);
        const state = Session.folderState(folder);
        if (window) {
            app.saveFolder(window);
            window.folder = folder;
            window.sidebarVisible = true;
            OpenDocuments.restore(state.documents ?? []);
            const shown = (state.splits ?? []).find(node => node.file);
            const document = shown ? OpenDocuments.find(shown.file) : null;
            if (document)
                window.showDocument(document, null);
            return;
        }
        const owner = app.windows.find(candidate => candidate.folder.toString() === folder.toString());
        if (owner) {
            owner.raise();
            owner.requestActivate();
            return;
        }
        const idle = app.windows.filter(candidate => app.isUnused(candidate));
        OpenDocuments.restore(state.documents ?? []);
        app.openWindow(Object.assign({}, state, {
            folder: folder,
            sidebar: true
        }));
        for (const unused of idle)
            app.closeUnused(unused);
    }

    // No folder, and nothing but the start page or a new untouched document.
    function isUnused(window: EditorWindow): bool {
        return window.folder.toString() === "" && window.splits.views.every(view => !(view as EditorPane).document || (view as EditorPane).untouched);
    }

    function closeUnused(window: EditorWindow) {
        const documents = window.splits.views.map(view => (view as EditorPane).document).filter(document => document);
        window.close();
        for (const document of documents) {
            if (!app.isShown(document as Document))
                OpenDocuments.close(document as Document);
        }
    }

    // What `window` has open in its folder, for the next time the folder opens.
    function saveFolder(window: EditorWindow) {
        if (window.folder.toString() === "")
            return;
        const inSplits = window.splits.views.map(view => (view as EditorPane).fileUrl.toString());
        const inFolder = OpenDocuments.documentsIn(window.folder).map(document => (document as Document).fileUrl.toString());
        // Oldest first, so reopening them in order keeps the most recent on top.
        const recentFirst = OpenDocuments.documents.map(document => (document as Document).fileUrl.toString());
        const files = recentFirst.filter(file => file.startsWith("file:") && (inSplits.includes(file) || inFolder.includes(file))).reverse();
        Session.saveFolder(window.folder, Object.assign(window.state(), {
            documents: files
        }));
    }

    // The next (1) or previous (-1) document by name, in `window`'s active split.
    function showAdjacentDocument(window: EditorWindow, step: int) {
        const documents = OpenDocuments.sortedDocuments;
        const index = documents.indexOf(window.activeDocument);
        window.showDocument(documents[(index + step + documents.length) % documents.length] as Document, null);
    }

    function noteActiveWindow(window: EditorWindow) {
        app.lastActiveWindow = window;
    }

    function windowClosed(window: EditorWindow) {
        // Quitting saved them all before closing anything.
        if (!QuitGuard.quitting)
            app.saveFolder(window);
        const index = app.windows.indexOf(window);
        if (index >= 0)
            app.windows.splice(index, 1);
        if (app.lastActiveWindow === window)
            app.lastActiveWindow = app.windows.length > 0 ? app.windows[app.windows.length - 1] : null;
        // Settings would otherwise keep the app running without an editor.
        if (app.windows.length === 0)
            settingsWindow.close();
        Qt.callLater(() => window.destroy());
    }

    // Reviews every unsaved document in the last active window, then quits.
    function quit() {
        const window = app.lastActiveWindow ?? app.windows[0];
        if (!window) {
            QuitGuard.quit();
            return;
        }
        window.review(OpenDocuments.modifiedDocuments(), () => {
            for (const open of app.windows)
                app.saveFolder(open);
            QuitGuard.quit();
        });
    }

    function showSettings() {
        settingsWindow.show();
        settingsWindow.raise();
        settingsWindow.requestActivate();
    }

    function showAbout() {
        aboutDialog.open();
    }

    function showAboutQt() {
        aboutQtDialog.open();
    }

    Component {
        id: windowComponent

        EditorWindow {
            app: app
        }
    }

    SettingsWindow {
        id: settingsWindow
    }

    AboutDialog {
        id: aboutDialog

        version: qsTr("Version %1").arg(Qt.application.version)
    }

    AboutQtDialog {
        id: aboutQtDialog
    }

    // ssh's prompts while connecting to a host (kode-askpass), one at a time.
    TextPrompt {
        id: sshPrompt

        property int promptId: -1

        window: app.lastActiveWindow
        informativeText: qsTr("Kode is connecting to this host over SSH.")
        onFinished: (button, value) => Hosts.answer(sshPrompt.promptId, button === 0, value)
    }

    Alert {
        id: sshQuestion

        property int promptId: -1

        window: app.lastActiveWindow
        style: Alert.Warning
        buttons: [qsTr("Connect"), qsTr("Cancel")]
        onFinished: button => Hosts.answer(sshQuestion.promptId, button === 0, "yes")
    }

    Connections {
        target: Hosts

        function onPromptRequested(id: int, host: string, text: string, kind: int) {
            if (kind === Hosts.Confirm) {
                sshQuestion.promptId = id;
                sshQuestion.text = qsTr("Connect to “%1”?").arg(host);
                sshQuestion.informativeText = text.trim();
                sshQuestion.open();
                return;
            }
            sshPrompt.promptId = id;
            sshPrompt.text = text.trim();
            sshPrompt.secure = kind === Hosts.Secret;
            sshPrompt.open();
        }
    }

    Alert {
        id: errorAlert

        window: app.lastActiveWindow
        style: Alert.Critical
        text: qsTr("The document could not be opened or saved.")
    }

    Alert {
        id: changedAlert

        property Document document: null

        window: app.lastActiveWindow
        style: Alert.Warning
        text: qsTr("“%1” changed on disk.").arg(document ? document.fileName : "")
        informativeText: qsTr("Reload it and lose your changes here, or keep editing your version?")
        buttons: [qsTr("Reload"), qsTr("Keep Mine")]
        onFinished: button => {
            if (button === 0 && changedAlert.document)
                changedAlert.document.reload();
            else if (changedAlert.document)
                changedAlert.document.keepMine();
            changedAlert.document = null;
        }
    }

    Connections {
        target: OpenDocuments

        function onErrorOccurred(message: string) {
            errorAlert.informativeText = message;
            errorAlert.open();
        }
        function onChangedOnDisk(document: Document) {
            changedAlert.document = document;
            changedAlert.open();
        }
    }

    // The language servers follow Settings.
    Binding {
        target: OpenDocuments.languageServers
        property: "enabled"
        value: Preferences.languageServers
    }
    Binding {
        target: OpenDocuments.languageServers
        property: "searchFolders"
        value: Preferences.languageServerFolders
    }
    Binding {
        target: OpenDocuments.languageServers
        property: "clangdThreads"
        value: Preferences.clangdThreads
    }
    Binding {
        target: OpenDocuments.languageServers
        property: "customServers"
        value: Preferences.customLanguageServers
    }

    Connections {
        target: FileOpenHandler

        function onFilesOpened(files: list<url>) {
            app.openFiles(files, null);
        }
    }

    Connections {
        target: QuitGuard

        function onQuitRequested() {
            app.quit();
        }
    }

    // Launched to open a folder or files (the Finder, `kode`), they arrive
    // just after launching; launched alone, a window without a folder.
    Timer {
        id: launchTimer

        interval: 300
        onTriggered: {
            if (app.windows.length === 0)
                app.openWindow(null);
        }
    }

    Component.onCompleted: {
        if (app.initialFile.toString() !== "")
            app.openFiles([app.initialFile], null);
        else
            launchTimer.start();
    }
}
