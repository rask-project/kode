# README and site screenshots

The README and the site (`site/`, through `tools/site-assets.sh`) show the
images in `docs/images/`. This file lists what each one shows and how it was
taken, so they can be retaken when the UI changes.

## Prerequisites

- **Permissions** for the terminal that runs the script, in **System
  Settings ▸ Privacy & Security**: *Screen & System Audio Recording*
  (`screencapture`) and *Accessibility* (keys and clicks). Restart the
  terminal after granting them.
- Xcode command-line tools (`swiftc`), `ffmpeg` (`brew install ffmpeg`) and a
  configured build directory of the app.

## The `docshot.sh` script

`tools/screenshots/docshot.sh` (from Konsoll's) builds an isolated copy of
Kode, opens it on a demo project, drives it, and captures and converts the
images. Run it without arguments for the full command list.

| Command | What it does |
| --- | --- |
| `setup [BUILD_DIR]` | Builds the copy (default build dir `build/Qt_6_12_0_for_macOS_Debug`), the Swift helpers, the demo project, the settings and the backdrop |
| `session [dark\|light]` | Writes the demo session (the project in the sidebar, `Forecast.cpp` beside `ForecastView.qml`) and the appearance; the app must be stopped |
| `launch` / `stop` | Starts the copy (window at 60,90, 1560×880 pt) / quits it with the backdrop and the HUD |
| `backdrop on [IMAGE\|wallpaper]` / `backdrop off` | A click-through full-screen background behind the app: the gradient `setup` made, an image, or the wallpaper from `wallpaper` |
| `hud show X Y` / `hud say TEXT` / `hud off` | A caption pill for GIFs (the key being pressed) |
| `windows` / `place WINDOW X Y W H` / `front` | The app's windows (`ID X Y W H LAYER TITLE`), move or resize one (index or title), bring the app forward |
| `open FILE` | Opens a file in the running copy (the active split) |
| `click X Y [COUNT]`, `rclick X Y`, `move X Y`, `drag X1 Y1 X2 Y2 [HOLD_MS]` | Mouse input |
| `type [--enter] TEXT`, `key KEY [MODIFIERS]` | Keyboard input, e.g. `key d control`, `key left option,command` |
| `shot NAME [MARGIN]`, `shot-rect NAME X Y W H` | A PNG of the front window plus a margin of backdrop, or of any rectangle |
| `record NAME SECONDS [MARGIN \| X Y W H]`, `record-wait` | A screen recording in the background while the script acts |
| `gif IN OUT START DURATION [WIDTH]`, `png IN OUT [MAX_WIDTH]` | Optimized GIF (12 fps, 128 colors) and PNG (≤1600 px, 256 colors) |

Coordinates are points with (0,0) at the top-left of the built-in display;
read them off `windows` and a `shot`. Files go to `$KODE_DOCS_DIR` (default
`/tmp/kode-docs`), shots and recordings to its `shots/` folder.

### What it takes care of

- **Settings.** The copy is built with the organization domain
  `rask-project-docs.github.com`, so it reads and writes
  `com.github.rask-project-docs.Kode` (preferences, session, recent
  documents) instead of yours; its bundle id is
  `com.github.rask-project.kode.docs`. `setup` patches `main.cpp`, builds,
  copies the app and restores `main.cpp` (it refuses to run when `main.cpp`
  has local changes), then rebuilds the build directory from the clean
  sources. It takes your editor font; the rest is fixed (13 pt, theme Kode,
  transparent code area, large sidebar).
- **The demo project.** `tools/screenshots/demo/aurora` (a small weather
  station in C++, QML and Python, kept out of `tools/format-code.sh`) is
  copied to `$KODE_DOCS_DIR/home/aurora`, so no path of yours shows.
- **The see-through window.** Whatever is behind it shows through, so keep
  the backdrop on: `backdrop on` puts a neutral gradient behind the app (no
  personal wallpaper or windows), and `shot` captures a screen rectangle, so
  menus and popups are included.

### Example: a screenshot and a GIF

```sh
D=tools/screenshots/docshot.sh
$D setup                       # once, or after changing the app
$D launch
$D backdrop on

# The overview: the caret on a word, its occurrences marked
$D click 590 606               # "readings" on line 26 of Forecast.cpp
$D move 1400 1000              # the pointer out of the way
$D shot overview
$D png /tmp/kode-docs/shots/overview.png docs/images/overview.png

# Multiple carets, the keys in the HUD, cropped to the left split
$D click 743 444               # "readings" on line 17
$D hud show 640 800
$D record carets 14 300 340 680 470
$D hud say '⌃D   select the word'; $D key d control; sleep 1.2
for i in 1 2 3; do $D hud say '⌃D   add the next occurrence'; $D key d control; sleep 0.9; done
$D hud say 'type once, edit every caret'; $D type 'samples'; sleep 1.5
$D hud say 'esc   back to one caret'; $D key escape; sleep 1.5
$D hud say ''
$D record-wait
$D gif /tmp/kode-docs/shots/carets.mov docs/images/multiple-carets.gif 1 12.5 900

$D stop
```

In the 1560×880 window, line *N* of the left split is at y ≈ 372 + (*N* −
13) × 18 pt, its text from x ≈ 380 and its fold markers at x = 358.

## Shot list

| File | What it shows | How |
| --- | --- | --- |
| `overview.png` | Dark: the project in the sidebar, `Forecast.cpp` beside `ForecastView.qml`, the caret on `readings` (its occurrences marked, the loop's indent guide in the accent color) | `session dark`, click line 26 |
| `overview-light.png` | The same in light mode | `session light` |
| `multiple-carets.gif` | ⌃D on `readings` four times, `samples` typed, Escape | Above; `key z command` undoes it |
| `code-folding.gif` | ⌥⌘← on the `for` loop, ⌥⌘→, then the gutter marker of `pressureSlope` twice | Caret on line 25; markers at 358,462 |
| `select-block.gif` | Light: ⌘U four times from line 28, ⇧⌘U twice | Recorded after `overview-light` |
| `search.png` | ⌘F `sum`, ↩ once: 5 of 14, the matches marked | — |
| `document-switcher.png` | ⌃Tab over `fetch_readings.py`, seven documents listed | `open` `Forecast.h`, `main.cpp`, `stations.json`, `fetch_readings.py`; hold Control with `osascript -e 'tell application "System Events"' -e 'key down control' -e 'key code 48' -e 'delay 3' -e 'key up control' -e 'end tell' &`, `shot` after 1.6 s |
| `sidebar.png` | The right-click menu on `Forecast.h` | `rclick 160 309`, cropped to the left 1500×1380 px |
| `settings.png` | Settings' Appearance, Editor and Files sections side by side | ⌘,; the editor window `place`d out of the way; `shot NAME 40` per section, each scaled to 800 px, `hstack` |

Before committing, check every image for anything personal: paths under
your home folder, other apps' windows or notifications, and the menu-bar
clock.
