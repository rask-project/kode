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
| **`Rask.Controls`** | `SidebarLayout`, `SidebarItem`, `FormGroup`, `FormRow` (done; icons come from each app); `TabStrip`, `TabButton`, `ToolButton`, `SearchBar`, `Menu`, `MenuItem` (later) | konsoll (generalized) |
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
- **Next:** `SystemColors` in `Rask.Platform` (Stage 2), then the rest as the
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

> Known limitation: Qt Quick's `TextArea` gets slow on large files (several
> MB). Accepted for the MVP and addressed in Stage 7.

---

### Stage 2 — Finish the Mac look

Done in Stage 1: `rask` as a submodule (`external/rask`), the libraries
embedded and signed in the bundle, the window with its content under the
title bar and the blur, the native "Save changes?" sheet and About panels.
What is left:

- **`SystemColors`** in `Rask.Platform` (semantic `NSColor`s, from notes'
  `UIKit`): text, selection and caret colors (today Qt's `SystemPalette`).
- Window settings: blur on/off and opacity (later in the Settings window).
- The installed bundle (`cmake --install`, `qt_generate_deploy_qml_app_script`)
  runs without the build tree: Qt, the `rask` dylibs.
- Migrate **Metronom**'s macOS build to `Rask.Window` + `Rask.Platform`
  (proves the API serves two apps).

**Done when:** Tekst's colors come from the system, the installed bundle runs on a machine without the build tree, and
Metronom's macOS build runs on the shared `rask` libraries (its Android and
Linux builds unchanged).

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
- Settings window (⌘,) with `Rask.Controls`' `SidebarLayout`, `FormGroup`
  and `FormRow` (as in `rask/examples/settings`); `QSettings`.

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

Minimap, multiple cursors, split view, Markdown with preview, LSP
(completion/diagnostics), compare mode (diff), Quick Look and Spotlight
integration, AppleScript/Shortcuts support.

## 5. Open decisions

- Git submodule vs. `FetchContent` with a tag.
- Whether `Rask.Style` becomes a standalone style selectable via
  `QQuickStyle::setStyle("Rask.Style")` or stays an extension of the `macOS`
  style.
