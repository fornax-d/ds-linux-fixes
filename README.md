# Don't Starve – Linux fixes

One preloaded library for the native Linux version of single-player Don't
Starve (Steam, 32-bit). Game files are not modified. Sister project of
[dst-linux-fixes](https://github.com/fornax-d/dst-linux-fixes).

- **Native Wayland** on the SDL from Steam's runtime container (the game's own
  SDL is X11-only), with the game's mouse handling kept correct.
- **No 60 fps cap**, or any cap with accurate frame pacing.
- **NVIDIA GPU on Wayland** through Zink (see below).

Needs Steam's default Linux runtime for the game. Tested with game build
12527201 (all DLCs), Intel and NVIDIA, KDE Plasma.

## Install

```sh
./install.sh
```

It finds the game, builds `ds_fixes.so` (or uses `prebuilt/` without a 32-bit
compiler) and copies it to `bin/shims/`. Then set the launch options
(Steam → Don't Starve → Properties):

```
LD_PRELOAD='$ORIGIN/shims/ds_fixes.so':"$LD_PRELOAD" %command%
```

Other wrappers go before `%command%`, e.g. `… mangohud %command%`.
Remove with `./install.sh --uninstall` and clear the launch options.

## Settings

Put them in front, e.g. `DS_FPS_LIMIT=144 LD_PRELOAD=… %command%`.

| Variable | Default | |
|---|---|---|
| `DS_WAYLAND` | Wayland session: 1 | `0`: X11 (XWayland) |
| `DS_ZINK` | `0` | `1`: render through Zink on the dedicated GPU |
| `DS_FPS_LIMIT` | `0` | `0`: no cap (vsync), `N`: N fps, `game`: original 60 |
| `DS_SWAP_INTERVAL` | game's | `0`: vsync off |
| `SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS` | `0` | `1`: minimize fullscreen on focus loss (SDL default) |

The same settings can be changed in game: the **Linux options** mod (Steam
Workshop) adds a Linux page to Options. It saves them to
`~/.klei/DoNotStarve/ds_linux_options`, read at the next start; launch options win.

With vsync a cap only helps at or above the refresh rate. Don't combine it with
MangoHud's `fps_limit`.

## GPU on Wayland

The game is 32-bit, and NVIDIA's EGL needs a 32-bit `libnvidia-egl-wayland` to
draw on Wayland, which most distributions don't ship. Without it the game
renders on the integrated GPU, also with `prime-run`. `DS_ZINK=1` (the same as
`MESA_LOADER_DRIVER_OVERRIDE=zink DRI_PRIME=1`) renders on the dedicated GPU
through Vulkan instead; it needs 32-bit Mesa and the 32-bit Vulkan driver
(e.g. `lib32-mesa` and `lib32-nvidia-utils` on Arch). On X11 (`DS_WAYLAND=0`)
`prime-run` uses NVIDIA's GLX as usual.

`GL_RENDERER` in `~/.klei/DoNotStarve/log.txt` shows the GPU.

## Notes

- **Game doesn't start:** it runs outside Steam's runtime; remove the launch
  options.
- **MangoHud config:** the process name is cut to 15 characters, so the
  per-game file is `~/.config/MangoHud/dontstarve_stea.conf`.
- **Steam overlay doesn't work on Wayland:** Steam draws it for OpenGL games
  only through GLX. Use `DS_WAYLAND=0` when you need the overlay.
- **After a game update** that changes the patched code, the fps part prints
  `unknown game build` and leaves the 60 fps cap; the rest keeps working.
- `ERROR: ld.so: object '$ORIGIN/shims/ds_fixes.so' … ignored` comes from
  Steam's helper processes and is harmless.

## How it works

The game links `libSDL2-2.0.so.0` from its `lib32/`. `ds_fixes.so` has that
soname, so it takes its place, and depends on the runtime's
`/usr/lib/i386-linux-gnu/sdl2-compat/libSDL2-2.0.so.0` by absolute path
(Steam rewrites `/usr` paths in `LD_PRELOAD` to the host, not inside files).

- `src/sdl.c` – the game was built against a pre-release SDL 2.0.0 without the
  `which` field in mouse events; events are rewritten to that layout, wheel
  events with `y == 0` (read by the game as "up") are dropped.
- `src/fps.c` – the two loads of the 1/60 s frame interval are pointed at our
  value; with a cap, the game's wait (oversleeps, busy-waits) is replaced by
  sleeping to absolute deadlines.
- `src/glx.c` – on Wayland the game's GLEW asks for the GLX version of a NULL
  display and then reads uninitialized values; it now gets 1.4.
- `src/config.c` – reads the Linux options mod's file at start and reports
  what the run uses to the mod. The game's Lua opens only relative paths, so
  the mod opens bare names and the library answers them.

`make` builds into `build/`, `make check` lists the glibc versions and
libraries needed (glibc 2.17 is enough), `make prebuilt` refreshes `prebuilt/`.

## License

MIT, see `LICENSE`. Not affiliated with Klei Entertainment.
