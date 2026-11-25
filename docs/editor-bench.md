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
cmake --build build/Qt_6_12_0_for_macOS_Release --target tekst_editor_bench
tools/editor-bench/run.sh build/Qt_6_12_0_for_macOS_Release
# By hand: build/…/tools/editor-bench/tekst-editor-bench try FILE
```

Each case runs in its own process and opens a window: leave the machine alone
while it runs (the 100 MB TextEdit case takes several minutes).
