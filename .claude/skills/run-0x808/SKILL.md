---
name: run-0x808
description: Build, run, and drive the 0x808 drum machine (ImGui and GTK frontends) headlessly. Use when asked to run or start 0x808, launch the GUI, take a screenshot of the app, click through the UI (piano roll, presets, patterns), verify a GUI change in the real app, or run its tests.
---

0x808 is a C/C++ drum machine with two desktop frontends: ImGui (`build/0x808`,
SDL2 + OpenGL) and GTK 4 (`build/gtk/0x808_gtk`). Drive either one with
`.claude/skills/run-0x808/driver.sh`. It runs the app on a private Xvfb display,
sandboxes HOME so the user's session, autosave and audio device are never
touched, and exposes `ss` / `click` / `drag` / `scroll` / `key` commands.

All paths are relative to the repo root.

## Prerequisites (Fedora 44, verified installed)

```bash
rpm -q xorg-x11-server-Xvfb xdotool ImageMagick mesa-dri-drivers cmake gcc-c++ sdl2-compat-devel gtk4-devel alsa-lib-devel
```

Install any that are missing with `sudo dnf install`. `mesa-dri-drivers`
provides the llvmpipe software OpenGL that the ImGui build renders with under
Xvfb. `python3` is also used by the driver.

## Build

```bash
cmake -S . -B build && cmake --build build -j"$(nproc)"                                     # ImGui -> build/0x808
cmake -S . -B build/gtk -DBUILD_GTK=ON && cmake --build build/gtk -j"$(nproc)"              # GTK   -> build/gtk/0x808_gtk
```

The make targets are `sequencer_gui` and `sequencer_gtk` (output names `0x808`
and `0x808_gtk`). `make 0x808` builds nothing. `build/gtk/` sits under the
gitignored `build/` and gets its own `samples` and `themes` symlinks.

## Run (agent path)

```bash
D=.claude/skills/run-0x808/driver.sh
$D start imgui                      # or: start gtk
$D ss home                          # -> /tmp/run-0x808-$UID/shots/home.png ; Read it
$D click 646 57                     # ImGui: pattern "2" button (window-relative coords)
$D key 3                            # keys 1-9 switch patterns (ImGui)
$D scroll 350 600 up 6              # wheel over the ImGui piano roll
$D ss after
$D changed home after               # pixel count that differs (0 = click did nothing)
$D stop
```

- **Coordinates are window-relative.** `ss` crops to the `0x808` main window,
  so a pixel in the screenshot is the click target. Use `ss NAME --full` for
  the whole screen, which also shows separate toplevels such as GTK's
  "Pattern Presets".
- **Seed data:**
  - `start imgui|gtk --project FILE.sqproj` loads a project in either frontend.
  - `--user-data` copies the user's real `~/.local/share/0x808/{autosave.sqproj,session.json}` (read-only) into the sandbox.
  - With neither flag, you get the built-in demo pattern (808 kit, Trap 808 bass on track 9).
- **Other commands:** `drag X1 Y1 X2 Y2`, `rclick X Y`, `type TEXT`,
  `crop NAME X Y W H [SCALE%]` (zoom a region of the existing shot NAME into
  `NAME_crop.png`; default 200%), `windows`, `log`, `status`.
  Run `driver.sh` with no arguments for usage.
- **State:** everything lives in `/tmp/run-0x808-$UID/` (`app.log`, `shots/`, and the sandboxed `home/`).
  Override with `RUN_0X808_DIR`, `RUN_0X808_DISPLAY` (default `:97`) and
  `RUN_0X808_SCREEN` (default `1600x900`).

Verified flows:
- **ImGui:** `start imgui --user-data` → `key 3` → `ss` shows pattern 3 and the piano roll centered on the track's bass notes.
- **GTK:** `start gtk` → `scroll 400 550 up 12` → `click 653 31` (PRESETS) → `click 150 120` (bass dropdown) → `scroll 150 450 down 5` → `click 70 383` (Metal Chug) → `click 341 120` (Apply Bass) → `ss`. The piano roll jumps to C0–C1 and shows all 9 notes.

## Run (human path)

`./build/0x808` or `./build/gtk/0x808_gtk` from the repo root opens a window on
the real desktop. It uses the real `~/.local/share/0x808` session and autosave,
plus the real audio device. Don't do this while the user has their own
instance open, because both write `autosave.sqproj`.

## Test

```bash
bash scripts/test_all.sh quick
```

`engine_render_test` fails its virtual-keyboard timing check (known flaky, see
CLAUDE.md), and the script exits on the first failure (`set -e`), so the rest
never run. Run the remaining test binaries directly:

```bash
for t in $(grep -oE '"\$\{BUILD_DIR\}/[a-z_]+"' scripts/test_all.sh | sed 's/.*\///;s/"//' | sort -u | grep -v engine_render_test); do [ -x build/$t ] || continue; ./build/$t >/dev/null 2>&1 && echo "pass $t" || echo "FAIL $t"; done
```

## Gotchas

- **The ImGui build drops the first mouse press after launch, and again every time the pointer re-enters its window.**
  - Focusing the window or moving the mouse first doesn't help.
  - `driver.sh` handles it: if the pointer is cold, it spends that press on the middle button at the target. Nothing in the app handles the middle button.
  - If you use raw `xdotool` instead, click twice. The second click is the real one.
  - GTK doesn't have this problem.
- **Don't move the SDL window.** Without a window manager, SDL centers the ImGui
  window (160,90 on a 1600×900 screen). After `xdotool windowmove`, hover
  still works, but clicks land at the old origin. The driver translates
  coordinates instead of moving the window.
- **Software GL is slow.** The ImGui build pegs about 9 cores on llvmpipe.
  Instant `xdotool click` presses can fall between frames, so the driver holds
  each press for 0.6 s.
- **The two frontends load different files at startup.** ImGui loads
  `$XDG_DATA_HOME/0x808/autosave.sqproj`. GTK ignores the autosave and loads
  `session.json` → `last_project`. `--project` sets up both.
- **The ImGui log always says `Demo pattern: 10 tracks, BPM=145`.** It's
  logged before the autosave loads, so it doesn't mean the load failed. INFO
  lines aren't printed, only WARN and above.
- **The GTK window resizes itself.**
  - A status message in the toolbar ("Applied bass preset") widens the window up to the screen width, which shifts every widget to the right.
  - Re-run `ss` before clicking after any action.
  - The default 1600×900 screen exists so the 1538 px GTK window fits. On 1280×720 it got clipped.
- **GTK "Pattern Presets" is a separate toplevel at 0,0.**
  - It overlaps the main window, which is also at 0,0, so the same coordinates work for both.
  - Its dropdown lists scroll with `scroll X Y down N`.
- **Piano roll clicks differ by frontend.** In both, a left click on an
  empty cell places a note and a drag extends it. In GTK, a plain left click
  on an existing note also deletes it; ImGui deletes only with a right click.
- **Tracks are monophonic.** Placing a note on an occupied step replaces that
  step's note.
- **`pkill -f 0x808` (or `-f "Xvfb :97"`) can kill your own shell,** because the
  pattern matches the command line running it. `driver.sh stop` uses PID
  files.
- **Keep both env vars set:** `SDL_AUDIODRIVER=dummy` keeps the app off the
  user's saved audio device (for example a USB amp), and
  `env -u WAYLAND_DISPLAY` plus `SDL_VIDEODRIVER=x11` / `GDK_BACKEND=x11`
  keep the window off the real Wayland desktop. The driver sets all of these.

## Troubleshooting

- **A click changed nothing (`changed` reports a few hundred pixels, which is
  just logo pulse and hover):** in ImGui, the pointer was cold. The driver
  re-warms automatically, but raw `xdotool` doesn't. In GTK, re-screenshot,
  because the layout probably shifted.
- **`MESA-EGL: warning: DRI3 error: Could not get DRI3 device` in the GTK
  log:** harmless under Xvfb. The driver forces `GSK_RENDERER=cairo`.
- **`driver: already running`:** run `driver.sh stop` first. The state
  directory keeps PID files across calls.
