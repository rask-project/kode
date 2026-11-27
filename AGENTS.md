# AGENTS.md

Guidance for AI agents working in this repository.

## Project

- Kode ("code" in Norwegian) — a native macOS text and code editor built in
  QML (Qt Quick), with native AppKit code where Qt falls short.
- Toolchain: C++23, Qt 6.12, CMake ≥ 3.21 + Ninja.
- Target platform: macOS 14.4+ (Apple Silicon).
- Part of the rask-project family with Konsoll (`../konsoll`) and Metronom
  (`../metronom`): same toolchain, rules and look. Native features those apps
  already have (translucent/blurred window, Liquid Glass, tab strip, native
  dialogs, Sparkle) are reused, not rewritten — they are being moved to the
  shared `rask` library (`../rask`, QML modules `Rask.*`; its `AGENTS.md`
  has the library's rules).
- **`docs/ROADMAP.md` is the plan**: the stages in order and what each one
  delivers. Work on the current stage; don't pull features from later ones.

## Structure

- `CMakeLists.txt` — app target `kode` (bundle `Kode.app`), QML module
  `Kode`, version from the git tag, the `rask` libraries embedded
  (`rask_embed_libraries()`) and ad hoc signing after the build.
- `external/extra-cmake-modules`, `external/syntax-highlighting` — KDE's
  ECM and KSyntaxHighlighting (submodules, pinned to a KF6 tag), built once
  at configure time into `build/…/deps` by `cmake/KSyntaxHighlighting.cmake`
  and embedded as `libKF6SyntaxHighlighting.6.dylib`; bump both tags
  together.
- `external/rask` — the shared `Rask.*` modules, a git submodule
  (`git submodule update --init`). Kode uses `Rask.Window` (`NativeWindow`),
  `Rask.Platform` (`Appearance`, `SystemColors`), `Rask.Controls`
  (`SearchBar`, `HelpTag` for tooltips, the Settings controls), `Rask.Dialogs`
  (`Alert`, `AboutDialog`, `AboutQtDialog`) and `Rask.Application`
  (`QuitGuard`, `FileOpenHandler` with the "Open in Kode" Service,
  `RecentDocuments`, `DefaultApplication`, `CommandLineTool`) and
  `Rask.Updater` (Sparkle: "Check for Updates…" in the app menu, started
  by `Main.qml`; the feed and key in `Info.plist` from
  `KODE_UPDATE_FEED_URL`/`KODE_UPDATE_PUBLIC_KEY`, off in Debug). Changes to those go to the `rask` repository first, then
  the submodule is bumped here.
- `main.cpp` — `QGuiApplication` + `QQmlApplicationEngine`; passes the file
  from the command line (`initialFile`) to `Kode.Main`.
- `Main.qml` — the app root (no window of its own): the window list over
  the documents they share (`OpenDocuments`), opening files and folders (a folder goes to a window's sidebar: from
  outside the app the window already on it, else the last active one
  without a folder, else a new one; files in the active
  split; from the Finder or the Dock a file already in view is shown
  there; each split moves off a closing document itself, to the start
  page when none is left: the windows stay), document errors and changes on disk (alerts
  on the last active window), each folder's session (`Session`: the
  documents a folder's window had open — its splits' and those in the
  folder — with its geometry and splits, saved when the window closes, its
  folder changes and on quit; reopened when the folder opens, in a new
  window replacing an unused one; a folder never opened starts on the start
  page; launched without a folder, one window without one, nothing
  restored), the quit review (`QuitGuard`) and the
  app-wide Settings and About windows.
- `EditorWindow.qml` — a window without a title: the `Sidebar` with the
  window's `folder` (hidden without one; the sidebar button after the
  window buttons, ⌃⌘S), its splits reach the top
  (the bars of the splits there fill the title bar, the top-left one after
  the window buttons), Ctrl+Tab for the active split's switcher, its splits (`Splits` + `PaneLayout`), the menus
  (actions go to the active split), the editor's right-click menu (the
  same `Action`s, so their shortcuts show; `Popup.Native`), the file dialogs and the "Save
  changes?" sheet (`review()` asks about each modified document in turn).
  Closing a window keeps the documents open unless it is the last one.
  `showDocument()` (not `show()`: that is the window's) puts a document in a
  split and closes the untouched new document it replaces.
- `EditorPane.qml` — one split: a bar as in Qt Creator with Back and
  Forward, the documents pop-up (every open document, by name;
  `Popup.Native`), close document and the split buttons (`topInset`/
  `leadingInset` when it sits in the title bar; the window stays put while
  the pointer is on a button), then `SearchBar`, `GoToLine`, `TextEditor` and
  `StatusBar`. Shows one `Document` (`document`); other splits may show the
  same one, each with its own caret and scroll. Its `DocumentHistory` feeds
  Back and Forward and the Ctrl+Tab switcher; when its document closes it goes back to the previous
  one. Icons are SF Symbols by name (`icon.name`, `IconImage`): Qt asks
  the system for them, nothing is drawn or bundled.
- `BarButton.qml` — the flat SF Symbol button of the bars in the title bar.
  `TitleBarButtons.qml` (singleton) counts those under the pointer; the
  window is dragged by its title bar only when none (`NativeWindow.movable`
  bound to it): set per button, leaving one could come after entering the
  next and leave the window draggable over a button, eating its clicks.
- `Sidebar.qml` — the window's sidebar, as Xcode's navigator: under the
  title bar, the folder as a `TreeView` over `FileTree`, as VS Code's
  explorer (a click selects; a double click opens a folder in place, as its
  arrow does, or a file in the active split),
  the system's icons, the file in view revealed while syncing with the
  editor (`FileTree::indexOf()`, `expandToIndex()`, again as entries
  arrive); beside the folder's name, as Qt Creator's, Enclosing Folder,
  the filter menu (hidden files) and Synchronize with Editor; a folder's
  right-click menu makes it the root, as Neovim's file trees (both change
  the window's folder); Collapse All (View, ⌥⌘-, and the right-click menus
  of a folder and of the folder's name); the right-click menu's file
  operations on the selection, else the folder (New File/Folder, named in
  place, Open ⌘↓, Rename in place F2, Duplicate, Move to Trash ⌘Delete or
  Delete, confirmed, Cut/Copy/Paste — the Edit menu's, acting on the tree
  while it has the focus —, Copy Path ⌥⌘C, Reveal in Finder ⌥⌘R; shortcuts
  only while the tree has the focus); text, icon
  and row sizes from `Preferences.sidebarSize`;
- `OverlayScrollBar.qml` — the editor's and the sidebar's scroll bar: no
  track, a handle that shows while scrolling.
- `src/FileTree.{h,cpp}` — `FileTree`: a `QSortFilterProxyModel` over a
  `QFileSystemModel` (folders read in its thread, changes on disk
  followed): the name column, folders first, names by `QCollator`
  (numeric), hidden files on request; `rootIndex` for the view and the
  roles `iconSource`, `isFolder`, `fileUrl`, `parentFolder`; the folder is
  kept without a trailing slash (the Finder sends one); `indexOf()` reads the folders
  down to a file (TreeView reads a folder only while it has no entries).
- `src/FileOperations.{h,cpp}` — `kode::files`: creating, renaming,
  copying (folders entry by entry, `QDirIterator`), moving and trashing
  files as the Finder does (a taken name gets " 2", a duplicate " copy"),
  each returning the new path or the error (`std::expected`). `FileTree`
  exposes them to QML with the clipboard (file URLs, so the Finder pastes
  them; a cut moves when pasted here) and emits `moved()`, which
  `OpenDocuments::fileMoved()` follows (`Document::followMove()`).
- `src/FileIcons.{h,cpp}` — `FileIcons`, the `image://fileicon/<path>`
  provider (registered in `main.cpp`): the system's icon for a file or
  folder (`QAbstractFileIconProvider`, the platform theme's, i.e. the
  Finder's); `CodeIcons`, `image://codeicon/<key>`: a code file's icon,
  the system's document icon with a badge of its language's label and
  color (the system has none by language; one per language, cached by
  URL).
- `src/FileKinds.{h,cpp}` — `fileKind()`: a code file's language by
  extension or whole name (CMakeLists.txt, Makefile…), a table of labels
  and colors (Linguist's).
- `PaneLayout.qml` — a window's split tree as nested `SplitView`s; each
  leaf is a slot its view is reparented into, so rebuilding keeps views.
  Each node knows whether it touches the window's top and left edges, for
  the insets of the bars in the title bar.
- `StartPage.qml` — what a split without a document shows, as in Qt
  Creator: "Open a document" and how (closing the last document leaves
  the windows open on it).
- `DocumentSwitcher.qml` — the Ctrl+Tab popup, as in Qt Creator: every open
  document, those the split showed first by recent use; Tab moves on while Control is held, releasing
  it shows the one selected, Escape cancels.
- `src/OpenDocuments.{h,cpp}` — `OpenDocuments` (QML singleton): every
  open `Document`, owned (`deleteLater`), most recently shown first, by
  name for the menu; one document per file; untitled ones numbered; a
  binary file (a NUL in its first 8000 bytes, as git tells, or a type the
  system knows as not text — `src/FileTypes.mm`, `UTType` — and not UTF-8)
  goes to the system's app for it (`QDesktopServices`). A
  `kode:///path` URL (from the `kode` command, for a file that does not
  exist yet: `open` refuses those) opens empty (`Document::startFile()`;
  saving writes it, making its folders).
- `src/DocumentHistory.{h,cpp}` — `DocumentHistory`: the documents a
  split showed, back and forward as in a browser, and by recent use
  (`recent`; `recentAmong()` orders the open documents for Ctrl+Tab); closed documents are forgotten.
- `src/Splits.{h,cpp}` — `Splits` (one per window) and `SplitNode`: the
  binary split tree; leaves own their view (`deleteLater`: a view may go
  from its own signal); split, remove a split (the last view stays),
  activate; `state()`/`restore()` save the tree with each view's file
  (post-order, no recursion). Views are `Kode.EditorPane`, made from the
  module in the engine's root context (an inline `Component` is bound to
  its file's context under `ComponentBehavior: Bound`).
- `src/Session.{h,cpp}` — `Session` (QML singleton): each folder's state,
  in QSettings `session/folders` (JSON by canonical folder URL:
  `OpenDocuments::cleanFolder()` resolves links, as the Finder does).
- `src/Document.{h,cpp}` — `Document` (QML type): an open file; owns its
  text (`textDocument`, a `TextDocument`), loads it and saves it back through `QSaveFile`, keeping its encoding,
  byte order mark and line ending (changing them marks it modified);
  `reopen()` reads it again in another encoding. A `QFileSystemWatcher` on
  the file and its folder (a file replaced by renaming comes back there)
  reloads a change made elsewhere when nothing is unsaved (keeping the
  caret), else emits `changedOnDisk()`; its own saves are told apart by the
  modification time and size.
- `src/TextCodec.{h,cpp}` — detecting, decoding and encoding a file's
  bytes: Qt for UTF-8/16 and Latin-1 (no ICU in this Qt), CoreFoundation
  (`CFStringCreateWithBytes`/`CFStringGetBytes`) for the rest.
- `GoToLine.qml` — the ⌘L popup ("line" or "line:column", parsed by
  `EditorView::goToLine()`).
- `SettingsWindow.qml` — the Settings window (⌘,): `Rask.Controls`'
  `SidebarLayout`, `FormGroup`, `FormRow`, bound to `Preferences`; section
  icons in `assets/settings/`. Files also has Make Default
  (`DefaultApplication`, the "Code" document type: macOS confirms each type another app opens, so it stays short) and Install for the
  `kode` command (`CommandLineTool`), read again when the window shows.
- `bin/kode` — the `kode` command, as VS Code's `code` (bundled in
  `Contents/Resources/bin`, linked into `/usr/local/bin` from Settings):
  `open -a` for existing paths (the running Kode, no second instance), a
  `kode:` URL for a missing file (encoded by `osascript -l JavaScript`).
- `src/Preferences.{h,cpp}` — `Preferences` (QML singleton): the user's
  preferences in QSettings (the only copy), shared by every window; the
  editor binds its font, theme, wrapping, invisibles and default
  indentation to it, and its background: transparent (the window's blur
  shows) or the window's color (`transparentEditor`; the bars stay
  translucent either way).
- `StatusBar.qml` — line and column, and the syntax with a native menu
  (`Popup.Native`) of `Syntaxes` (C++ singleton, `src/Syntaxes.*`: the
  definitions by section).
- `TextEditor.qml` — the editing area: a `Flickable` around the
  `EditorView`, scroll bars, colors from `SystemColors`. It scrolls to the
  caret on `caretMoved` (navigation, edits) and to `scrollRequested` after a
  relayout (width, font, wrapping), which keeps the top line in place —
  not on `cursorChanged`, which a relayout emits too.
- `src/editor/` — the editing surface:
  - `TextBuffer` — piece table: original + append-only added buffer, each
    with a newline index; pieces carry their newline count; consecutive
    typing grows one piece. Lines use '\n' only.
  - `TextDocument` — what views share: the buffer, undo history,
    highlighter and indentation style. Every change goes through
    `replace()`/`replaceEach()` (several carets: one undo step, typing
    extends it)/`undo()`/`redo()`/`setText()` and reaches each view with
    `replaced()`/`reset()`; a view moves its caret over another view's
    edit. It keeps the place (caret, scroll) the last view left, for the
    next one.
  - `EditorView` (`QQuickItem`) — a view of a `TextDocument` (`document`;
    its own when none): caret, selection, scroll, wrapping and layout.
    `selectBlock()` (⌘U, as Qt Creator's Select Block Up) grows the
    selection by the folding blocks (the lines inside, then the whole
    block, then the one around it, at last all the text);
    `unselectBlock()` (⇧⌘U) steps back while that selection stands.
    `selectNextOccurrence()` (⌃D, as in VS Code) adds carets
    (`m_others`) at the next occurrences of the selection (whole words when
    it started from a caret); ⌥↑/↓ (`addCaretVertically()`) add one on the
    row above/below at the same column (⌥⇧: with the selection's columns, a
    block); typing, deleting, pasting, ←/→ (with ⌥, ⌘, ⇧) and Home/End act at each,
    any other move or Escape leaves one. A right click (⌃click) moves
    the caret there unless it lands on the selection and emits
    `contextMenuRequested()`, queued (a native menu runs its own loop). It
    sits in a `Flickable` (`contentX/Y`, `contentWidth/Height`); its edits
    go through `replace()`, which records undo in the document. `updatePolish()` (GUI thread) lays out the lines in view
    (`QTextLayout`), reusing rows by their *text* (an edit above a line moves
    it without laying it out again) and line numbers by line, and computes
    the selection rectangles and the caret, relative to the first line laid
    out. `updatePaintNode()` only syncs: two transforms carry the scroll
    (text area and gutter); each row is a `QSGTextNode` (`NativeRendering`)
    keyed by the row's id, filled once and then only moved by its matrix;
    the selection is `QSGRectangleNode`s behind the text. Scrolling within
    the lines laid out does not polish at all. Nothing per frame scales with
    the file size; a keystroke lays out one line.
    The caret blinks at the system's rate (`cursorFlashTime()`) while the
    view has focus, shown solid again whenever it moves (a timer only
    repaints; nothing is laid out).
  - `EditorAccessible` — the view's `QAccessibleInterface` (text, editable
    text), installed as a factory by the first `EditorView` (after Qt
    Quick's, so it is asked first); a friend of `EditorView`. The view
    reports text, caret and selection changes to it while accessibility is
    active; `TextEditor.qml` gives it its `Accessible.role`/`name` so it
    joins the tree.
  - `SyntaxHighlighter` — a KSyntaxHighlighting `AbstractHighlighter` over
    the `TextBuffer`: each line's end state, computed from the top by
    `advance()` (bounded by a deadline: 4 ms in a layout, 3 ms slices
    from a zero timer ahead of the view); `linesReplaced()` after an edit
    keeps the old states aligned so highlighting stops at the first line
    that ends as before. `EditorView` rows are reused only with the same
    start state; `syntax`/`detectSyntax()`/`syntaxTheme` drive it. Only themes
    with a light and a dark version ("X Light"/"X Dark") are offered, by
    family (`themeFamilies()`); the appearance picks the version (`themeName()`).
  - `TextSearch` — find over the `TextBuffer`, line by line in slices
    (`advance()` with a deadline, from a zero timer in the view), matches
    sorted and capped at 100 000; the view binds `searchPattern`/
    `searchCaseSensitive`/`searchRegularExpression`, marks the matches in
    view and offers `findNext()`/`findPrevious()`/`replaceCurrent()`/
    `replaceAll()` (one edit over the matches' span: one undo).
  - `WrapIndex` — visual rows per line when lines wrap, in chunks of
    ~1024 lines with their sums (a line's first row and the line at a row
    without walking every line); estimated from `TextBuffer::lineLengths()`
    and set exactly for the lines laid out. `EditorView` goes through
    `rowOfLine()`/`lineAtRow()`/`positionRect()`/`positionAtRow()` (the
    identity without wrapping or folds); each `Row` carries its `top`.
  - `Folding` — `FoldIndex`, a view's folds (hidden line ranges, sorted
    and disjoint; few, so walked), which `rowOfLine()`/`lineAtRow()`/
    `totalRows()` map rows past, over the wrapping's; edits open the folds
    they touch and move the rest. Blocks come from KSyntaxHighlighting's
    folding regions (`SyntaxHighlighter::regionEnd()`/`regionAbove()`,
    highlighting from a known state without touching the stored ones; a
    row keeps how many regions its line opens, from its `formats()`), or
    from indentation where the definition folds that way (Python, YAML).
    The line that closes a region stays shown. `EditorView::fold()`/
    `unfold()`/`unfoldAll()` (View > Code Folding, ⌥⌘← / ⌥⌘→ as in
    Xcode), markers in the gutter (▾/▸, click to toggle) and a ⋯
    placeholder after a folded line (click to unfold). A caret never sits
    in hidden lines: moving there (go to line, find, undo) unfolds, ←/→
    go past them.
  - Indent guides (`showIndentGuides`, as Neovim's indent-blankline): a
    1 px line per indentation level of each row in view (a blank line takes
    its neighbours' lesser indentation), the one of the indented block at
    the caret (`caretBlock()`, a 2 ms bounded scan) in the caret's color;
    two geometry nodes, refilled only when they change.
  - Invisibles are drawn by the view (one `QSGGeometryNode`): Qt's
    `QTextOption::ShowTabsAndSpaces` only affects `QPainter` drawing, not
    `glyphRuns()`.
  - `Indentation` — free functions: the style (tabs/spaces, width),
    what Tab inserts, block indent/outdent and detecting the style from
    the first lines; `EditorView` applies them (`indent()`, `outdent()`,
    auto-indent on Return) as single edits. Indenting without knowing the
    language: `brackets()` (what a line's brackets do, those in comments
    and strings aside — `SyntaxHighlighter::nonCode()` by the formats'
    text styles) and `indentAfter()` (a level in after a bracket left
    open, out for closing ones; a ':' too where indentation is the
    syntax). Return indents so (between brackets, the closing one on a
    line of its own); a closing bracket typed on an empty line goes under
    the line of its opening one; `reindent()` (Edit > Re-Indent, ⌃I, as
    in Xcode) and `reindentAll()` (before saving, with
    `Preferences.reindentOnSave`: only what moves is replaced, the caret
    stays on its text) indent lines again, comment and string lines moving
    with the line before; not for indentation-based languages, nor when
    highlighting cannot be ready in 2 s.
  - `Commenting` — free functions: the selected lines commented out (or
    back in when all are) with the language's line marker at their least
    indentation (column 0 where the definition asks), each line between
    the block markers without one (HTML); a selection between the block
    markers or out of them. The markers come from the KSyntaxHighlighting
    definition; `EditorView::toggleLineComment()` (⌘/) and
    `toggleBlockComment()` (⌥⌘/, line comments without block ones) apply
    them as one edit (`changeSelectedLines()`, as indenting).
  - `Matching` — free functions for the marks drawn behind the text: the
    bracket pairing with the one at the caret (bounded line scan), the
    word at the caret and its whole-word occurrences in the rows in view.
  - Keep scene coordinates small: positions are relative to the first line
    laid out, never `line × lineHeight` (float vertices lose precision past
    a few million pixels).
- `tools/editor-bench/` — `kode-editor-bench` (TextEdit vs. EditorView on
  one file, in a real window) and `run.sh` (1/10/100 MB, Markdown table for
  `docs/editor-bench.md`). Benchmark Release builds only.
- `tests/` — QtTest (`tst_document`, `tst_textbuffer` with randomized edits
  against a `QString`, `tst_editorview`, `tst_syntaxhighlighter` with
  randomized edits against highlighting from scratch, `tst_matching`, `tst_textsearch`, `tst_indentation`, `tst_commenting`, `tst_wrapindex`, `tst_folding`,
  `tst_preferences`, `tst_filetree`, `tst_splits` with stand-in views, `OpenDocuments` and `DocumentHistory`), run offscreen with
  `ctest`; the sources under test are compiled into each test.
- `cmake/` — `Info.plist.in`, `FinderKeys.plist.in` (the document types
  — the "Code" (Make Default) and "Text" lists and imported types shared with Konsoll's Neovim, so both
  give an extension one type — and the "Open in Kode" Service; in the plist
  only with `KODE_FINDER_INTEGRATION`, off in Debug: Launch Services lists
  every copy it sees), `PruneBundle.cmake` (after Qt's deploy script in
  `package-macos`: removes what Kode never loads, signs again),
  `AppIcons.cmake` (compiles `assets/kode.icon`
  with actool; falls back to `assets/kode.icns`).
- `assets/` — app icon: `kode.icon` (Icon Composer document), `kode.svg`,
  `kode.png`, `kode.icns`. All generated by `tools/make-app-icons.py`
  (palette "Natt"); never edit them by hand — change the script and rerun it.
  `assets/themes/` — the highlighting themes Kode Light and Kode Dark
  (KSyntaxHighlighting JSON, in the app's resources under `:/kode/syntax`),
  generated by `tools/make-syntax-themes.py` from Qt Creator's Light/Dark
  (2024) color schemes with Qt Creator's own defaults and style mapping;
  never edit them by hand either.
- `tools/` — `format-code.sh`, `make-app-icons.py`, `render-icon.swift`,
  `make-syntax-themes.py`, `update-appcast.py` (from Konsoll's), `site-assets.sh`/`site-serve.sh` (the site's
  images and a local preview) and `screenshots/` (`docshot.sh`, which drives
  an isolated copy of the app over a demo project, `demo/aurora`, to take the
  README and site images; kept out of `format-code.sh`).
- `.github/workflows/` — `ci.yml` (formatting, Release build, tests),
  `release.yml` (a `v*` tag: tests, `package-macos`, the ad-hoc-signed
  `.dmg`/`.zip` on a GitHub Release, notes from `docs/releases/<tag>.md`,
  then the Sparkle appcast on the `appcast` branch, signed with the
  `SPARKLE_ED_PRIVATE_KEY` secret: `tools/update-appcast.py`)
  and `pages.yml` (the site).
- `docs/` — `ROADMAP.md`, `RELEASE.md` (releasing; the bundle; Developer
  ID), `releases/` (each release's notes), `editor-bench.md`, `SCREENSHOTS.md` (how each
  image in `docs/images/` is taken; retake them when the UI they show
  changes).
- `site/` — the website (Jekyll, from Konsoll's), published to GitHub Pages
  at <https://rask-project.github.io/kode/> by `.github/workflows/pages.yml`;
  its images come from `docs/images/` and `assets/` (`tools/site-assets.sh`),
  never copied into git. `site/README.md` has the layout.

## Build and run

Build to validate changes (the macOS Debug build below); also run it when the
user asks.

```sh
git submodule update --init                    # once, and after pulling a bump
cmake -B build/Qt_6_12_0_for_macOS_Debug -G Ninja \
  -DCMAKE_PREFIX_PATH=/opt/Qt/6.12.0/macos -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/Qt_6_12_0_for_macOS_Debug
ctest --test-dir build/Qt_6_12_0_for_macOS_Debug --output-on-failure
open build/Qt_6_12_0_for_macOS_Debug/Kode.app
```

`tools/format-code.sh` and clang-tidy cover Kode's own files; `external/rask`
follows its own `AGENTS.md`. Lint QML with the build's target, which lints
every QML module (Kode's, the benchmark's, rask's) with their import paths
and resources: `cmake --build build/Qt_6_12_0_for_macOS_Debug --target
all_qmllint` — keep it free of warnings. Running `qmllint` on a file by hand
misses the `Kode` module's context (its C++ types show as "not found").

Qt Creator opens the project directly with the matching kit.

## Conventions

- **Language:** respond in Portuguese. Code, comments and commit messages in
  English.
- **Comments:** minimal — names speak for themselves; at most one short line
  when the "why" is not obvious.
- **Work order:** get the code compiling/working first; review tests later.
- **Intentionally commented-out code** for testing parts in isolation is not
  a bug — confirm it's intentional before "fixing" it.
- **Hot paths (typing, layout, rendering, highlighting):** no allocation per
  keystroke or frame where avoidable, no locks, no logging.
- **Design:** minimal solutions, no speculative fields or options.
- No debug logging (`qDebug`) left in committed code.
- **Commits and PRs:** never add `Co-authored-by` (or any other AI
  attribution trailer, e.g. "Generated with") to commit messages or pull
  request descriptions. The author is the repository owner.

## C++ rules

- **C++23** is required (enforced in `CMakeLists.txt`).
- **Qt 6.12.**
- **Ownership:** every owned object has exactly one owner, a
  `std::unique_ptr` (or a plain value member). Ownership moves with
  `std::move`; code that only uses an object gets a non-owning `const T *`
  (or a reference) and never keeps it past the owner's lifetime. QObject
  children owned by their Qt parent are fine.
- **`std::shared_ptr` is forbidden** (and with it `std::make_shared`,
  `std::weak_ptr`, `QSharedPointer`). Shared ownership is never the answer:
  if the logic seems to need it, rethink it so one object owns the thing and
  the others borrow it (look it up by id when they need it, hand the owner
  the callback, keep the state in the owner).
- **Raw pointers are forbidden** — no `new`/`delete`, no owning raw pointers.
  Exceptions: the non-owning `const T *` above, and non-owning pointers
  required by C/POSIX/Qt/AppKit APIs. Wrap C handles in `std::unique_ptr`
  with a custom deleter. Qt Quick scene-graph nodes (`updatePaintNode`) are
  the one ownership hand-off: create them with `std::make_unique` and pass
  `release()` to the parent node, which owns them.
- **Exceptions are forbidden** — no `throw`, `try` or `catch` anywhere.
- **Error handling:** fallible operations return `std::expected<T, E>` with a
  concrete error type (or `bool`/`std::optional` when there is nothing to
  report).
- **Types:** use Qt's typedefs, never the `<cstdint>` ones: `qint8`…`qint64`
  / `quint8`…`quint64` for fixed width, `qsizetype` for sizes and indices
  (not `std::size_t`), `qreal` for geometry. Integers are `qint32` instead of
  `int`, `Q_PROPERTY` and `QList<qint32>` included (QML still sees `int`);
  only `main(int argc, …)` keeps `int`. Lists are `QList`, not `std::vector`.
  Platform API types (`CGFloat`, `NSInteger`, …) stay at the API boundary.
- **Initialization:** brace-initialize every variable — data members,
  constants, locals and `for` counters (`bool m_modified{false};`,
  `for (qsizetype i{0}; ...)`): braces reject narrowing conversions. `=` only
  where the grammar requires it (default arguments, enumerators). Exception:
  a container constructor that takes a size or other non-element arguments
  keeps parentheses (`QList<qint32> offsets(n);`), since braces would pick
  the `std::initializer_list` constructor.
- **Algorithms:** always use `std::ranges` / `<algorithm>` instead of
  hand-rolled loops.
- **Control flow:** early return over nesting; flatten `if`/`else` chains
  with guard clauses.
- **Performance:** `emplace_back` over `push_back`, move over copy, no
  needless temporaries; mark `noexcept` where it holds.
- **Member ordering / alignment:** order class data members from largest
  alignment requirement to smallest (16-byte types, then 8-byte — pointers,
  `std::unique_ptr`, `qint64`, `double` — then 4-byte, then 1-byte: `bool`,
  small enums) so the compiler inserts the least padding. Booleans/flags that
  fit in a byte should be packed, not given a full field each. Debug builds
  enable `-Wpadded` to flag any layout that still wastes padding — keep it
  clean.

## Editor core

- The editing surface is C++ (`EditorView`, a `QQuickItem` drawing into the
  scene graph), the rest of the UI QML. No QWidgets anywhere: Qt Creator's
  useful parts (`QTextLayout`, KSyntaxHighlighting) are QtGui-only.
- Work per keystroke and per frame is bounded by what is visible: lay out
  and draw only the lines in view, never walk or copy the whole text on the
  GUI thread. Text layout happens in `updatePolish()`, not in
  `updatePaintNode()` (render thread).
- Scene-graph nodes are owned by their parent node: create with
  `std::make_unique` / the window's `create*Node()`, hand over with
  `release()`, take back into a `unique_ptr` when removing.

## Qt / QML

- Follow Qt idioms: signals/slots, `Q_PROPERTY`, `QML_ELEMENT`, and Qt's
  model classes for list data.
- QML stays declarative. Business logic (documents, encodings, file I/O,
  search) belongs in C++ — a QML file that grows imperative logic is a bug in
  the layering, not a style preference.
- C++ types exposed to QML live in `namespace kode` and register with
  `QML_ELEMENT` / `QML_NAMED_ELEMENT` / `QML_SINGLETON`.
- New user-visible strings use `qsTr()`.
- Check <https://doc.qt.io> for component and API behaviour rather than
  guessing.
  - QML best practices: <https://doc.qt.io/qt-6/qtquick-bestpractices.html>
  - QML coding conventions: <https://doc.qt.io/qt-6/qml-codingconventions.html>

## Native macOS interop

- Native AppKit behaviour is written in **Objective-C++** (`src/*.mm`,
  compiled with ARC; `CMakeLists.txt` sets `LANGUAGES CXX OBJCXX`). Anything
  another rask-project app could use goes to `rask`, not here.
- The `.mm` file exposes plain C++ functions in a C++-only header; the QML
  type calls them.
- On macOS `QWindow::winId()` is the window's `contentView` (`NSView *`), not
  the `NSWindow` — get the window via `[nsView window]`.
- **Configurable native behaviour is exposed to QML as a C++ QML type**
  (`QML_ELEMENT`), and QML binds its properties. Don't reach into native code
  from QML directly; add a property to such a type instead.
- The app menu is a QML `MenuBar`. Qt moves items into the macOS app menu by
  matching their text ("About…", "Settings…", "Quit…"), so those texts stay
  in English; any other item of that menu must be added natively.
- AppKit colors expect **0.0–1.0** components: use `QColor::redF()`/
  `greenF()`/`blueF()`, not `red()`/`green()`/`blue()`.

## Formatting

- **Run `tools/format-code.sh` after every change**, before building or
  committing. It formats C++ and Objective-C++ (`.cpp`, `.h`, `.mm`) with
  clang-format (`.clang-format`: LLVM-based, 120 columns, 4 spaces, never
  tabs) and QML with qmlformat (`.qmlformat.ini`).
- `tools/format-code.sh --check` changes nothing and exits 1 listing the
  files that need formatting; `--changed` limits it to files changed against
  HEAD.
- QML follows qmlformat's defaults (the QML Coding Conventions, as Qt Creator
  formats it): 4-space indent, no line wrapping, attribute order kept. Don't
  hand-wrap long bindings — qmlformat joins them back.

## clang-tidy

- **Follow the clang-tidy rules proactively.** Before writing or changing
  C++, read `.clang-tidy` and write code that already conforms to its checks.
  The config is the source of truth — `WarningsAsErrors: "*"` makes every
  enabled check an error.
- Key enforced rules:
  - `readability-function-cognitive-complexity` — threshold 25; keep
    functions under it (extract helpers, table-drive, early-return).
  - `misc-no-recursion` — no recursion; rewrite iteratively with an explicit
    stack.
  - `cppcoreguidelines-avoid-do-while` — no `do { … } while (…)`.
  - `readability-implicit-bool-conversion`, `misc-const-correctness`,
    `cppcoreguidelines-init-variables`.
  - No `NOLINT` to suppress findings — refactor the code to fix the finding.
  - Flag/mask types: `inline constexpr unsigned int` constants in a
    namespace, unsigned literals with the `u` suffix.
- Verify per file (uses the build's `compile_commands.json`; with Homebrew's
  clang-tidy pass the SDK):
  `clang-tidy -p build/Qt_6_12_0_for_macOS_Debug --config-file=.clang-tidy --extra-arg=-isysroot$(xcrun --show-sdk-path) <file>`.
- **`.mm` caveat:** clang-tidy 19 segfaults on `.mm` files while running
  `cppcoreguidelines-avoid-const-or-ref-data-members` (AST-matcher bug, not a
  code issue). Run `.mm` files with that single check disabled
  (`--checks=-cppcoreguidelines-avoid-const-or-ref-data-members`); all other
  checks still apply.
