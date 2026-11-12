/* Native Wayland: the game runs on the runtime's SDL instead of its own X11-only
 * SDL 2.0.0, which needs its old mouse event layout. */
#include "common.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct SDL_Window SDL_Window;

/* Modern SDL2 events (stable ABI since 2.0.4, also sdl2-compat) */
enum { SDL_MOUSEMOTION = 0x400, SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP, SDL_MOUSEWHEEL };
typedef struct { uint32_t type, timestamp, window, which, state; int32_t x, y, xrel, yrel; } Motion;
typedef struct { uint32_t type, timestamp, window, which; uint8_t button, state, clicks, pad; int32_t x, y; } Button;
typedef struct { uint32_t type, timestamp, window, which; int32_t x, y; uint32_t direction; } Wheel;
typedef union { uint32_t type; Motion motion; Button button; Wheel wheel; uint8_t size[56]; } SDL_Event;

_Static_assert(sizeof(SDL_Event) == 56, "SDL_Event");
_Static_assert(offsetof(Motion, x) == 0x14 && offsetof(Motion, yrel) == 0x20, "motion");
_Static_assert(offsetof(Button, button) == 0x10 && offsetof(Button, x) == 0x14, "button");
_Static_assert(offsetof(Wheel, y) == 0x14, "wheel");

/* The game's SDL 2.0.0 snapshot has no `which`. Its input handler (0x8188600,
 * build 12527201) reads motion x/y @0x10/0x14, button @0x0c, wheel y @0x10. */
typedef struct { uint32_t type, timestamp, window, state; int32_t x, y, xrel, yrel; } OldMotion;
typedef struct { uint32_t type, timestamp, window; uint8_t button, state, pad[2]; int32_t x, y; } OldButton;
typedef struct { uint32_t type, timestamp, window; int32_t x, y; } OldWheel;

#define REAL(name, ret, ...) \
    static ret (*real_##name)(__VA_ARGS__); \
    if (!real_##name) real_##name = (ret (*)(__VA_ARGS__))sdl_sym(#name)

/* Rewrites e in place; 0 = drop the event. */
static int to_game_layout(SDL_Event *e) {
    if (e->type == SDL_MOUSEMOTION) {
        Motion m = e->motion;
        *(OldMotion *)e = (OldMotion){ m.type, m.timestamp, m.window, m.state, m.x, m.y, m.xrel, m.yrel };
    } else if (e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP) {
        Button b = e->button;
        *(OldButton *)e = (OldButton){ b.type, b.timestamp, b.window, b.button, b.state, {0}, b.x, b.y };
    } else if (e->type == SDL_MOUSEWHEEL) {
        Wheel w = e->wheel;
        /* The game takes the sign of y as the direction, so y == 0 would zoom in.
         * SDL sends those for horizontal scrolling and partial touchpad steps. */
        if (w.y == 0) return 0;
        *(OldWheel *)e = (OldWheel){ w.type, w.timestamp, w.window, w.x, w.y };
    }
    return 1;
}

/* The game reads events only through SDL_PollEvent, into a 56-byte buffer. */
int SDL_PollEvent(SDL_Event *e) {
    REAL(SDL_PollEvent, int, SDL_Event *);
    int ret;
    while ((ret = real_SDL_PollEvent(e)) && e && !to_game_layout(e))
        ;
    return ret;
}

/* Keep libEGL loaded: its destructor at exit can free GL state that
 * libGLdispatch still uses afterwards (crashed Don't Starve Together on exit). */
SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, uint32_t flags) {
    REAL(SDL_CreateWindow, SDL_Window *, const char *, int, int, int, int, uint32_t);
    if (!dlopen("libEGL.so.1", RTLD_NOW | RTLD_NODELETE))
        fprintf(stderr, TAG "cannot keep libEGL loaded: %s\n", dlerror());
    return real_SDL_CreateWindow(title, x, y, w, h, flags);
}

/* The game always requests vsync; DS_SWAP_INTERVAL overrides it. */
int SDL_GL_SetSwapInterval(int interval) {
    REAL(SDL_GL_SetSwapInterval, int, int);
    return real_SDL_GL_SetSwapInterval(env_int("DS_SWAP_INTERVAL", interval));
}

/* Set before SDL starts; an existing value always wins. */
__attribute__((constructor))
static void set_sdl_env(void) {
    /* sdl2-compat defaults to X11; DS_WAYLAND: 1 always, 0 never, unset = in a Wayland session. */
    int wayland = env_int("DS_WAYLAND", getenv("WAYLAND_DISPLAY") != NULL);
    if (wayland) setenv("SDL_VIDEODRIVER", "wayland", 0);
    /* No 32-bit NVIDIA EGL for Wayland: DS_ZINK=1 renders on the NVIDIA GPU through Zink. */
    if (env_int("DS_ZINK", 0)) {
        setenv("MESA_LOADER_DRIVER_OVERRIDE", "zink", 0);
        setenv("DRI_PRIME", "1", 0);
    }
    /* Fullscreen stays up on alt-tab / a second monitor. */
    setenv("SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS", "0", 0);
}
