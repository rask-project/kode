#!/bin/bash
# Drives an isolated Kode to take the README and site screenshots and GIFs.
# See docs/SCREENSHOTS.md for the workflow and the shot list.
#
# Usage: tools/screenshots/docshot.sh COMMAND [ARGS...]
#
#   setup [BUILD_DIR]        build an isolated copy of the app (own settings
#                            domain and bundle id), the Swift helpers and the
#                            demo project
#   session [dark|light]     write the demo session (the project in the
#                            sidebar, two splits) and the appearance; the
#                            app must be stopped
#   launch                   start the copy (window at 60,90, 1560×880 pt)
#   stop                     quit the copy, the backdrop and the HUD
#
#   wallpaper                capture the desktop wallpaper (no windows, no
#                            icons) to $DIR/wallpaper.png
#   backdrop on [IMAGE|wallpaper] | off
#                            full-screen background behind the app: the
#                            gradient setup made, an image or the wallpaper
#   hud show X Y             caption pill, bottom-center at X,Y
#   hud say TEXT...          set the caption ("" hides it)
#   hud off
#
#   windows                  the app's windows: ID X Y W H LAYER TITLE
#   place WINDOW X Y W H     move/resize WINDOW (index from 1, or its title)
#   front                    bring the app forward
#   open FILE                open FILE in the running copy (active split)
#   click X Y [COUNT] | rclick X Y | move X Y | drag X1 Y1 X2 Y2 [HOLD_MS]
#   type [--enter] TEXT      type TEXT (safe on dead-key layouts)
#   key KEY [MODIFIERS]      KEY: left right up down return escape delete
#                            fwd-delete tab, or one character; MODIFIERS:
#                            comma list of command,shift,option,control
#
#   shot NAME [MARGIN]       capture the front window plus MARGIN pt (28)
#   shot-rect NAME X Y W H   capture a screen rectangle
#   record NAME SECONDS [MARGIN | X Y W H]
#                            record in the background (front window + margin
#                            by default); wait with `record-wait`
#   record-wait
#   gif IN.mov OUT.gif START DURATION [WIDTH]   12 fps palette GIF (1000 px)
#   png IN.png OUT.png [MAX_WIDTH]              scale (1600 px) + 256 colors
#
# Coordinates are global points with (0,0) at the top-left of the built-in
# display. Files go to $KODE_DOCS_DIR (default /tmp/kode-docs); shots and
# recordings to its shots/ folder unless NAME is a path.
#
# Needs: Screen & System Audio Recording and Accessibility permissions for the
# terminal that runs it, Xcode command-line tools (swiftc), ffmpeg.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOLS="$ROOT/tools/screenshots"
DIR=${KODE_DOCS_DIR:-/tmp/kode-docs}
BIN="$DIR/bin"
SHOTS="$DIR/shots"
BUNDLE="$DIR/Kode.app"
APP="$BUNDLE/Contents/MacOS/Kode"
PROJECT="$DIR/home/aurora"
DOMAIN=com.github.rask-project-docs.Kode
REAL_DOMAIN=com.github.rask-project.Kode

die() {
    echo "docshot: $*" >&2
    exit 1
}

pid_of() { # NAME -> pid in $DIR/NAME.pid if that process is alive
    local file="$DIR/$1.pid"
    [[ -f $file ]] && kill -0 "$(cat "$file")" 2>/dev/null && cat "$file"
}

app_pid() {
    pid_of app || die "Kode is not running; run '$0 launch'"
}

out_path() { # NAME EXT -> path
    if [[ $1 == */* ]]; then echo "$1"; else echo "$SHOTS/$1.$2"; fi
}

# The frontmost normal window of the app: "X Y W H".
front_bounds() {
    "$BIN/ctl" windows "$(app_pid)" | awk '$6 == 0 { print $2, $3, $4, $5; exit }'
}

osa() { # AppleScript lines for System Events, run with the app in front
    local pid
    pid=$(app_pid)
    osascript -e 'tell application "System Events"' \
        -e "set frontmost of (first process whose unix id is $pid) to true" \
        -e 'delay 0.2' -e "$1" -e 'end tell'
}

applescript_string() {
    local s=${1//\\/\\\\}
    printf '"%s"' "${s//\"/\\\"}"
}

# Dead keys (ABNT, US International) swallow the next character: send each
# of them followed by a space, which produces the bare character.
type_script() {
    local text=$1 chunk="" lines="" ch i
    for ((i = 0; i < ${#text}; i++)); do
        ch=${text:i:1}
        case $ch in
        \" | \' | '~' | '^' | '`')
            [[ -n $chunk ]] && lines+="keystroke $(applescript_string "$chunk")"$'\n'
            chunk=""
            lines+="keystroke $(applescript_string "$ch")"$'\n'"keystroke \" \""$'\n'
            ;;
        *) chunk+=$ch ;;
        esac
    done
    [[ -n $chunk ]] && lines+="keystroke $(applescript_string "$chunk")"$'\n'
    printf '%s' "$lines"
}

modifiers_clause() { # "command,shift" -> " using {command down, shift down}"
    [[ -z ${1:-} ]] && return
    local list="" mod
    IFS=, read -ra mods <<<"$1"
    for mod in "${mods[@]}"; do
        case $mod in
        command | shift | option | control) list+="${list:+, }$mod down" ;;
        *) die "unknown modifier: $mod" ;;
        esac
    done
    echo " using {$list}"
}

key_script() {
    local code
    case $1 in
    left) code=123 ;;
    right) code=124 ;;
    up) code=126 ;;
    down) code=125 ;;
    return) code=36 ;;
    escape) code=53 ;;
    delete) code=51 ;;
    fwd-delete) code=117 ;;
    tab) code=48 ;;
    ?) echo "keystroke $(applescript_string "$1")$(modifiers_clause "${2:-}")"; return ;;
    *) die "unknown key: $1" ;;
    esac
    echo "key code $code$(modifiers_clause "${2:-}")"
}

# --- setup ---------------------------------------------------------------

build_isolated_app() {
    local build_dir=$1
    [[ -f $build_dir/build.ninja || -f $build_dir/Makefile ]] || die "no configured build in $build_dir"
    git -C "$ROOT" diff --quiet -- main.cpp || die "main.cpp has local changes; commit or stash them first"
    # Settings live under the organization domain: point the copy at its own.
    trap 'git -C "$ROOT" checkout -- main.cpp' EXIT
    sed -i '' 's/"rask-project.github.com")/"rask-project-docs.github.com")/' "$ROOT/main.cpp"
    grep -q rask-project-docs "$ROOT/main.cpp" || die "could not patch the settings domain in main.cpp"
    cmake --build "$build_dir" --target kode
    rm -rf "$BUNDLE"
    cp -R "$build_dir/Kode.app" "$BUNDLE"
    git -C "$ROOT" checkout -- main.cpp
    trap - EXIT
    # Its own bundle id too: the system's recent documents and open events.
    plutil -replace CFBundleIdentifier -string com.github.rask-project.kode.docs "$BUNDLE/Contents/Info.plist"
    codesign --force --deep --sign - "$BUNDLE" 2>/dev/null
    echo "Rebuilding $build_dir from the unpatched sources…"
    cmake --build "$build_dir" --target kode >/dev/null
}

write_demo_project() {
    rm -rf "$PROJECT"
    mkdir -p "$(dirname "$PROJECT")"
    cp -R "$TOOLS/demo/aurora" "$PROJECT"
    (
        cd "$PROJECT"
        export GIT_AUTHOR_NAME=Demo GIT_COMMITTER_NAME=Demo
        export GIT_AUTHOR_EMAIL=demo@example.com GIT_COMMITTER_EMAIL=demo@example.com
        git init -q -b main && git add . && git commit -qm "Aurora: a small weather station"
    )
}

# A neutral, colorful background for the see-through window (no personal
# wallpaper in the shots).
write_backdrop() {
    ffmpeg -loglevel error -y -f lavfi -i \
        "gradients=s=3360x2100:c0=0x0e1a3a:c1=0x2b1b52:c2=0x0b4a5a:c3=0x173d2f:nb_colors=4:x0=0:y0=0:x1=3360:y1=2100:seed=7,format=rgb24" \
        -frames:v 1 "$DIR/backdrop.png"
}

write_demo_settings() {
    local font
    font=$(defaults read "$REAL_DOMAIN" editor.fontFamily 2>/dev/null || true)
    [[ -n $font ]] && defaults write "$DOMAIN" editor.fontFamily "$font"
    defaults write "$DOMAIN" view.fontSize -int 13
    defaults write "$DOMAIN" view.theme Kode
    defaults write "$DOMAIN" window.transparentEditor -bool true
    defaults write "$DOMAIN" sidebar.size -int 2
    defaults write "$DOMAIN" sidebar.syncWithEditor -bool true
}

cmd_setup() {
    local build_dir=${1:-$ROOT/build/Qt_6_12_0_for_macOS_Debug}
    mkdir -p "$DIR" "$BIN" "$SHOTS"
    local tool
    for tool in ctl backdrop hud; do
        swiftc -O "$TOOLS/$tool.swift" -o "$BIN/$tool"
    done
    build_isolated_app "$build_dir"
    write_demo_project
    write_demo_settings
    write_backdrop
    cmd_session dark
    echo "Ready in $DIR. Next: $0 launch"
}

# The project in the sidebar, the C++ source beside the QML view.
cmd_session() {
    [[ -z $(pid_of app || true) ]] || die "stop the app first (it saves its session on quit)"
    local scheme
    case ${1:-dark} in
    dark) scheme=2 ;;
    light) scheme=1 ;;
    *) die "usage: session [dark|light]" ;;
    esac
    defaults write "$DOMAIN" appearance.colorScheme -int "$scheme"
    local url="file://$PROJECT" json
    json=$(
        cat <<EOF
{"documents":["$url/src/Forecast.cpp","$url/qml/ForecastView.qml","$url/README.md"],
 "windows":[{"x":60,"y":90,"width":1560,"height":880,"visibility":"normal",
  "folder":"$url","sidebar":true,"sidebarWidth":250,
  "splits":[{"file":"$url/src/Forecast.cpp"},{"file":"$url/qml/ForecastView.qml"},{"split":1}]}]}
EOF
    )
    defaults write "$DOMAIN" session.state -data "$(printf '%s' "$json" | tr -d '\n' | xxd -p | tr -d '\n')"
}

# --- running -------------------------------------------------------------

cmd_launch() {
    [[ -x $APP ]] || die "no isolated app; run '$0 setup' first"
    [[ -n $(pid_of app || true) ]] && die "already running (pid $(pid_of app))"
    (
        cd "$DIR/home" || exit 1
        nohup "$APP" >"$DIR/app.log" 2>&1 &
        echo $! >"$DIR/app.pid"
    )
    local i
    for i in {1..100}; do
        [[ -n $(front_bounds 2>/dev/null || true) ]] && break
        sleep 0.1
    done
    [[ -n $(front_bounds 2>/dev/null || true) ]] || die "no window after 10 s; see $DIR/app.log"
    cmd_place 1 60 90 1560 880
    echo "Kode running (pid $(cat "$DIR/app.pid"))"
}

stop_pid() {
    local pid
    pid=$(pid_of "$1" || true)
    [[ -n $pid ]] && kill "$pid" 2>/dev/null || true
    rm -f "$DIR/$1.pid"
}

cmd_stop() {
    stop_pid hud
    stop_pid backdrop
    stop_pid app
}

cmd_backdrop() {
    case ${1:-} in
    on)
        stop_pid backdrop
        local image=${2:-$DIR/backdrop.png}
        [[ $image == wallpaper ]] && image=$DIR/wallpaper.png
        [[ -z $image || -f $image ]] || die "no image $image (run '$0 wallpaper' first)"
        nohup "$BIN/backdrop" ${image:+"$image"} >/dev/null 2>&1 &
        echo $! >"$DIR/backdrop.pid"
        sleep 0.5
        [[ -n $(pid_of app || true) ]] && cmd_front
        ;;
    off) stop_pid backdrop ;;
    *) die "usage: backdrop on [IMAGE|wallpaper] | off" ;;
    esac
}

cmd_wallpaper() {
    local id
    id=$("$BIN/ctl" wallpaper) || die "could not find the wallpaper window"
    screencapture -x -o -l "$id" "$DIR/wallpaper.png"
    echo "$DIR/wallpaper.png"
}

cmd_hud() {
    case ${1:-} in
    show)
        [[ $# -eq 3 ]] || die "usage: hud show X Y"
        stop_pid hud
        : >"$DIR/hud.txt"
        nohup "$BIN/hud" "$DIR/hud.txt" "$2" "$3" >/dev/null 2>&1 &
        echo $! >"$DIR/hud.pid"
        ;;
    say)
        shift
        printf '%s\n' "$*" >"$DIR/hud.txt"
        ;;
    off) stop_pid hud ;;
    *) die "usage: hud show X Y | hud say TEXT | hud off" ;;
    esac
}

cmd_place() {
    [[ $# -eq 5 ]] || die "usage: place WINDOW X Y W H"
    local window
    if [[ $1 =~ ^[0-9]+$ ]]; then window="window $1"; else window="(first window whose name is $(applescript_string "$1"))"; fi
    osascript -e "tell application \"System Events\" to tell (first process whose unix id is $(app_pid)) to tell $window to set {position, size} to {{$2, $3}, {$4, $5}}" >/dev/null
    osascript -e "tell application \"System Events\" to tell (first process whose unix id is $(app_pid)) to perform action \"AXRaise\" of $window" >/dev/null
}

cmd_front() {
    osascript -e "tell application \"System Events\" to set frontmost of (first process whose unix id is $(app_pid)) to true"
}

cmd_open() {
    [[ $# -eq 1 ]] || die "usage: open FILE"
    app_pid >/dev/null
    open -a "$BUNDLE" "$1"
}

cmd_type() {
    local enter=false
    [[ ${1:-} == --enter ]] && enter=true && shift
    [[ $# -eq 1 ]] || die "usage: type [--enter] TEXT"
    local script
    script=$(type_script "$1")
    $enter && script+=$'\nkey code 36'
    osa "$script"
}

# --- capturing -----------------------------------------------------------

cmd_shot() {
    [[ $# -ge 1 ]] || die "usage: shot NAME [MARGIN]"
    local margin=${2:-28} x y w h
    read -r x y w h <<<"$(front_bounds)"
    [[ -n ${h:-} ]] || die "no window to capture"
    cmd_shot_rect "$1" $((x - margin)) $((y - margin)) $((w + 2 * margin)) $((h + 2 * margin))
}

cmd_shot_rect() {
    [[ $# -eq 5 ]] || die "usage: shot-rect NAME X Y W H"
    local out
    out=$(out_path "$1" png)
    mkdir -p "$(dirname "$out")"
    screencapture -x -R "$2,$3,$4,$5" "$out"
    echo "$out"
}

cmd_record() {
    [[ $# -ge 2 ]] || die "usage: record NAME SECONDS [MARGIN | X Y W H]"
    local out rect
    out=$(out_path "$1" mov)
    if [[ $# -eq 6 ]]; then
        rect="$3,$4,$5,$6"
    else
        local margin=${3:-28} x y w h
        read -r x y w h <<<"$(front_bounds)"
        rect="$((x - margin)),$((y - margin)),$((w + 2 * margin)),$((h + 2 * margin))"
    fi
    mkdir -p "$(dirname "$out")"
    rm -f "$out"
    nohup screencapture -x -v -V "$2" -R "$rect" "$out" >/dev/null 2>&1 &
    echo $! >"$DIR/record.pid"
    sleep 1.5 # screencapture takes a moment to start
    echo "$out"
}

cmd_record_wait() {
    local pid
    pid=$(pid_of record || true)
    [[ -z $pid ]] && return
    while kill -0 "$pid" 2>/dev/null; do sleep 0.2; done
    rm -f "$DIR/record.pid"
}

cmd_gif() {
    [[ $# -ge 4 ]] || die "usage: gif IN OUT START DURATION [WIDTH]"
    local width=${5:-1000}
    ffmpeg -loglevel error -y -ss "$3" -t "$4" -i "$1" -vf \
        "fps=12,scale=$width:-1:flags=lanczos,split[a][b];[a]palettegen=stats_mode=diff:max_colors=128[p];[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle" \
        -loop 0 "$2"
    ls -lh "$2"
}

cmd_png() {
    [[ $# -ge 2 ]] || die "usage: png IN OUT [MAX_WIDTH]"
    local max=${3:-1600}
    ffmpeg -loglevel error -y -i "$1" -vf \
        "scale='min($max,iw)':-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=256:stats_mode=full[p];[b][p]paletteuse=dither=sierra2_4a" \
        "$2"
    ls -lh "$2"
}

# --- dispatch ------------------------------------------------------------

command=${1:-}
[[ -n $command ]] || {
    sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'
    exit 2
}
shift
case $command in
setup) cmd_setup "$@" ;;
session) cmd_session "$@" ;;
launch) cmd_launch ;;
stop) cmd_stop ;;
backdrop) cmd_backdrop "$@" ;;
wallpaper) cmd_wallpaper ;;
hud) cmd_hud "$@" ;;
windows) "$BIN/ctl" windows "$(app_pid)" ;;
place) cmd_place "$@" ;;
front) cmd_front ;;
open) cmd_open "$@" ;;
click | rclick | move | drag) "$BIN/ctl" "$command" "$@" ;;
type) cmd_type "$@" ;;
key) osa "$(key_script "$@")" ;;
shot) cmd_shot "$@" ;;
shot-rect) cmd_shot_rect "$@" ;;
record) cmd_record "$@" ;;
record-wait) cmd_record_wait ;;
gif) cmd_gif "$@" ;;
png) cmd_png "$@" ;;
*) die "unknown command: $command (run without arguments for help)" ;;
esac
