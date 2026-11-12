/* GLX version on Wayland. The game's built-in GLEW calls
 * glXQueryVersion(glXGetCurrentDisplay(), ...) without checking the result.
 * With an EGL context the display is NULL, the call fails and GLEW reads
 * uninitialized stack; major == 1 && minor < 2 would end in "Graphics
 * Initialization Error". A NULL display now reports GLX 1.4 and never reaches
 * the driver. */
#include "common.h"

typedef int (*query_fn)(void *, int *, int *);

int glXQueryVersion(void *dpy, int *major, int *minor) {
    if (!dpy) {
        if (major) *major = 1;
        if (minor) *minor = 4;
        return 0;
    }
    static query_fn real;
    if (!real) {
        void *gl = dlopen("libGL.so.1", RTLD_NOW | RTLD_NOLOAD);
        real = gl ? (query_fn)dlsym(gl, "glXQueryVersion") : NULL;
        if (!real) real = (query_fn)dlsym(RTLD_NEXT, "glXQueryVersion");
        if (!real) return 0;
    }
    return real(dpy, major, minor);
}
