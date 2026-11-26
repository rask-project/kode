pragma ComponentBehavior: Bound

import QtQuick

import Rask.Application
import Rask.Dialogs

import Tekst

// The app: its windows (EditorWindow) over the documents they share
// (OpenDocuments), opening files, closing documents in every split that
// shows them, the session reopened at launch and the review of unsaved
// documents before quitting. One Settings window for all of them.
Item {
    id: app

    // A file given on the command line, opened at start.
    required property url initialFile

    property list<EditorWindow> windows: []
    property EditorWindow lastActiveWindow: null

    // A window with the splits of `state` (see EditorWindow.state()), else
    // one split with a new document.
    function openWindow(state: var): EditorWindow {
        const window = windowComponent.createObject(app) as EditorWindow;
        app.windows.push(window);
        if (state && state.width > 0) {
            window.x = state.x;
            window.y = state.y;
            window.width = state.width;
            window.height = state.height;
        }
        if (!state || !window.splits.restore(state.splits))
            window.splits.addView({
                document: OpenDocuments.create()
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
    // where it is, else in the last active window.
    function openFiles(files: list<url>, window: EditorWindow) {
        for (const file of files) {
            const document = OpenDocuments.open(file);
            if (!document)
                continue;
            RecentDocuments.add(file);
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
        // The last window closing ends the app: it is what reopens next time.
        if (app.windows.length === 1 && !QuitGuard.quitting)
            app.saveSession();
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

    function saveSession() {
        // Oldest first, so reopening them in order keeps the most recent on top.
        const files = OpenDocuments.documents.map(document => (document as Document).fileUrl.toString()).filter(file => file.startsWith("file:")).reverse();
        Session.save({
            documents: files,
            windows: app.windows.map(window => window.state())
        });
    }

    // Reviews every unsaved document in the last active window, then quits.
    function quit() {
        const window = app.lastActiveWindow ?? app.windows[0];
        if (!window) {
            QuitGuard.quit();
            return;
        }
        window.review(OpenDocuments.modifiedDocuments(), () => {
            app.saveSession();
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
            changedAlert.document = null;
        }
    }

    Connections {
        target: OpenDocuments

        // The splits showing a closing document move off it themselves
        // (EditorPane); with no document left, the windows close.
        function onDocumentClosing(document: Document) {
            if (OpenDocuments.other(document))
                return;
            for (const window of app.windows)
                window.closeApproved = true;
            Qt.callLater(() => app.windows.slice().forEach(window => window.close()));
        }
        function onErrorOccurred(message: string) {
            errorAlert.informativeText = message;
            errorAlert.open();
        }
        function onChangedOnDisk(document: Document) {
            changedAlert.document = document;
            changedAlert.open();
        }
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

    Component.onCompleted: {
        const session = Session.state();
        OpenDocuments.restore(session.documents ?? []);
        for (const state of session.windows ?? [])
            app.openWindow(state);
        if (app.initialFile.toString() !== "")
            app.openFiles([app.initialFile], null);
        if (app.windows.length === 0)
            app.openWindow(null);
    }
}
