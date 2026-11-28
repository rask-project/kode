# Kode — Roadmap

A native macOS text editor in **Qt 6 / QML**, with native code
(Objective-C++/AppKit) only where Qt does not deliver the experience of a Mac
app.

This document sets **the order of implementation**. Each stage ends with an
app that builds, launches and is usable; no stage depends on a later one.

---

## 0. Baseline decisions

| Topic | Decision | Reason |
|---|---|---|
| Toolchain | C++23, Qt 6.12, CMake ≥ 3.21 + Ninja | Same as Konsoll |
| Platform | macOS 14.4+, arm64 | Same as Konsoll/Metronom |
| Bundle id | `com.github.rask-project.kode` (`.debug` in Debug) | Same pattern as Konsoll |
| App QML module | URI `Kode` | |
| Shared library | repository **`rask-project/rask`** (`../rask`), modules `Rask.*`, built as **shared (dynamic) libraries** | See section 2 |
| License | MIT (Rask Project) | Same as the other repositories |

---

## 1. What already exists and can be reused

Survey of the sibling repositories (`../konsoll`, `../metronom`, `../notes`):

| Feature | Where it is today | Notes |
|---|---|---|
| Translucent window + blur + content under the title bar | `konsoll/src/MacOSTranslucency.mm`, `WindowTranslucency` · `metronom/Metronom/MacWindowEffects.mm`, `NativeWindow` · `notes/Rask/Controls/Native/MacOSWindow` | **Three implementations of the same feature** → first thing to unify |
| Traffic-light buttons inset | `metronom NativeWindow.leadingInset` | Needed for the tab strip |
| Title-bar double click (zoom/minimize per system setting), movable window | `konsoll WindowTranslucency` | |
| Liquid Glass (macOS 26, `NSGlassEffectView`) | `konsoll/src/GlassEffect.*`, `controls/GlassSurface.qml` | |
| System colors (semantic `NSColor`s) | `notes/.../UIKit.*` | Useful for selection, caret and text background colors |
| User-chosen light/dark | `metronom/Metronom/Appearance.*` | |
| Native dialogs (`NSAlert`, text prompt) | `konsoll/src/NativeDialogs.mm` | Currently has terminal-specific methods → generalize |
| Native About panel | `konsoll/src/AboutDialog.mm` | |
| Sparkle updates | `konsoll/src/Updater.mm` (complete) · `metronom/Metronom/MacUpdater.mm` (simple) · `cmake/Sparkle.cmake` + `tools/update-appcast.py` in both | |
| Window state/geometry, screen, restoration | `konsoll/src/WindowState*`, `WindowScreen` | |
| Quit guard (`NSApplicationDelegate`) and Dock menu | `konsoll/src/AppQuitGuard.mm`, `DockMenu.mm` | Kode needs the quit guard for unsaved documents |
| Tab strip / tab button | `konsoll/controls/TerminalTabStrip.qml`, `TerminalTabButton.qml` | Coupled to the terminal → extract a generic version |
| SearchBar, Menu, ToolButton, FormGroup/FormRow, SettingsSidebarItem | `konsoll/controls/` | |
| Qt Quick Controls style for macOS | `metronom/MacStyle/` | Fills the gaps in Qt's `macOS` style |
| Version from git tag, `CFBundleVersion`, ad hoc signing, `PruneBundle`, icons | `CMakeLists.txt` and `cmake/` of both apps | Copied between projects → becomes a CMake module |

---

## 2. Shared library: `rask`

Repository `rask-project/rask` (local checkout `../rask`; its `AGENTS.md`
holds the library's rules), consumed by the apps as a **git submodule**
(`external/rask`) with `add_subdirectory`. Alternative:
`FetchContent` pinned to a tag (later, once the API stabilizes).

### Rules

- Each module is a **shared (dynamic) QML library**:
  `qt_add_library(rask_window SHARED)` + `qt_add_qml_module(... URI Rask.Window)`,
  producing a backing library (`libRaskWindow.dylib` / `.so`) and its QML
  plugin. Apps link the alias target `Rask::Window`, etc. Why shared: the
  apps load one copy of each module per process (one `NSApplication`
  delegate, one Sparkle updater, one set of QML type registrations), a fix in
  `rask` reaches every app without relinking its code, and the library
  has a real public API boundary instead of being a pile of sources compiled
  into each app.
- **Public API and symbols:** default visibility hidden
  (`CXX_VISIBILITY_PRESET hidden`, `VISIBILITY_INLINES_HIDDEN ON`); exported
  classes and functions carry an export macro generated per module
  (`generate_export_header` → `RASK_WINDOW_EXPORT`). Public headers sit next to the sources in
  `src/Rask/<Module>/`, are included as `<Rask/<Module>/Header.h>` and are
  installed to `include/Rask/<Module>/`; private ones (`*_p.h`, the `.mm`
  bridges) are never installed.
- **Versioning:** every library has `VERSION`/`SOVERSION` from the
  `rask` project version (`libRaskWindow.0.dylib`); the QML module
  version follows the same major. Breaking the C++ or QML API bumps the major.
- **QML layout:** `QT_QML_OUTPUT_DIRECTORY` is `${CMAKE_BINARY_DIR}/qml`, so
  every module ends up in `qml/Rask/<Module>/` (qmldir, plugin, `.qmltypes`)
  and the engine finds them in the build tree with one import path.
- **Bundling (macOS):** `rask_macos_bundle()` in `RaskApp.cmake` copies the
  backing libraries into `Contents/Frameworks`, the QML modules into
  `Contents/Resources/qml`, sets the rpath (`@executable_path/../Frameworks`)
  and signs each dylib before the bundle itself (ad hoc in Debug, Developer
  ID for releases; notarization rejects unsigned nested code). In the
  install step `qt_generate_deploy_qml_app_script` deploys them with Qt's own
  modules. `PruneBundle` must not strip them.
- **Consumption:** git submodule + `add_subdirectory` (builds from source with
  the app's Qt kit — no ABI mismatch). The project also has `install(EXPORT)`
  with a `RaskConfig.cmake`, so `find_package(Rask)` works against an
  installed copy later.
- C++ namespace `rask`. Types registered with `QML_ELEMENT` / `QML_SINGLETON`.
- **macOS only**: no stubs or fallbacks for other systems; `rask` refuses to
  configure elsewhere. An app that also ships elsewhere (Metronom: Android,
  Linux) links `rask` on macOS only and keeps its own code for the others.
  Features of newer macOS versions report it (`GlassEffect.supported`).
- Minimum Qt **6.10** (Metronom requires 6.10; Konsoll and Kode use 6.12).
- Nothing app-specific: no "workspace", "session", "agent".
- Each module has an example in `examples/` and tests (`qmltestrunner` / QtTest).

### Modules

| Module (URI) | Contents | Source |
|---|---|---|
| **`Rask.Platform`** | `Appearance` (light/dark), `SystemColors` (semantic `NSColor`s), `AppInfo` (versions, `nativeMenuBar`) | metronom, notes |
| **`Rask.Window`** | `NativeWindow` (translucency, blur, content under the title bar, `leadingInset`, movable, title-bar double click, `representedFile`, `documentEdited`), `GlassEffect` + `GlassSurface.qml` (native Liquid Glass and its QML fallback), `WindowState` (geometry/screen/restoration) | konsoll + metronom + notes, unified |
| **`Rask.Dialogs`** | `AboutDialog` (system About panel), `AboutQtDialog`, `Alert` (`NSAlert`, sheet when given a window — e.g. Save / Don't Save / Cancel); later a text prompt for Konsoll | konsoll |
| **`Rask.Application`** | `QuitGuard` (the app asks before quitting), configurable `DockMenu`, `RecentDocuments` (`NSDocumentController` → native "Open Recent" menu), `FileOpenHandler` (`QFileOpenEvent`, Finder's "Open With") | konsoll + new |
| **`Rask.Updater`** | `Updater` singleton (Sparkle, beta channel, "Check for Updates…") + `RaskSparkle.cmake` + `rask_sparkle_plist_keys()`; later `update-appcast.py` | konsoll |
| **`Rask.Controls`** | `SidebarLayout`, `SidebarItem`, `FormGroup`, `FormRow` (done; icons come from each app), `SearchBar`, `TabStrip` (done); `ToolButton`, `Menu`, `MenuItem` (later) | konsoll (generalized) |
| **`Rask.Style`** | Qt Quick Controls style for macOS (extends `QtQuick.Controls.macOS`) | metronom `MacStyle` |
| `cmake/RaskApp.cmake` | `rask_version_from_git()`, `rask_bundle_version()`, `rask_macos_bundle()` (Info.plist, embedding and signing the `Rask.*` libraries, prune, icons) | konsoll + metronom |

Modules are created **on demand**, in the Kode stage that needs them (see
section 3), not all at once.

### Status

- **Done** (`rask` 0.1.0): the shared-library plumbing (`rask_add_qml_module()`,
  export macros, hidden symbols, `SOVERSION`, install/export,
  `rask_embed_libraries()`), **`Rask.Platform`** (`Appearance`) and
  **`Rask.Window`** (`NativeWindow`, unifying Konsoll's `WindowTranslucency`
  and Metronom's `NativeWindow`; `GlassEffect`/`GlassSurface` from Konsoll's
  tab strip), **`Rask.Dialogs`** (`AboutDialog`,
  `AboutQtDialog`, `Alert`) and **`Rask.Updater`** (`Updater`, Sparkle
  embedded by `rask_embed_libraries()`) and **`Rask.Controls`** (the
  System Settings controls: `SidebarLayout`, `SidebarItem`, `FormGroup`,
  `FormRow`), with example apps, tests and CI.
- `SystemColors` in `Rask.Platform` is done (Stage 2); `Rask.Application`,
  `TabStrip` and `NativeWindow`'s document properties (Stages 7–8, `rask`
  0.2.0); the rest come as the
  stages below need them.

---

## 3. Kode stages

### Stage 0 — Foundation

- `git init`, `.gitignore`, `.clang-format`, `.clang-tidy` (copied from
  Konsoll), `AGENTS.md`, `README.md`, `LICENSE`.
- `CMakeLists.txt`: target `kode`, QML module `Kode`, `MACOSX_BUNDLE`,
  `Info.plist.in`, version from the git tag, ad hoc signing after the build.
- `main.cpp`: `QGuiApplication`, organization/domain for `QSettings`,
  `QQmlApplicationEngine` loading `Kode.Main`.
- Icon: `assets/kode.icon` (Icon Composer, **Natt** palette), generated by
  `tools/make-app-icons.py`; compiled with `actool` like Konsoll's
  `cmake/AppIcons.cmake` (Assets.car + fallback .icns).
- GitHub Actions workflow building on macOS.

**Done when:** `cmake --build` produces `Kode.app` and it opens an empty
window.

---

### Stage 1 — Simple text editor (MVP) — done

One window, one document. Built directly on `rask` (the parts of Stage 2 it
already covered): `NativeWindow` with the title bar drawn by QML,
`Appearance`, the "Save changes?" sheet (`Alert`), About Kode / About Qt.
The editor is a `TextEdit` in a `Flickable` rather than a `TextArea` (the
macOS style draws `TextArea` as a bordered field). Checked by hand: open from
the command line, edit, ⌘S, ⌘W and ⌘Q with unsaved changes (Save, Don't
Save, Cancel), the save panel for an untitled document.

**C++**
- `Document` (`QML_ELEMENT`): `fileUrl`, `fileName`, `modified`, `text`
  bound to the `TextArea`'s `QQuickTextDocument`; `load(url)`, `save()`,
  `saveAs(url)`. UTF-8, preserves the original line ending (LF/CRLF) and the
  final newline, atomic write with `QSaveFile`, `error(message)` signal.

**QML**
- `Main.qml`: `ApplicationWindow` with `ScrollView` + `TextArea`, the
  system's monospaced font (`QFontDatabase::FixedFont`), line wrapping not
  yet configurable (default: on).
- `MenuBar` (becomes the native menu bar on macOS): **File** (New, Open…,
  Save, Save As…, Close), **Edit** (Undo, Redo, Cut, Copy, Paste, Select All).
- Standard shortcuts (`StandardKey`): ⌘N, ⌘O, ⌘S, ⇧⌘S, ⌘W, ⌘Z, ⇧⌘Z, ⌘X/C/V, ⌘A.
- `FileDialog` (`QtQuick.Dialogs`, which already uses the native panel on
  macOS).
- Title: `name.txt — Kode`, "Untitled" for a new document.
- Closing with unsaved changes: `MessageDialog` Save / Don't Save / Cancel.
- Open a file passed on the command line (`kode file.txt`).

**Tests**
- QtTest for `Document`: UTF-8 round trip, CRLF preserved, missing file,
  `modified` after editing/saving.

**Done when:** you can create, open, edit, undo, save and close a file
without losing data, with no native code.

> Known limitation: Qt Quick's `TextEdit` gets slow on large files (several
> MB) and cannot grow into a code editor. Accepted for the MVP; `EditorView`
> (Stages 3–4) replaces it.

---

### Stage 2 — Finish the Mac look

Done in Stage 1: `rask` as a submodule (`external/rask`), the libraries
embedded and signed in the bundle, the window with its content under the
title bar and the blur, the native "Save changes?" sheet and About panels.
What is left:

- Done: **`SystemColors`** in `Rask.Platform` (semantic `NSColor`s): the
  editor's text, selection (unemphasized in an inactive window), caret and
  line-number colors, following light/dark and the accent live.
- Done: window settings — blur on/off and opacity, in the Settings window.
- Done: the installed bundle (`package-macos`: `qt_generate_deploy_qml_app_script`,
  then `cmake/PruneBundle.cmake`) runs without the build tree: Qt, the
  `rask` dylibs.

**Done when:** Kode's colors come from the system and the installed bundle
runs on a machine without the build tree.

---

### Stage 3 — `EditorView` prototype — done

Result (`docs/editor-bench.md`): `EditorView`'s keystroke and scroll cost is
flat from 1 to 100 MB; `TextEdit` takes 20 s to open 100 MB and freezes up to
1.5 s per key. Opening is 15–235× faster, memory 25–40× lower. Stage 4 goes
ahead.

Why: a code editor at the level of Qt Creator (highlighting, gutter, folding,
multiple cursors, minimap, large files) needs control over the text model,
the layout and the rendering, which `TextEdit` does not give. Qt Creator's
editing core is not QWidgets: `QTextDocument`, `QTextLayout` and
KSyntaxHighlighting are QtGui-only; only its `QPlainTextEdit` shell is a
widget. Kode keeps QML for the whole UI and moves the editing surface to a
C++ `QQuickItem` drawing into the scene graph (Konsoll's `TerminalRenderer`
pattern). Highlighting comes after it, since it is designed for it.

A short prototype first, to confirm the approach before investing:

- `TextBuffer`: piece table (original + added buffers, a newline index per
  buffer, consecutive typing coalesced into one piece). Line start, line
  text and position ↔ line in O(pieces + log n). QtTest against a plain
  `QString` reference, randomized.
- `EditorView` (`QQuickItem`): lays out only the visible lines
  (`QTextLayout`, monospaced, no wrapping yet) in `updatePolish()`, draws
  each with a `QSGTextNode` (`NativeRendering`) plus a caret node; typing,
  Backspace/Delete, arrows, Home/End and page keys, click to place the caret,
  committed input-method text. Scrolls inside a `Flickable`.
- `kode-editor-bench`: opens 1, 10 and 100 MB files with `TextEdit` and
  with `EditorView` in a real window and reports open-to-first-frame,
  keystroke-to-frame, scroll frame time and memory.


---

### Stage 4 — `EditorView` replaces `TextEdit`

**4a — done.** Kode edits with `EditorView` (`TextEditor.qml`: a Flickable
around it, keeping the caret in view): selection (Shift + keys, mouse drag,
double/triple click), clipboard, undo/redo grouped by typing bursts (a new
line closes a group), ⌥ word and ⌘ line navigation and deletion, gutter
with line numbers, current-line band, horizontal scrolling, preedit shown
inline and `inputMethodQuery` for the candidate window and press-and-hold
accents. `Document` loads and saves through the `TextBuffer`.
`tst_editorview` covers the editing model.

**4b:**
- Done: the scene graph synced incrementally (`docs/editor-bench.md`), the
  selection drawn as rectangles (line ends and empty lines included), and
  accessibility: `EditorAccessible` exposes the text, caret, selection and
  character/word/line boundaries and accepts edits; macOS sees an
  `AXTextArea` with its character count, selection and insertion line.
- Done: drag and drop of text — the selection moves within the view (⌥
  copies) or leaves for another app; text dropped from elsewhere is
  inserted; the caret shows where it would go.
- Done: light/dark colors from `SystemColors` (Stage 2).

Soft wrap moves to Stage 6 ("toggle line wrapping"): code is not wrapped by
default, and wrapping needs a visual-line index of its own.

**Done when:** Kode edits with `EditorView` and nothing regresses from
Stage 1 (round trips, IME, unsaved-changes flows).

---

### Stage 5 — Syntax highlighting

Started: KSyntaxHighlighting 6.32 built from submodules (with ECM) at
configure time and embedded; `SyntaxHighlighter` keeps each line's end
state, highlights in 3 ms slices ahead of the view and after an edit only
until a line ends as before; rows carry their start state; detection by
file name and shebang; Breeze light/dark following `Appearance`
(`docs/editor-bench.md`). Then: a status bar (line and column, the
syntax with a native menu of every definition by section), the bracket
pairing with the one at the caret and the word at the caret with its
occurrences in view marked behind the text (brackets in comments and
strings aside, as far as highlighting has gone). Large files: Stage 10.

- **KSyntaxHighlighting** (KDE Frameworks, MIT, 300+ languages, themes;
  QtCore/QtGui only) through `AbstractHighlighter`, line by line: each line
  keeps the state it ends in; an edit re-highlights from its line until the
  state matches what was stored, so a keystroke costs a line or two. Runs
  ahead of the visible lines in slices; far ahead of it, the view guesses.
- Formats applied to the visible lines' `QTextLayout`s.
- Language detection by extension/shebang, selector in the status bar.
- Done: highlighting themes — chosen in View > Theme or Settings among the
  ones that come as a light and a dark pair ("X Light"/"X Dark": Kode, Atom
  One, ayu, Breeze, GitHub, gruvbox, Solarized); the app's appearance picks
  the light or dark one (Kode by default); Qt Creator's Light/Dark (2024) colors
  converted by `tools/make-syntax-themes.py`. Plain text keeps the system
  color; KSyntaxHighlighting is syntactic, so the colors Qt Creator gets
  from clangd/QmlJS (fields, parameters, locals, QML ids) need a language
  server (Stage 11).
- Brackets matching and current-word highlight.

---

### Stage 6 — Editor features

Done: find and replace — `SearchBar` (added to `Rask.Controls`) under the
title bar, ⌘F, ⌘R, ⌘G/⇧⌘G, ⌘E; `TextSearch` searches line by line in
3 ms slices (matches within a line, at most 100 000 stored), the matches
in view marked in `findHighlightColor`; Replace All is one undo; with a
regular expression, $0…$9 in the replacement. Go to line (⌘L): "line" or
"line:column", centered in the view. Font zoom (View > Bigger/Smaller/Actual Size,
⌘+ / ⌘- / ⌘0), saved in the settings. Indentation: tabs or spaces and
the width, detected when a file opens and shown (and changed) in the
status bar; Return keeps the line's indentation, Tab goes to the next
stop or indents the selected lines, ⇧Tab and ⌘[ / ⌘] shift them.
Encodings: detected when a file opens (byte order mark, else UTF-8 if
valid, else Windows-1252), the byte order mark kept; Reopen with / Save
with Encoding from the status bar (20 encodings: Qt's Unicode and
Latin-1, the rest through CoreFoundation); saving refuses characters the
encoding lacks. The status bar also shows the selection's length and the
line ending (LF/CRLF, changeable). View > Wrap Lines: `WrapIndex` keeps
each line's visual rows in chunks (estimated from the length, the font
being monospaced, and corrected when a line is laid out), so scrolling,
the caret and edits never walk every line; estimating 100 MB again
(open, resize) takes 17 ms. View > Show Invisibles: dots and dashes for
spaces and tabs, one geometry node (Qt's ShowTabsAndSpaces only draws
through QPainter). Settings window (⌘,): Appearance (light/dark/system,
background blur and opacity) and Editor (font family and size, theme,
wrapping, invisibles, the indentation for new documents), bound to the
`Preferences` singleton (QSettings) that every window shares.

- Find/replace (`SearchBar` from `Rask.Controls`): ⌘F, ⌘G/⇧⌘G, ⌘R, regex,
  case sensitivity, matches highlighted by the `EditorView` (search runs on
  the text buffer, off the GUI thread for large files).
- Go to line (⌘L).
- Toggle line wrapping; show invisible characters.
- Indentation: tabs/spaces, width, auto-indent, ⌘[ / ⌘] to indent the
  selection.
- Status bar: line:column, selection, encoding, line ending, indentation,
  language.
- Font zoom (⌘+ / ⌘- / ⌘0).
- Encodings beyond UTF-8 (detection + "Reopen with Encoding").
- Settings window (⌘,) with `Rask.Controls`' `SidebarLayout`, `FormGroup`
  and `FormRow` (as in `rask/examples/settings`); `QSettings`.

---

### Stage 7 — macOS document integration — done

Done, with Qt Creator's model instead of tabs: the open documents are
the app's (`OpenDocuments`), and every window is a tree of splits, each
showing one of them, picked from its bar (`EditorPane`); two splits can
show the same document (`TextDocument` shared by the views, each with its
caret and scroll). `NativeWindow` got `representedFile` and
`documentEdited` (the proxy icon stays hidden with the title AppKit hides
for the QML title bar); `Rask.Application` has `QuitGuard`,
`FileOpenHandler` and `RecentDocuments` (Open Recent in File, the system's
list for the Dock); `Session` reopens the documents and each window's
geometry and splits. ⌘N new document, ⇧⌘N new window, ⌘W close document, ⇧⌘W close all documents (⌥⌘W closes the window),
⌘D / ⇧⌘D split side by side / top and bottom, ⌥⌘D remove split.

The window then lost its title (no sidebar yet: the project view comes
later): the splits reach the top, the bars of those there filling the
title bar. Each bar has Back and Forward (⌃⌘← / ⌃⌘→) through the
documents that split showed (`DocumentHistory`) and a wider documents
pop-up; Ctrl+Tab switches among the open documents, that split's most recent first, as
in Qt Creator (`DocumentSwitcher`). Closing the last document leaves the
window on a start page with how to open one (`StartPage`), not closed.
⌃D selects the next occurrence of the selection with a caret of its own,
as in VS Code and Qt Creator: typing then changes them all at once.
Comments by the language's markers (KSyntaxHighlighting's): ⌘/ toggles
line comments on the selected lines, ⌥⌘/ a block comment around the
selection (line ones for languages without, HTML's lines wrapped).
⌘U selects the block around the selection, a larger one each time, by
the folding blocks (⇧⌘U steps back), as Qt Creator's Select Block Up/Down.
⌥↑/↓ add a caret on the line above/below (⌥⇧↑/↓ keep the selection's
columns, a block selection), and ⇧←/→ extend every caret's selection.
Code folding: by KSyntaxHighlighting's regions or by indentation, from
the gutter's markers or View > Code Folding (⌥⌘← / ⌥⌘→, as in Xcode).
Indent guides, the caret's block in the accent color, as Neovim's
indent-blankline (View > Show Indent Guides, on by default). Auto-indent
by brackets without knowing the language (comments and strings told apart
by the highlighting): Return, closing brackets, Edit > Re-Indent (⌃I) and
re-indenting on save (Settings, off by default). Every icon is an SF Symbol
(`icon.name`). `Rask.Controls` got `ToolbarGroup`/`ToolbarButton` (a glass
toolbar capsule) along the way, unused here for now.

- Multiple windows, one per document (⌘N opens a new window); window
  controller in `Main.qml`.
- `NativeWindow.representedFile` → proxy icon in the title bar and path menu
  on ⌘-click; `documentEdited` → dot in the close button.
- **`Rask.Application`**:
  - `FileOpenHandler`: Finder's "Open With" and dropping onto the Dock icon
    (`QFileOpenEvent`); document types (`public.plain-text`,
    `public.source-code`, …) in `Info.plist`.
  - `RecentDocuments`: native "Open Recent" menu.
  - `QuitGuard`: ⌘Q reviews unsaved documents across all windows.
- Dropping a file into the window opens it.
- Detect external file changes (`QFileSystemWatcher`) → reload automatically
  if there are no edits, ask if there are.
- Restore open windows/documents on relaunch (`WindowState`).

---

### Stage 8 — Sidebar: folders — done

Kode has no tabs: Stage 7's splits and documents menu (Qt Creator's model)
take their place (`TabStrip` stays in `Rask.Controls`).

A sidebar that hides, as in Xcode: the button after the window buttons
(and View > Show Sidebar, ⌃⌘S: ⌘0 is Actual Size here). Each window may
have a folder (File > Open Folder…, ⇧⌘O; the command line, the Finder,
the Dock, Open Recent, a folder dropped on the window), shown in the
sidebar as VS Code's explorer (`FileTree` over `QFileSystemModel`, which
reads folders in its own thread and follows changes on disk): folders
first, names as the Finder sorts them, a click selects, a double click
opens a folder in place (as its arrow does) or a file in the active split,
the file in view marked and revealed (its folders opened, scrolled to)
while Synchronize with Editor is on (the link button beside the folder's
name, as in Qt Creator, with a filter menu). As in Neovim's file trees, the
root goes up a folder (the arrow button) or moves to a folder in the tree
(its right-click menu). Collapse All closes every folder (View, ⌥⌘-, or
the right-click menus). The right-click menu does what a file tree needs:
New File and New Folder (named in place), Rename (in place), Duplicate,
Move to Trash, Cut/Copy/Paste (file URLs on the clipboard, so the Finder
pastes them too), Copy Path, Reveal in Finder; open documents follow a
rename or move. Settings > Files > Sidebar size (small, medium, large)
scales its text and icons. Opening a file alone
leaves the sidebar hidden. The system's icon for each entry (`FileIcons`,
`image://fileicon/…`). Hidden files shown or not (View > Show Hidden
Files, ⇧⌘., as in the Finder; Settings > Files). The window's folder,
the sidebar and its width are saved with the session.

---

### Stage 9 — Polish and distribution — done

- Done: release via GitHub Actions (`.github/workflows/release.yml`: a `v*`
  tag publishes the ad-hoc-signed `.dmg`/`.zip`; `docs/RELEASE.md`), 1.0.0;
  the website (`site/`); the `kode` command, "Open in Kode" and Make
  Default (`Rask.Application`).
- Done: **`Rask.Updater`** (linked, `rask_sparkle_plist_keys()` in
  `Info.plist`, `Updater.start()`, Settings ▸ Updates) + the appcast in the
  release workflow.
- Decided: no Developer ID signing or notarization; releases stay signed ad
  hoc (`docs/RELEASE.md`).
- Dropped: Liquid Glass on the bars (tried and reverted) and adopting
  `Rask.Style`: no gain for the cost.

---

### Stage 10 — Large files off the GUI thread — done

Editing stays on the GUI thread: Qt Quick items, input and accessibility
live there, a keystroke lays out one line, and documents not in view cost
nothing. What moved is the work that grows with the file, measured first
(`kode-work-bench`, `docs/editor-bench.md`):

- Done: opening — from 2 MB on, reading, decoding (`TextCodec`) and the
  `TextBuffer`'s line index on a worker; the view shows "Opening…" and
  then the text (100 MB: 0.1 ms blocking instead of ~230 ms). UTF-8 is
  decoded as it is read, 4 MB at a time: opening 100 MB peaks at the
  memory the file then takes (495 MB in all, from 782 MB).
- Done: highlighting a jump far down a large file — the lines in view
  from a guessed state (no state, 200 lines above them) until the exact
  highlighting, still in 3 ms slices, gets there (100 MB: colors in 7 ms
  instead of after 22 s). Not on a worker: the states form a chain, so a
  thread would not be sooner, and KSyntaxHighlighting's definitions are not
  thread-safe (they cache states while highlighting).
- Done: Find and Replace All stay in 3 ms slices on the GUI thread, which
  already keep up.
- Done: re-indenting on save — only the lines edited since the last save
  (as VS Code's "format modifications only"), each moved line's
  indentation replaced in one undo step: the cost follows the edits, not
  the file (it re-indented every line before: 2.7 s at 10 MB, a minute at
  100 MB, all blocking).
- Dropped: saving large files on a worker; measured, no gain for the cost.

---

### Stage 11 — Minimap and LSP

Done: the minimap (`Minimap`, beside the view; View > Show Minimap), its
cost measured (`docs/editor-bench.md`). LSP started: the client
(`src/lsp/`: framing, a server's process and handshake, a server per
program and root found on the login shell's PATH, edits sent as ranges)
and diagnostics — a wave under each, the message in a help tag, errors
and warnings counted in the status bar (a click lists them); what the
server is doing (starting, analyzing the file, indexing) in the sidebar's
footer, once per window; clangd given the compilation database of a build
folder. Go to Definition (F2 — Rename while the sidebar has the focus —,
⌘click; several: a menu) and Switch Header/Source (F4, as Qt Creator's:
beside it, else clangd's). Settings > Language Servers: on/off, folders
searched before the shell's PATH, clangd's threads (1–2), what was found
for each language (Detect Again; a missing one's install command copied
by a long press), custom servers (any command for any syntax, before the
built-in ones). Built in: clangd, Python, Rust, Go, JS/TS, QML, Swift,
Bash, Lua, Java, Kotlin, YAML, JSON, HTML, CSS, Markdown, CMake,
Dockerfile, Terraform, TOML. Completion: asked when a word starts, after
a trigger character or on ⌃Space, filtered as one types, a list under the
caret (↑↓, Return/Tab, Escape), the server's other edits (an #include) in
the same undo step. Semantic highlighting: the server's tokens (clangd's,
qmlls's) in Qt Creator's categories and colors (its schemes and defaults,
in the themes' "semantic-styles"), over KSyntaxHighlighting's, asked a
moment after typing, only what changed. Quick fixes (⌥Return, as Qt
Creator's Alt+Return): the server's fixes and refactorings at the caret,
made in one undo step. QML as Qt Creator shows it: qmlls given the build
folder (the project's own types found), its tokens refined by their text.

- **Minimap** on top of `EditorView`: the document at a reduced scale
  beside the text, the part in view marked, click and drag to scroll. Bound
  by what is visible like the rest of the view: nothing per frame scales
  with the file size.
- **LSP** (Language Server Protocol): a client talking to the language
  server of each language over stdio (JSON-RPC), the documents' changes
  sent incrementally; completion, diagnostics in the text and the gutter,
  Go to Definition (F2, ⌘click), Switch Header/Source (F4), a Settings
  section to find and configure the servers, and semantic highlighting (the colors Qt Creator gets from
  clangd/QmlJS: fields, parameters, locals) over KSyntaxHighlighting's.

---

## 4. Future items (outside the initial scope)

- **Plugin support**: a way to extend Kode from outside its core. Markdown
  with preview is built as a plugin, not in the core.
- **Remote folders over SSH**: documents, the sidebar, Find File and the
  language servers on another machine; the plan is `docs/REMOTE.md`.
- Compare mode (diff), Quick Look and Spotlight integration,
  AppleScript/Shortcuts support.

Not planned: git support (status, blame).

## 5. Open decisions

- Git submodule vs. `FetchContent` with a tag.
