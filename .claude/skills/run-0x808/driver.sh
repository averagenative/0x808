#!/usr/bin/env bash
# driver.sh — launch and drive 0x808 (ImGui or GTK frontend) on a private
# Xvfb display, with a sandboxed HOME so the user's real session/autosave
# and audio device are never touched.
#
# All X/Y coordinates are relative to the app's main window ("0x808"), and
# `ss` crops to that window, so a pixel in a screenshot is a click target.
#
# Usage (from anywhere):
#   driver.sh start imgui|gtk [--project FILE.sqproj] [--user-data]
#   driver.sh ss NAME [--full]         screenshot -> $RUN/shots/NAME.png
#   driver.sh click X Y [HOLD_SEC]     held left click (default hold 0.6s)
#   driver.sh rclick X Y               held right click
#   driver.sh drag X1 Y1 X2 Y2         left-drag
#   driver.sh scroll X Y up|down [N]   mouse wheel N ticks (default 3)
#   driver.sh key KEYSYM...            e.g. key space / key ctrl+z
#   driver.sh type TEXT
#   driver.sh crop NAME X Y W H [SCALE%]  zoom a region of shot NAME -> NAME_crop.png
#   driver.sh changed A B [WxH+X+Y]    pixels that differ between two shots
#   driver.sh windows                  visible X windows + geometry
#   driver.sh log [N]                  tail the app log
#   driver.sh status | stop
#
# Env overrides: RUN_0X808_DIR (state dir), RUN_0X808_DISPLAY (default :97),
#                RUN_0X808_SCREEN (default 1600x900).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
RUN="${RUN_0X808_DIR:-${TMPDIR:-/tmp}/run-0x808-$(id -u)}"
DISP="${RUN_0X808_DISPLAY:-:97}"
SCREEN="${RUN_0X808_SCREEN:-1600x900}"
SANDBOX_HOME="$RUN/home"
DATA="$SANDBOX_HOME/.local/share/0x808"
SHOTS="$RUN/shots"

export DISPLAY="$DISP"

die() { echo "driver: $*" >&2; exit 1; }
pause() { python3 -c "import time; time.sleep($1)"; }   # foreground sleep, short

alive() { [ -f "$1" ] && kill -0 "$(cat "$1")" 2>/dev/null; }

wait_for() {  # wait_for SECONDS CMD...
    local secs=$1; shift
    local end=$((SECONDS + secs))
    until "$@" >/dev/null 2>&1; do
        [ $SECONDS -ge $end ] && return 1
        pause 0.3
    done
}

start_xvfb() {
    alive "$RUN/xvfb.pid" && return 0
    local n="${DISP#:}"
    rm -f "/tmp/.X11-unix/X$n" "/tmp/.X$n-lock" 2>/dev/null || true
    setsid Xvfb "$DISP" -screen 0 "${SCREEN}x24" -nolisten tcp >"$RUN/xvfb.log" 2>&1 &
    echo $! >"$RUN/xvfb.pid"
    wait_for 10 test -e "/tmp/.X11-unix/X$n" || die "Xvfb did not start (see $RUN/xvfb.log)"
}

seed_data() {  # seed_data PROJECT USER_DATA
    local project=$1 user_data=$2
    rm -rf "$SANDBOX_HOME"
    mkdir -p "$DATA"
    if [ "$user_data" = 1 ]; then
        local real="${XDG_DATA_HOME:-$HOME/.local/share}/0x808"
        for f in autosave.sqproj session.json; do
            [ -f "$real/$f" ] && cp "$real/$f" "$DATA/"
        done
        # GTK loads session.last_project, not the autosave: point it at a copy
        [ -f "$DATA/autosave.sqproj" ] && [ -z "$project" ] && project="$DATA/autosave.sqproj"
    fi
    if [ -n "$project" ]; then
        [ -f "$project" ] || die "no such project: $project"
        cp "$project" "$DATA/project.sqproj.tmp"
        mv "$DATA/project.sqproj.tmp" "$DATA/project.sqproj"
        cp "$DATA/project.sqproj" "$DATA/autosave.sqproj"     # ImGui loads this
        python3 -I - "$DATA/session.json" "$DATA/project.sqproj" <<'EOF'
import json, os, sys
path, proj = sys.argv[1], sys.argv[2]
d = json.load(open(path)) if os.path.exists(path) else {"version": 1}
d["last_project"] = proj                                   # GTK loads this
json.dump(d, open(path, "w"), indent=1)
EOF
    fi
}

cmd_start() {
    local fe="${1:-}"; shift || true
    local project="" user_data=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --project)   project="$(realpath "$2")"; shift 2 ;;
            --user-data) user_data=1; shift ;;
            *) die "unknown option $1" ;;
        esac
    done
    local bin
    case "$fe" in
        imgui) bin="$ROOT/build/0x808" ;;
        gtk)   bin="$ROOT/build/gtk/0x808_gtk" ;;
        *) die "start imgui|gtk" ;;
    esac
    [ -x "$bin" ] || die "not built: $bin (see SKILL.md Build)"
    alive "$RUN/app.pid" && die "already running (pid $(cat "$RUN/app.pid")); run: driver.sh stop"

    mkdir -p "$RUN" "$SHOTS"
    seed_data "$project" "$user_data"
    start_xvfb

    # Sandbox HOME/XDG so autosave + session writes stay in $RUN; dummy audio so
    # the saved device (e.g. a USB amp) is never opened; force X11 so nothing
    # lands on the real Wayland desktop.
    (cd "$ROOT" && exec setsid env -u WAYLAND_DISPLAY \
        DISPLAY="$DISP" HOME="$SANDBOX_HOME" XDG_DATA_HOME="$SANDBOX_HOME/.local/share" \
        SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy GDK_BACKEND=x11 GSK_RENDERER=cairo \
        "$bin" >"$RUN/app.log" 2>&1) &
    echo $! >"$RUN/app.pid"
    echo "$fe" >"$RUN/frontend"

    wait_for 30 xdotool search --name '^0x808$' || { tail -20 "$RUN/app.log"; die "window never appeared"; }
    pause 2   # first frames (software GL is slow)
    alive "$RUN/app.pid" || { tail -20 "$RUN/app.log"; die "app exited"; }
    rm -f "$RUN/warm"
    xdotool mousemove 0 0   # park the pointer (SDL centers its window, so off it)
    echo "started $fe (pid $(cat "$RUN/app.pid")) on $DISP, screen $SCREEN"
    echo "data: $DATA   log: $RUN/app.log   shots: $SHOTS"
    cmd_windows
}

cmd_stop() {
    for p in app xvfb; do
        if alive "$RUN/$p.pid"; then
            kill "$(cat "$RUN/$p.pid")" 2>/dev/null || true
            for _ in $(seq 1 25); do alive "$RUN/$p.pid" || break; pause 0.2; done
            alive "$RUN/$p.pid" && kill -9 "$(cat "$RUN/$p.pid")" 2>/dev/null || true
        fi
        rm -f "$RUN/$p.pid"
    done
    echo "stopped"
}

cmd_status() {
    for p in app xvfb; do
        if alive "$RUN/$p.pid"; then echo "$p: running (pid $(cat "$RUN/$p.pid"))"; else echo "$p: stopped"; fi
    done
    [ -f "$RUN/frontend" ] && echo "frontend: $(cat "$RUN/frontend")"
}

cmd_windows() {
    xdotool search --onlyvisible --name '' 2>/dev/null | while read -r w; do
        local name geo
        name=$(xdotool getwindowname "$w" 2>/dev/null || true)
        geo=$(xdotool getwindowgeometry "$w" 2>/dev/null | awk '/Position/{p=$2} /Geometry/{g=$2} END{print p" "g}')
        case "$geo" in *" 1x1") continue ;; esac
        printf '%-10s %-18s %s\n' "$w" "${name:-(unnamed)}" "$geo"
    done
}

# Main window origin + size -> WX WY WW WH. SDL centers its window on the
# screen and must not be moved (ImGui would keep using the old origin).
win_geom() {
    local w WINDOW X Y WIDTH HEIGHT SCREEN   # --shell output; keep it local
    w=$(xdotool search --onlyvisible --name '^0x808$' 2>/dev/null | head -1)
    [ -n "$w" ] || die "no 0x808 window (is it running?)"
    eval "$(xdotool getwindowgeometry --shell "$w")"
    WX=$X; WY=$Y; WW=$WIDTH; WH=$HEIGHT
}

move() {  # move X Y  (window-relative)
    win_geom
    xdotool mousemove $((WX + $1)) $((WY + $2))
}

pointer_in_window() {
    local X Y SCREEN WINDOW
    eval "$(xdotool getmouselocation --shell)"
    [ "$X" -ge "$WX" ] && [ "$X" -lt $((WX + WW)) ] && [ "$Y" -ge "$WY" ] && [ "$Y" -lt $((WY + WH)) ]
}

# The ImGui build drops the first mouse button press after launch and after
# every time the pointer re-enters its window (focus doesn't help). Spend that
# press on the middle button at the target: nothing in the app handles it.
ensure_warm() {  # ensure_warm X Y
    [ "$(cat "$RUN/frontend" 2>/dev/null)" = imgui ] || return 0
    win_geom
    if [ -f "$RUN/warm" ] && pointer_in_window; then return 0; fi
    move "$1" "$2";      pause 0.3
    xdotool mousedown 2; pause 0.6
    xdotool mouseup 2;   pause 0.4
    touch "$RUN/warm"
}

cmd_ss() {
    [ -n "${1:-}" ] || die "ss NAME [--full]"
    mkdir -p "$SHOTS"
    if [ "${2:-}" = --full ]; then
        import -window root "$SHOTS/$1.png"
    else
        win_geom
        import -window root -crop "${WW}x${WH}+${WX}+${WY}" +repage "$SHOTS/$1.png"
    fi
    echo "$SHOTS/$1.png"
}

press() {  # press BUTTON X Y HOLD
    ensure_warm "$2" "$3"
    move "$2" "$3"
    pause 0.3                      # let a frame see the hover
    xdotool mousedown "$1"
    pause "$4"                     # ImGui at llvmpipe frame rates drops sub-frame clicks
    xdotool mouseup "$1"
    pause 0.6
}

cmd_click()  { press 1 "$1" "$2" "${3:-0.6}"; }
cmd_rclick() { press 3 "$1" "$2" "${3:-0.6}"; }

cmd_drag() {
    ensure_warm "$1" "$2"
    move "$1" "$2";      pause 0.3
    xdotool mousedown 1; pause 0.3
    local i
    for i in 1 2 3 4; do
        move $(( $1 + ($3 - $1) * i / 4 )) $(( $2 + ($4 - $2) * i / 4 ))
        pause 0.2
    done
    pause 0.3
    xdotool mouseup 1;            pause 0.6
}

cmd_scroll() {
    local btn=4; [ "${3:-}" = down ] && btn=5
    ensure_warm "$1" "$2"
    move "$1" "$2"; pause 0.3
    local i; for i in $(seq 1 "${4:-3}"); do xdotool click "$btn"; pause 0.05; done
    pause 0.6
}

cmd_crop() {
    local name=$1 x=$2 y=$3 w=$4 h=$5 scale=${6:-200}
    magick "$SHOTS/$name.png" -crop "${w}x${h}+${x}+${y}" +repage -scale "${scale}%" "$SHOTS/${name}_crop.png"
    echo "$SHOTS/${name}_crop.png"
}

cmd_changed() {
    local a="$SHOTS/$1.png" b="$SHOTS/$2.png" geo=${3:-}
    if [ -n "$geo" ]; then
        magick compare -metric AE "$a[$geo]" "$b[$geo]" null: 2>&1 || true
    else
        magick compare -metric AE "$a" "$b" null: 2>&1 || true
    fi
    echo
}

sub="${1:-}"; shift || true
case "$sub" in
    start)   cmd_start "$@" ;;
    stop)    cmd_stop ;;
    status)  cmd_status ;;
    windows) cmd_windows ;;
    ss)      cmd_ss "$@" ;;
    click)   cmd_click "$@" ;;
    rclick)  cmd_rclick "$@" ;;
    drag)    cmd_drag "$@" ;;
    scroll)  cmd_scroll "$@" ;;
    key)     xdotool key --delay 80 "$@"; pause 0.6 ;;
    type)    xdotool type --delay 80 "$*"; pause 0.6 ;;
    crop)    cmd_crop "$@" ;;
    changed) cmd_changed "$@" ;;
    log)     tail -n "${1:-30}" "$RUN/app.log" ;;
    *) sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac
