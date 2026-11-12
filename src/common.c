#include "common.h"
#include <errno.h>
#include <string.h>

int is_game(void) {
    return strcmp(program_invocation_short_name, "dontstarve_steam") == 0;
}

/* Not atoi/strtol: glibc >= 2.38 headers redirect those to a new symbol version. */
int env_int(const char *name, int def) {
    const char *s = getenv(name);
    if (!s || !*s) return def;
    int neg = *s == '-', v = 0;
    if (neg || *s == '+') s++;
    if (!*s) return def;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || v > 100000000) return def;
        v = v * 10 + (*s - '0');
    }
    return neg ? -v : v;
}

/* Through an explicit handle: dlsym(RTLD_NEXT) breaks when an overlay such as
 * MangoHud interposes dlsym, and dlopen("libSDL2-2.0.so.0") would return us. */
void *sdl_sym(const char *name) {
    void *sdl = dlopen(RUNTIME_SDL, RTLD_NOW | RTLD_NOLOAD);
    void *sym = sdl ? dlsym(sdl, name) : NULL;
    if (!sym) sym = dlsym(RTLD_NEXT, name);
    if (!sym) {
        fprintf(stderr, TAG "cannot resolve %s\n", name);
        abort();
    }
    return sym;
}
