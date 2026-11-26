# Tekst — Roadmap

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
| Bundle id | `com.github.rask-project.tekst` (`.debug` in Debug) | Same pattern as Konsoll |
| App QML module | URI `Tekst` | |
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
| Quit guard (`NSApplicationDelegate`) and Dock menu | `konsoll/src/AppQuitGuard.mm`, `DockMenu.mm` | Tekst needs the quit guard for unsaved documents |
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
- Minimum Qt **6.10** (Metronom requires 6.10; Konsoll and Tekst use 6.12).
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

Modules are created **on demand**, in the Tekst stage that needs them (see
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

### Migrating the existing apps

1. **Metronom** migrates first (`Rask.Window`, `Rask.Platform`, `Rask.Style`,
   `Rask.Updater`) on its macOS build: it is small and proves the API serves
   a second app.
2. **Konsoll** migrates afterwards, module by module, without blocking Tekst.
3. **notes** replaces `Rask.Controls.Native` with the new modules.

---

## 3. Tekst stages

### Stage 0 — Foundation

- `git init`, `.gitignore`, `.clang-format`, `.clang-tidy` (copied from
  Konsoll), `AGENTS.md`, `README.md`, `LICENSE`.
- `CMakeLists.txt`: target `tekst`, QML module `Tekst`, `MACOSX_BUNDLE`,
  `Info.plist.in`, version from the git tag, ad hoc signing after the build.
- `main.cpp`: `QGuiApplication`, organization/domain for `QSettings`,
  `QQmlApplicationEngine` loading `Tekst.Main`.
- Icon: `assets/tekst.icon` (Icon Composer, **Natt** palette), generated by
  `tools/make-app-icons.py`; compiled with `actool` like Konsoll's
  `cmake/AppIcons.cmake` (Assets.car + fallback .icns).
- GitHub Actions workflow building on macOS.

**Done when:** `cmake --build` produces `Tekst.app` and it opens an empty
window.

---

### Stage 1 — Simple text editor (MVP) — done

One window, one document. Built directly on `rask` (the parts of Stage 2 it
already covered): `NativeWindow` with the title bar drawn by QML,
`Appearance`, the "Save changes?" sheet (`Alert`), About Tekst / About Qt.
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
- Title: `name.txt — Tekst`, "Untitled" for a new document.
- Closing with unsaved changes: `MessageDialog` Save / Don't Save / Cancel.
- Open a file passed on the command line (`tekst file.txt`).

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
- The installed bundle (`cmake --install`, `qt_generate_deploy_qml_app_script`)
  runs without the build tree: Qt, the `rask` dylibs.
- Migrate **Metronom**'s macOS build to `Rask.Window` + `Rask.Platform`
  (proves the API serves two apps).

**Done when:** Tekst's colors come from the system, the installed bundle runs on a machine without the build tree, and
Metronom's macOS build runs on the shared `rask` libraries (its Android and
Linux builds unchanged).

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
widget. Tekst keeps QML for the whole UI and moves the editing surface to a
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
- `tekst-editor-bench`: opens 1, 10 and 100 MB files with `TextEdit` and
  with `EditorView` in a real window and reports open-to-first-frame,
  keystroke-to-frame, scroll frame time and memory.


---

### Stage 4 — `EditorView` replaces `TextEdit`

**4a — done.** Tekst edits with `EditorView` (`TextEditor.qml`: a Flickable
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

**Done when:** Tekst edits with `EditorView` and nothing regresses from
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
occurrences in view marked behind the text. Left: off-thread
highlighting if large files need it, and brackets inside strings and
comments (today matched like any other).

- **KSyntaxHighlighting** (KDE Frameworks, MIT, 300+ languages, themes;
  QtCore/QtGui only) through `AbstractHighlighter`, line by line: each line
  keeps the state it ends in; an edit re-highlights from its line until the
  state matches what was stored, so a keystroke costs a line or two. Runs
  ahead of the visible lines, off the GUI thread for large files.
- Formats applied to the visible lines' `QTextLayout`s.
- Language detection by extension/shebang, selector in the status bar.
- Done: highlighting themes — chosen in View > Theme or Settings among the
  ones that come as a light and a dark pair ("X Light"/"X Dark": Tekst, Atom
  One, ayu, Breeze, GitHub, gruvbox, Solarized); the app's appearance picks
  the light or dark one (Tekst by default); Qt Creator's Light/Dark (2024) colors
  converted by `tools/make-syntax-themes.py`. Plain text keeps the system
  color; KSyntaxHighlighting is syntactic, so the colors Qt Creator gets
  from clangd/QmlJS (fields, parameters, locals, QML ids) need a language
  server (later).
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
geometry and splits. ⌘N new document, ⇧⌘N new window, ⌘W close document,
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
⌥↑/↓ add a caret on the line above/below (⌥⇧↑/↓ keep the selection's
columns, a block selection), and ⇧←/→ extend every caret's selection. Every icon is an SF Symbol
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

### Stage 8 — Tabs — replaced

Tekst has no tabs: Stage 7's splits and documents menu (Qt Creator's
model) take their place. `TabStrip` was added to `Rask.Controls` for it
and stays there for Konsoll.

- Migrate Konsoll to the shared `TabStrip` (can happen in parallel).

---

### Stage 9 — Polish and distribution

- **`Rask.Updater`** (link it, `rask_sparkle_plist_keys()` in `Info.plist`,
  `Updater.start()`) + appcast + release via GitHub Actions
  (Developer ID signing and notarization, as in `konsoll/docs/RELEASE.md`;
  every embedded `Rask.*` dylib and QML plugin signed with the hardened
  runtime).
- Liquid Glass (`Rask.Window`'s `GlassEffect`, `GlassSurface` as fallback) on
  the tab strip/status bar on macOS 26.
- **`Rask.Style`** extracted from Metronom and adopted by Tekst.
- Website (GitHub Pages) following the Konsoll/Metronom pattern.

---

## 4. Future items (outside the initial scope)

On top of `EditorView`: code folding, multiple cursors, minimap.
Also Markdown with preview, LSP (completion/diagnostics), compare mode
(diff), Quick Look and Spotlight integration, AppleScript/Shortcuts support.

## 5. Open decisions

- Git submodule vs. `FetchContent` with a tag.
- Whether `Rask.Style` becomes a standalone style selectable via
  `QQuickStyle::setStyle("Rask.Style")` or stays an extension of the `macOS`
  style.
