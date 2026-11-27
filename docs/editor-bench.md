# EditorView prototype — benchmark

Roadmap Stage 3: does a C++ `QQuickItem` editing surface (`EditorView`, on a
piece table, laying out and drawing only the visible lines) beat Qt Quick's
`TextEdit` enough to build the editor on it?

**Answer: yes.** `EditorView`'s cost per keystroke and per scroll stays the
same from 1 MB to 100 MB; `TextEdit`'s grows with the file until it drops
frames for seconds. Opening is 15–235× faster and memory 25–40× lower. Stage 4
(`EditorView` replaces `TextEdit`) goes ahead.

## Results

Release build, Apple M1, macOS 27.2.1, Qt 6.12.0, 2560 × 1600 Retina at
60 Hz, window 900 × 700 pt, Menlo 13 pt, no wrapping (both editors).
Taken on 2026-11-25 with `tools/editor-bench/run.sh`.

| Editor | Size (MB) | Open to first frame (ms) | Keystroke work avg (ms) | Keystroke work max (ms) | Keystroke to screen max (ms) | Scroll work avg (ms) | Scroll work max (ms) | Memory (MB) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| TextEdit | 1 | 483 | 11.25 | 12.38 | 16.85 | 19.02 | 34.49 | 119 |
| EditorView | 1 | 32 | 11.63 | 13.36 | 18.94 | 12.27 | 17.77 | 3 |
| TextEdit | 10 | 4 935 | 76.12 | 105.41 | 105.45 | 167.59 | 203.80 | 375 |
| EditorView | 10 | 33 | 11.60 | 13.16 | 19.22 | 13.24 | 17.50 | 9 |
| TextEdit | 100 | 19 550 | 548.51 | 1 194.08 | 1 517.82 | 528.82 | 971.31 | 1 241 |
| EditorView | 100 | 83 | 11.68 | 12.94 | 16.74 | 12.45 | 14.66 | 32 |

What the columns are:

- **Open to first frame**: from handing the decoded text to the editor to its
  first frame on screen (reading and decoding the file are outside the clock,
  the same for both).
- **Keystroke work**: one character typed in the middle of the text, until
  the frame showing it is rendered (`afterRendering`). This still includes
  waiting for the next render tick, which the threaded render loop aligns
  with the display, so ~11 ms is the floor for both editors here: compare how
  the numbers *grow with the file*, not their absolute value.
- **Keystroke to screen**: until that frame is on the display
  (`frameSwapped`); above 16.7 ms a frame was missed.
- **Scroll work**: one jump of the view to another part of the text, until
  the frame is rendered.
- **Memory**: resident size after the first frame minus before loading. macOS
  compresses memory, so this is noisy (TextEdit measured 646 MB for 10 MB in
  an earlier run); the order of magnitude is what counts.

## Reading the numbers

- `EditorView` is flat: typing and scrolling cost the same at 1, 10 and
  100 MB, because it lays out and draws only the ~50 lines in view and edits
  the piece table without copying text. Its memory is the text itself (UTF-16)
  plus the newline index.
- `TextEdit` lays out and keeps nodes for the whole document: at 10 MB every
  keystroke misses about six frames, at 100 MB it freezes for up to 1.5 s per
  key and takes 20 s to open.
- At 1 MB both keep up with the display; the difference there is opening
  (15×) and memory (40×).

## Scene graph update cost

After the prototype, `EditorView` stopped rebuilding its scene every frame
(Stage 4): each line is a `QSGTextNode` placed by its own matrix inside a
transform that carries the scroll (relative to the first line laid out, so
coordinates stay small in huge files); a line's node is found again by its
row (rows are reused by their text, wherever an edit moved them) and filled
only when it first shows; line numbers likewise; the selection is drawn as
rectangles behind the text. "Update" is from the end of animations to the
end of synchronization: polishing (layout) plus `updatePaintNode`.

| EditorView | Size (MB) | Keystroke update avg (ms) | Keystroke update max (ms) | Scroll jump update avg (ms) | Scroll jump update max (ms) |
|---|---:|---:|---:|---:|---:|
| Before | 1 | 1.13 | 1.98 | 1.63 | 2.97 |
| Before | 10 | 1.21 | 1.89 | 1.62 | 3.67 |
| Before | 100 | 1.30 | 1.80 | 1.64 | 3.71 |
| After | 1 | 0.32 | 0.71 | 1.13 | 3.60 |
| After | 10 | 0.30 | 0.79 | 1.02 | 3.22 |
| After | 100 | 0.36 | 0.75 | 1.11 | 2.90 |

A keystroke now lays out one line instead of every line in view (≈ 4×
less work). The scroll test jumps to unrelated parts of the file, where
every line in view is new; scrolling through lines already laid out only
changes a matrix.

## With syntax highlighting

Stage 5: the benchmark's EditorView highlights the text as C++
(KSyntaxHighlighting, the file is the project's own sources). States are
computed from the top in 3 ms slices between frames, so right after
opening, typing in the middle of a large file starts plain and turns
highlighted when the slices get there (at 10 MB after about one second;
at 100 MB not within the test).

| Size (MB) | Open to first frame (ms) | Keystroke update avg (ms) | Keystroke update max (ms) | Keystroke to screen max (ms) | Scroll jump update avg (ms) | Scroll jump update max (ms) | Memory (MB) |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 36.2 | 0.47 | 4.50 | 20.35 | 0.81 | 1.89 | 4 |
| 10 | 33.2 | 0.21 | 2.81 | 32.41 | 0.94 | 3.31 | 9 |
| 100 | 99.9 | 0.09 | 0.20 | 33.16 | 0.44 | 2.79 | 32 |

A highlighted keystroke costs about 0.5 ms (the line highlighted again
and laid out with its formats; the next lines only checked). The max
update is the frame where the view's lines first get their colors (all
laid out again). While slices run, a frame is missed now and then
(33 ms); with 8 ms slices it was most of the first keystrokes. Moving the
work off the GUI thread is the next step if this shows in use.

## Work that grows with the file

Stage 10: what blocks the GUI thread (or, sliced, keeps it busy) in
proportion to the file, measured with `kode-work-bench` on the same files
(`tools/editor-bench/run-work.sh`, offscreen, no window). Release build,
Apple M1 with 8 GB, macOS 27.2.1, Qt 6.12.0, on 2026-11-26.

| Size (MB) | Read (ms) | Decode (ms) | Set text (ms) | Highlight all (ms) | Search (ms) | Search, sliced (ms) | Replace All (ms) | Undo (ms) | Re-indent all (ms) | Save (ms) |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1.2 | 0.9 | 1.8 | 0.9 | 269.4 | 13.8 | 14.1 | 5.7 | 4.7 | 298.2 | 15.9 |
| 10.2 | 3.6 | 14.5 | 5.5 | 2 282.5 | 122.4 | 126.7 | 25.0 | 17.5 | 2 715.2 | 218.6 |
| 100.4 | 38.7 | 143.5 | 50.1 | 22 434.6 | 1 277.5 | 1 355.3 | 234.4 | 154.0 | 60 341.4 | 14 424.7 |

- **Opening** (read + decode + set text) blocks for 25 ms at 10 MB and
  about 230 ms at 100 MB; decoding is most of it.
- **Highlighting** every line takes 2.3 s at 10 MB and 22 s at 100 MB. It
  runs in 3 ms slices, so it never blocks, but it takes the GUI thread's
  spare time and a jump far down a large file shows plain text until the
  slices get there.
- **Find** is already sliced: done in about the time it takes in one go
  (1.4 s against 1.3 s at 100 MB), never blocking.
- **Replace All** and its undo block for 0.2 s and 0.15 s at 100 MB.
- **Re-indent on save** (`Preferences.reindentOnSave`, off by default) is
  the worst: 0.3 s at 1 MB, 2.7 s at 10 MB and a minute at 100 MB, all
  blocking, with highlighting ready; when it is not ready the save waits up
  to 2 s for it and then saves without re-indenting. It builds the whole
  text twice and every line as a string: at 100 MB the process peaks at
  1.5 GB and the system compresses memory, which also slows the save that
  follows (14 s, against 0.2 s without re-indenting).
- **Saving** blocks for about 0.2 s at 100 MB.

Opening off the GUI thread (from 2 MB on: reading, decoding and the line
index on a worker): `Document::load()` now blocks for 0.1 ms at 10 and
100 MB, and the text shows 27 ms and 213 ms later; smaller files are still
read in place (1.2 MB: 11 ms, most of it loading the syntax definitions the
first time).

Highlighting far ahead of the slices: the lines in view are highlighted
from a guessed state, starting with no state 200 lines above them (from
the last line highlighted when closer), until the exact highlighting gets
there (`SyntaxHighlighter::guess()`). A view at the end of the file takes
7 ms that way at 10 and 100 MB (the "Guess at the end" column of
`run-work.sh`), against the 22 s the slices need at 100 MB. A worker
thread would not bring the colors sooner: each line's state comes from the
one before, and KSyntaxHighlighting's definitions cache states as they
highlight (`DefinitionData::unify`), so two threads cannot share one.

## Not covered by the prototype

Selection, undo, preedit (marked text) display, soft wrap, horizontal
scrolling, accessibility, the gutter. These are Stage 4. Two things to watch
there:

- `TextBuffer` walks its piece list (O(pieces)) to find a line or position.
  Typing coalesces into one piece, but many scattered edits (find and
  replace all) grow the list; a balanced tree of pieces (or a piece index
  with cumulative counts) fixes that if it shows up in measurements.
- Soft wrap needs a visual-line index next to the logical-line one, built
  incrementally, so that scrolling stays independent of the file size.

## Re-running

```sh
cmake -B build/Qt_6_12_0_for_macOS_Release -G Ninja \
  -DCMAKE_PREFIX_PATH=/opt/Qt/6.12.0/macos -DCMAKE_BUILD_TYPE=Release
cmake --build build/Qt_6_12_0_for_macOS_Release --target kode_editor_bench
tools/editor-bench/run.sh build/Qt_6_12_0_for_macOS_Release
cmake --build build/Qt_6_12_0_for_macOS_Release --target kode_work_bench
tools/editor-bench/run-work.sh build/Qt_6_12_0_for_macOS_Release
# By hand: build/…/tools/editor-bench/kode-editor-bench try FILE
```

Each case runs in its own process and opens a window: leave the machine alone
while it runs (the 100 MB TextEdit case takes several minutes).
