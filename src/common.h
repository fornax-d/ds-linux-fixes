#pragma once
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "[ds_fixes] "
/* 32-bit sdl2-compat inside the Steam Linux Runtime container */
#ifndef RUNTIME_SDL
#define RUNTIME_SDL "/usr/lib/i386-linux-gnu/sdl2-compat/libSDL2-2.0.so.0"
#endif

/* Oldest symbol versions, so the library loads on any i386 glibc. */
__asm__(".symver dlopen,dlopen@GLIBC_2.1");
__asm__(".symver dlsym,dlsym@GLIBC_2.0");
__asm__(".symver dlerror,dlerror@GLIBC_2.0");

#define INTERNAL __attribute__((visibility("hidden")))

INTERNAL int is_game(void);                 /* the game itself, not a helper process */
INTERNAL int env_int(const char *name, int def);
INTERNAL void *sdl_sym(const char *name);   /* the runtime SDL's function; aborts if missing */
