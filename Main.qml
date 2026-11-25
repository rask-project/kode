pragma ComponentBehavior: Bound

import QtQuick

import Rask.Application
import Rask.Dialogs

import Tekst

// The app: its windows (EditorWindow), opening files in them, moving tabs
// and panes between them, the session reopened at launch and the review of
// unsaved documents before quitting. One Settings window for all of them.
Item {
    id: app

    // A file given on the command line, opened at start.
    required property url initialFile

    property list<EditorWindow> windows: []
    property EditorWindow lastActiveWindow: null

    function openWindow(state: var): EditorWindow {
        const window = windowComponent.createObject(app) as EditorWindow;
        app.windows.push(window);
        if (state) {
            window.tabs.restore(state.tabs);
            if (state.width > 0) {
                window.x = state.x;
                window.y = state.y;
                window.width = state.width;
                window.height = state.height;
            }
        }
        if (window.tabs.tabs.length === 0)
            window.newTab();
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

    // The pane showing `file` in any window, or null.
    function paneWithFile(file: url): EditorPane {
        for (const window of app.windows) {
            const pane = window.allPanes().find(candidate => candidate.fileUrl.toString() === file.toString());
            if (pane)
                return pane;
        }
        return null;
    }

    // Each file in a tab of `window` (the last active one, or a new one),
    // in place of an untouched new document; a file already open is shown.
    function openFiles(files: list<url>, window: EditorWindow) {
        for (const file of files) {
            const open = app.paneWithFile(file);
            if (open) {
                app.showPane(open);
                continue;
            }
            const target = window ?? app.lastActiveWindow ?? app.openWindow(null);
            const pane = target.activePane && target.activePane.untouched ? target.activePane : target.newTab();
            if (pane)
                pane.open(file);
            target.raise();
            target.requestActivate();
        }
    }

    function windowOf(pane: EditorPane): EditorWindow {
        return app.windows.find(window => window.tabs.tabOf(pane) >= 0) ?? null;
    }

    function showPane(pane: EditorPane) {
        const window = app.windowOf(pane);
        if (!window)
            return;
        window.tabs.activatePane(pane);
        window.raise();
        window.requestActivate();
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
        Session.save(app.windows.map(window => window.state()));
    }

    // Moves the tab at `index` of `source` to `target` (a new window when
    // null) at `targetIndex` (the end when negative); its documents stay as
    // they are. A source left without tabs closes.
    function moveTab(source: EditorWindow, index: int, target: EditorWindow, targetIndex: int): EditorWindow {
        const newWindow = target === null;
        if (newWindow) {
            target = windowComponent.createObject(app) as EditorWindow;
            app.windows.push(target);
            target.width = source.width;
            target.height = source.height;
        }
        const position = targetIndex < 0 ? target.tabs.tabs.length : targetIndex;
        if (!source.tabs.moveTabTo(index, target.tabs, position)) {
            if (newWindow)
                target.close();
            return null;
        }
        target.show();
        target.raise();
        target.requestActivate();
        return target;
    }

    // The window under a global point; `preferred` wins where windows overlap.
    function windowAtGlobal(globalX: real, globalY: real, preferred: EditorWindow): EditorWindow {
        const contains = window => window.visible && window.visibility !== Window.Minimized && globalX >= window.x && globalX < window.x + window.width && globalY >= window.y && globalY < window.y + window.height;
        if (preferred && contains(preferred))
            return preferred;
        return app.windows.find(contains) ?? null;
    }

    // A tab dragged out of `source` and released at a global point joins the
    // window under it, or tears off into a new window placed there.
    function dropTab(source: EditorWindow, index: int, globalX: real, globalY: real) {
        const target = app.windows.find(window => window !== source && window === app.windowAtGlobal(globalX, globalY, null)) ?? null;
        if (target) {
            app.moveTab(source, index, target, target.tabInsertionIndexAtGlobal(globalX, globalY));
            return;
        }
        // A lone tab torn off would just be its window again.
        if (source.tabs.tabs.length < 2)
            return;
        const window = app.moveTab(source, index, null, 0);
        if (window && window.visibility === Window.Windowed) {
            window.x = globalX - 100;
            window.y = globalY - 18;
        }
    }

    // Where a pane dragged out of `source` to a global point lands.
    function paneDropAt(pane: EditorPane, source: EditorWindow, globalX: real, globalY: real): var {
        const target = app.windowAtGlobal(globalX, globalY, source);
        let drop = target ? target.paneDropTarget(globalX, globalY, pane) : null;
        // A window's only pane already is its own tab there.
        if (drop && drop.kind === "tab" && target === source && source.tabs.paneCount < 2)
            drop = null;
        return {
            window: target,
            drop: drop
        };
    }

    function hoverPane(pane: EditorPane, source: EditorWindow, globalX: real, globalY: real) {
        const at = app.paneDropAt(pane, source, globalX, globalY);
        app.windows.forEach(window => window.showPaneDrop(window === at.window ? at.drop : null));
    }

    function clearPaneDrops() {
        app.windows.forEach(window => window.showPaneDrop(null));
    }

    // A pane released at a global point: beside the pane under it (in this
    // or another window), into a tab strip as a tab of its own, or torn off
    // into a new window placed there.
    function dropPane(pane: EditorPane, source: EditorWindow, globalX: real, globalY: real) {
        app.clearPaneDrops();
        const at = app.paneDropAt(pane, source, globalX, globalY);
        if (at.window) {
            if (!at.drop)
                return;
            const moved = at.drop.kind === "pane" ? source.tabs.movePane(pane, at.window.tabs, at.drop.pane, at.drop.edge) : source.tabs.movePaneToTab(pane, at.window.tabs, at.drop.index);
            if (moved && at.window !== source) {
                at.window.raise();
                at.window.requestActivate();
            }
            return;
        }
        if (source.tabs.paneCount < 2)
            return;
        const window = windowComponent.createObject(app) as EditorWindow;
        app.windows.push(window);
        window.width = source.width;
        window.height = source.height;
        window.x = globalX - 100;
        window.y = globalY - 18;
        if (!source.tabs.movePaneToTab(pane, window.tabs, 0)) {
            window.close();
            return;
        }
        window.show();
        window.raise();
        window.requestActivate();
    }

    // Reviews each window's unsaved documents in turn, then quits.
    function reviewWindows(index: int) {
        if (index >= app.windows.length) {
            app.saveSession();
            QuitGuard.quit();
            return;
        }
        const window = app.windows[index];
        window.review(window.allPanes(), () => app.reviewWindows(index + 1));
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

    Connections {
        target: FileOpenHandler

        function onFilesOpened(files: list<url>) {
            app.openFiles(files, null);
        }
    }

    Connections {
        target: QuitGuard

        function onQuitRequested() {
            app.reviewWindows(0);
        }
    }

    Component.onCompleted: {
        for (const state of Session.windows())
            app.openWindow(state);
        if (app.initialFile.toString() !== "")
            app.openFiles([app.initialFile], null);
        if (app.windows.length === 0)
            app.openWindow(null);
    }
}
