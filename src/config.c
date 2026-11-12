/* Options from the in-game "Linux" options page (the "Linux options" mod).
 *
 * The mod saves  ~/.klei/DoNotStarve/ds_linux_options  (KEY=VALUE lines).
 * At start they become environment variables, before the other constructors
 * read them; variables already set (Steam launch options) win.
 *
 * The game's io.open only takes relative paths (at most one ".." up), so the
 * mod opens bare names and we answer them:
 *   "ds_linux_options"  -> the file above
 *   "ds_linux_status"   -> what this run uses, from memory
 * Without us both fail, so the page knows the library isn't loaded. */
#include "common.h"
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define OPTIONS_FILE "ds_linux_options"
#define STATUS_FILE "ds_linux_status"

static const char *const keys[] = {
    "DS_WAYLAND", "DS_ZINK", "DS_FPS_LIMIT", "DS_SWAP_INTERVAL", "SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS",
};
#define NKEYS (int)(sizeof keys / sizeof *keys)

static char status[2048], options_path[1024];
static int status_len;

static int known_key(const char *k, size_t n) {
    for (int i = 0; i < NKEYS; i++)
        if (strlen(keys[i]) == n && !memcmp(keys[i], k, n)) return 1;
    return 0;
}

/* KEY=VALUE lines; unknown keys and odd values are ignored. */
static void apply_options(const char *path) {
    char buf[4096];
    FILE *f = fopen(path, "r");
    if (!f) return;
    size_t len = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[len] = 0;
    char *p = buf, *line;
    while ((line = strsep(&p, "\n"))) {
        char *eq = strchr(line, '='), *v = eq + 1;
        if (!eq || !known_key(line, (size_t)(eq - line))) continue;
        size_t vn = strspn(v, "abcdefghijklmnopqrstuvwxyz0123456789");
        if (!vn || (v[vn] != 0 && v[vn] != '\r')) continue;
        v[vn] = *eq = 0;
        setenv(line, v, 0);
    }
}

static void add(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(status + status_len, sizeof status - status_len, fmt, ap);
    va_end(ap);
    if (n > 0 && status_len + n < (int)sizeof status) status_len += n;
}

/* Runs before the default-priority constructors in sdl.c, fps.c. */
__attribute__((constructor(101)))
static void load_config(void) {
    const char *home = getenv("HOME");
    char *path = options_path;
    if (!is_game() || !home ||
        snprintf(path, sizeof options_path, "%s/.klei/DoNotStarve/" OPTIONS_FILE, home) >= (int)sizeof options_path) {
        path[0] = 0;
        return;
    }

    char from_env[NKEYS];
    for (int i = 0; i < NKEYS; i++) from_env[i] = getenv(keys[i]) != NULL;
    from_env[1] |= getenv("MESA_LOADER_DRIVER_OVERRIDE") != NULL;   /* DS_ZINK set by hand */
    apply_options(path);

    /* same rules as set_sdl_env() in sdl.c */
    const char *drv = getenv("SDL_VIDEODRIVER");
    int wayland = drv ? !strcmp(drv, "wayland") : env_int("DS_WAYLAND", getenv("WAYLAND_DISPLAY") != NULL) != 0;
    const char *mesa = getenv("MESA_LOADER_DRIVER_OVERRIDE");
    int zink = env_int("DS_ZINK", 0) || (mesa && !strcmp(mesa, "zink"));

    add("loaded=1\ndisplay=%s\nzink=%d\n", wayland ? "wayland" : "x11", zink);
    for (int i = 0; i < NKEYS; i++) {
        const char *v = getenv(keys[i]);
        add("%s=%s\n", keys[i], v ? v : "");
    }
    add("env=");
    for (int i = 0, first = 1; i < NKEYS; i++)
        if (from_env[i]) add(first ? "%s" : ",%s", keys[i]), first = 0;
    add("\n");
}

/* Last path component; the game's file layer may prefix the name. */
static int named(const char *path, const char *name) {
    const char *slash = path ? strrchr(path, '/') : NULL;
    return path && !strcmp(slash ? slash + 1 : path, name);
}

static int status_fd(void) {
    int fd = (int)syscall(SYS_memfd_create, STATUS_FILE, 0);
    if (fd < 0) return -1;
    if (write(fd, status, status_len) != status_len || lseek(fd, 0, SEEK_SET) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void *libc_sym(const char *name) {
    void *libc = dlopen("libc.so.6", RTLD_NOW | RTLD_NOLOAD);
    void *sym = libc ? dlsym(libc, name) : NULL;
    return sym ? sym : dlsym(RTLD_NEXT, name);
}

/* Lua's io.open ends in one of these. */
FILE *fopen(const char *path, const char *mode) {
    static FILE *(*real)(const char *, const char *);
    if (!real) real = (FILE *(*)(const char *, const char *))libc_sym("fopen");
    if (options_path[0] && named(path, OPTIONS_FILE))
        return real(options_path, mode);
    if (status_len && named(path, STATUS_FILE) && mode[0] == 'r' && !strchr(mode, '+')) {
        int fd = status_fd();
        FILE *f = fd >= 0 ? fdopen(fd, "r") : NULL;
        if (f) return f;
        if (fd >= 0) close(fd);
    }
    return real(path, mode);
}

int open(const char *path, int flags, ...) {
    static int (*real)(const char *, int, ...);
    if (!real) real = (int (*)(const char *, int, ...))libc_sym("open");
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }
    if (options_path[0] && named(path, OPTIONS_FILE))
        return real(options_path, flags, mode);
    if (status_len && named(path, STATUS_FILE) && (flags & O_ACCMODE) == O_RDONLY) {
        int fd = status_fd();
        if (fd >= 0) return fd;
    }
    return real(path, flags, mode);
}
