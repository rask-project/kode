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
| Shared library | new repository **`rask-project/rask-qml`**, modules `Rask.*` | See section 2 |
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

## 2. Shared library: `rask-qml`

New repository `rask-project/rask-qml`, consumed by the apps as a **git
submodule** (`external/rask-qml`) with `add_subdirectory`. Alternative:
`FetchContent` pinned to a tag (later, once the API stabilizes).

### Rules

- Each module is a **static QML library** (`qt_add_library(... STATIC)` +
  `qt_add_qml_module`), like `metronom_qml`. Apps link the target
  `Rask::Window`, etc.
- C++ namespace `rask`. Types registered with `QML_ELEMENT` / `QML_SINGLETON`.
- **Cross-platform by contract**: Metronom runs on Android and Linux. Every
  `.mm` has a `_stub.cpp` counterpart (no-op) selected in CMake, and each type
  exposes `supported`/`translucent` so QML can fall back.
- Minimum Qt **6.10** (Metronom requires 6.10; Konsoll and Tekst use 6.12).
- Nothing app-specific: no "workspace", "session", "agent".
- Each module has an example in `examples/` and tests (`qmltestrunner` / QtTest).

### Modules

| Module (URI) | Contents | Source |
|---|---|---|
| **`Rask.Platform`** | `Appearance` (light/dark), `SystemColors` (semantic `NSColor`s), `AppInfo` (versions, `nativeMenuBar`) | metronom, notes |
| **`Rask.Window`** | `NativeWindow` (translucency, blur, content under the title bar, `leadingInset`, movable, title-bar double click, `representedFile`, `documentEdited`), `GlassEffect`, `WindowState` (geometry/screen/restoration) | konsoll + metronom + notes, unified |
| **`Rask.Dialogs`** | Generic `NativeDialogs`: `alert`, `confirm`, `prompt(text)`, **`askSaveChanges()`** (Save / Don't Save / Cancel); `AboutPanel` | konsoll |
| **`Rask.Application`** | `QuitGuard` (the app asks before quitting), configurable `DockMenu`, `RecentDocuments` (`NSDocumentController` → native "Open Recent" menu), `FileOpenHandler` (`QFileOpenEvent`, Finder's "Open With") | konsoll + new |
| **`Rask.Updater`** | `Updater` (Sparkle, beta channel) + `RaskSparkle.cmake` + `update-appcast.py` | konsoll |
| **`Rask.Controls`** | `TabStrip`, `TabButton`, `GlassSurface`, `ToolButton`, `SearchBar`, `Menu`, `MenuItem`, `FormGroup`, `FormRow`, `SidebarItem` | konsoll (generalized) |
| **`Rask.Style`** | Qt Quick Controls style for macOS (extends `QtQuick.Controls.macOS`) | metronom `MacStyle` |
| `cmake/RaskApp.cmake` | `rask_version_from_git()`, `rask_bundle_version()`, `rask_macos_bundle()` (Info.plist, ad hoc signing, prune, icons) | konsoll + metronom |

Modules are created **on demand**, in the Tekst stage that needs them (see
section 3), not all at once.

### Migrating the existing apps

1. **Metronom** migrates first (`Rask.Window`, `Rask.Platform`, `Rask.Style`,
   `Rask.Updater`): it is small and validates Android/Linux.
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

### Stage 1 — Simple text editor (MVP)

One window, one document. Qt only, no native code yet.

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

> Known limitation: Qt Quick's `TextArea` gets slow on large files (several
> MB). Accepted for the MVP and addressed in Stage 7.

---

### Stage 2 — Create `rask-qml` and give it a Mac look

First stage that produces shared code.

- Create the `rask-qml` repository with `cmake/RaskApp.cmake`,
  **`Rask.Platform`** and **`Rask.Window`** (unifying Konsoll's
  `WindowTranslucency`, Metronom's `NativeWindow` and notes' `MacOSWindow`).
- Tekst: add it as a submodule; window with content under the title bar
  (`Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint`), translucent
  blurred background (optional), text/selection/caret colors from
  `SystemColors`, follows light/dark.
- **`Rask.Dialogs`**: replace the `MessageDialog` with the native `NSAlert`
  "Save changes?" (as a sheet attached to the window).
- Migrate **Metronom** to `Rask.Window` + `Rask.Platform` (proves the API
  serves two apps and works outside macOS).

**Done when:** Tekst has translucency/blur and native dialogs, and Metronom
builds on all three platforms using `rask-qml`.

---

### Stage 3 — macOS document integration

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

### Stage 4 — Tabs

- **`Rask.Controls`**: extract generic `TabStrip`/`TabButton` from Konsoll
  (tab model, drag to reorder, close button, modified indicator, respecting
  the traffic lights' `leadingInset`).
- Tekst: several documents per window; ⌘T new tab, ⌘⇧[ / ⌘⇧] to navigate,
  ⌘1…9; move a tab to a new window / between windows.
- Migrate Konsoll to the shared `TabStrip` (can happen in parallel).

---

### Stage 5 — Editor features

- Find/replace (`SearchBar` from `Rask.Controls`): ⌘F, ⌘G/⇧⌘G, ⌥⌘F, regex,
  case sensitivity, highlight matches.
- Go to line (⌘L).
- Gutter with line numbers and current-line highlight.
- Toggle line wrapping; show invisible characters.
- Indentation: tabs/spaces, width, auto-indent, ⌘[ / ⌘] to indent the
  selection.
- Status bar: line:column, selection, encoding, line ending, indentation,
  language.
- Font zoom (⌘+ / ⌘- / ⌘0).
- Encodings beyond UTF-8 (detection + "Reopen with Encoding").
- Settings window (⌘,) with `FormGroup`/`FormRow`; `QSettings`.

---

### Stage 6 — Syntax highlighting

- **KSyntaxHighlighting** (KDE Frameworks, MIT, 300+ languages, themes)
  attached to the `QQuickTextDocument` via `SyntaxHighlighter`.
- Language detection by extension/shebang, selector in the status bar.
- Light/dark themes following `Appearance`.
- Measure the performance impact before moving on to Stage 7.

---

### Stage 7 — Performance and large files

Entry criterion: measure opening, typing and scrolling with 1 MB, 10 MB and
100 MB files. If `TextArea` falls short:

- Own text model (piece table or rope) in C++, with its own undo/redo.
- `EditorView` (`QQuickItem`) rendering only the visible lines — the same
  principle as Konsoll's `TerminalRenderer` — with caret, selection, IME
  (`inputMethodQuery`) and accessibility.
- Incremental highlighting reusing KSyntaxHighlighting's states.

This is the largest stage, which is why it comes after the app is already
useful.

---

### Stage 8 — Polish and distribution

- **`Rask.Updater`** (Sparkle) + appcast + release via GitHub Actions
  (Developer ID signing and notarization, as in `konsoll/docs/RELEASE.md`).
- About panel (`Rask.Dialogs.AboutPanel`), app icon.
- Liquid Glass (`GlassEffect`) on the tab strip/status bar on macOS 26.
- **`Rask.Style`** extracted from Metronom and adopted by Tekst.
- Website (GitHub Pages) following the Konsoll/Metronom pattern.

---

## 4. Future items (outside the initial scope)

Minimap, multiple cursors, split view, Markdown with preview, LSP
(completion/diagnostics), compare mode (diff), Quick Look and Spotlight
integration, AppleScript/Shortcuts support.

## 5. Open decisions

- Shared repository name: `rask-qml` (proposed) vs. `rask-kit` / `rask`.
- Git submodule vs. `FetchContent` with a tag.
- Whether `Rask.Style` becomes a standalone style selectable via
  `QQuickStyle::setStyle("Rask.Style")` or stays an extension of the `macOS`
  style.
