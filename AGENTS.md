# AGENTS.md

Guidance for AI agents working in this repository.

## Project

- Tekst ("text" in Norwegian) — a native macOS text and code editor built in
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

- `CMakeLists.txt` — app target `tekst` (bundle `Tekst.app`), QML module
  `Tekst`, version from the git tag, the `rask` libraries embedded
  (`rask_embed_libraries()`) and ad hoc signing after the build.
- `external/extra-cmake-modules`, `external/syntax-highlighting` — KDE's
  ECM and KSyntaxHighlighting (submodules, pinned to a KF6 tag), built once
  at configure time into `build/…/deps` by `cmake/KSyntaxHighlighting.cmake`
  and embedded as `libKF6SyntaxHighlighting.6.dylib`; bump both tags
  together.
- `external/rask` — the shared `Rask.*` modules, a git submodule
  (`git submodule update --init`). Tekst uses `Rask.Window` (`NativeWindow`),
  `Rask.Platform` (`Appearance`, `SystemColors`), `Rask.Controls`
  (`SearchBar`) and `Rask.Dialogs` (`Alert`, `AboutDialog`,
  `AboutQtDialog`). Changes to those go to the `rask` repository first, then
  the submodule is bumped here.
- `main.cpp` — `QGuiApplication` + `QQmlApplicationEngine`; passes the file
  from the command line (`initialFile`) and the system's monospaced font
  (`editorFont`) to `Tekst.Main`.
- `Main.qml` — the editor window: title bar drawn under the transparent
  AppKit one, a `TextEdit` in a `Flickable` (not `TextArea`: the macOS style
  draws it as a bordered field), the menu bar, file dialogs and the
  "Save changes?" sheet.
- `src/Document.{h,cpp}` — `Document` (QML type): loads a file into the
  `EditorView` and saves it back through `QSaveFile`, keeping its encoding,
  byte order mark and line ending (changing them marks it modified);
  `reopen()` reads it again in another encoding.
- `src/TextCodec.{h,cpp}` — detecting, decoding and encoding a file's
  bytes: Qt for UTF-8/16 and Latin-1 (no ICU in this Qt), CoreFoundation
  (`CFStringCreateWithBytes`/`CFStringGetBytes`) for the rest.
- `GoToLine.qml` — the ⌘L popup ("line" or "line:column", parsed by
  `EditorView::goToLine()`).
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
  - `EditorView` (`QQuickItem`) — sits in a `Flickable` (`contentX/Y`,
    `contentWidth/Height`); all text changes go through `replace()`, which
    records undo. `updatePolish()` (GUI thread) lays out the lines in view
    (`QTextLayout`), reusing rows by their *text* (an edit above a line moves
    it without laying it out again) and line numbers by line, and computes
    the selection rectangles and the caret, relative to the first line laid
    out. `updatePaintNode()` only syncs: two transforms carry the scroll
    (text area and gutter); each row is a `QSGTextNode` (`NativeRendering`)
    keyed by the row's id, filled once and then only moved by its matrix;
    the selection is `QSGRectangleNode`s behind the text. Scrolling within
    the lines laid out does not polish at all. Nothing per frame scales with
    the file size; a keystroke lays out one line.
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
    start state; `syntax`/`detectSyntax()`/`darkTheme` drive it.
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
    identity without wrapping); each `Row` carries its `top`.
  - Invisibles are drawn by the view (one `QSGGeometryNode`): Qt's
    `QTextOption::ShowTabsAndSpaces` only affects `QPainter` drawing, not
    `glyphRuns()`.
  - `Indentation` — free functions: the style (tabs/spaces, width),
    what Tab inserts, block indent/outdent and detecting the style from
    the first lines; `EditorView` applies them (`indent()`, `outdent()`,
    auto-indent on Return) as single edits.
  - `Matching` — free functions for the marks drawn behind the text: the
    bracket pairing with the one at the caret (bounded line scan), the
    word at the caret and its whole-word occurrences in the rows in view.
  - Keep scene coordinates small: positions are relative to the first line
    laid out, never `line × lineHeight` (float vertices lose precision past
    a few million pixels).
- `tools/editor-bench/` — `tekst-editor-bench` (TextEdit vs. EditorView on
  one file, in a real window) and `run.sh` (1/10/100 MB, Markdown table for
  `docs/editor-bench.md`). Benchmark Release builds only.
- `tests/` — QtTest (`tst_document`, `tst_textbuffer` with randomized edits
  against a `QString`, `tst_editorview`, `tst_syntaxhighlighter` with
  randomized edits against highlighting from scratch, `tst_matching`, `tst_textsearch`, `tst_indentation`, `tst_wrapindex`), run offscreen with
  `ctest`; the sources under test are compiled into each test.
- `cmake/` — `Info.plist.in`, `AppIcons.cmake` (compiles `assets/tekst.icon`
  with actool; falls back to `assets/tekst.icns`).
- `assets/` — app icon: `tekst.icon` (Icon Composer document), `tekst.svg`,
  `tekst.png`, `tekst.icns`. All generated by `tools/make-app-icons.py`
  (palette "Natt"); never edit them by hand — change the script and rerun it.
  `assets/themes/` — the highlighting themes Tekst Light and Tekst Dark
  (KSyntaxHighlighting JSON, in the app's resources under `:/tekst/syntax`),
  generated by `tools/make-syntax-themes.py` from Qt Creator's Light/Dark
  (2024) color schemes with Qt Creator's own defaults and style mapping;
  never edit them by hand either.
- `tools/` — `format-code.sh`, `make-app-icons.py`, `render-icon.swift`,
  `make-syntax-themes.py`.
- `docs/` — `ROADMAP.md`.

## Build and run

Build to validate changes (the macOS Debug build below); also run it when the
user asks.

```sh
git submodule update --init                    # once, and after pulling a bump
cmake -B build/Qt_6_12_0_for_macOS_Debug -G Ninja \
  -DCMAKE_PREFIX_PATH=/opt/Qt/6.12.0/macos -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/Qt_6_12_0_for_macOS_Debug
ctest --test-dir build/Qt_6_12_0_for_macOS_Debug --output-on-failure
open build/Qt_6_12_0_for_macOS_Debug/Tekst.app
```

`tools/format-code.sh` and clang-tidy cover Tekst's own files; `external/rask`
follows its own `AGENTS.md`. Lint QML with the build's target, which lints
every QML module (Tekst's, the benchmark's, rask's) with their import paths
and resources: `cmake --build build/Qt_6_12_0_for_macOS_Debug --target
all_qmllint` — keep it free of warnings. Running `qmllint` on a file by hand
misses the `Tekst` module's context (its C++ types show as "not found").

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
- C++ types exposed to QML live in `namespace tekst` and register with
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
