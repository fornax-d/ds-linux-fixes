/* Frame limit. DS_FPS_LIMIT: unset/0 = none, N = N fps, game = keep the game's 60. */
#include "common.h"
#include <errno.h>
#include <link.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define MAX_SITES 8

/* The main loop (0x8053a20, build 12527201) reloads its frame interval
 * (1/60 s, 1/30 in netbook mode) before each Timer::SetInterval call:
 *   mov eax,[esi+0xcc] ; ...stores... ; call SetInterval
 * Each such load is pointed at our own float instead. */
static const unsigned char set_interval[] = {   /* mov edx,[esp+8]; mov eax,[esp+4]; mov [eax+8],edx; ret */
    0x8b, 0x54, 0x24, 0x08, 0x8b, 0x44, 0x24, 0x04, 0x89, 0x50, 0x08, 0xc3 };
static const unsigned char interval_load[] = { 0x8b, 0x86, 0xcc, 0x00, 0x00, 0x00 };
static float interval;

static const unsigned char *rel32_target(const unsigned char *call) {
    int32_t d;
    memcpy(&d, call + 1, 4);
    return call + 5 + d;
}

static int in_text(const void *p, size_t len, uintptr_t text, uintptr_t text_end) {
    return (uintptr_t)p >= text && (uintptr_t)p + len <= text_end;
}

/* The call to SetInterval that follows the load within a few instructions. */
static const unsigned char *following_call(const unsigned char *p, uintptr_t text, uintptr_t text_end) {
    for (const unsigned char *c = p + 6; c < p + 32 && in_text(c, 5, text, text_end); c++) {
        if (*c != 0xe8) continue;
        const unsigned char *t = rel32_target(c);
        if (in_text(t, sizeof set_interval, text, text_end) && !memcmp(t, set_interval, sizeof set_interval))
            return c;
    }
    return NULL;
}

static int make_writable(void *addr, size_t len, int writable) {
    uintptr_t page = (uintptr_t)sysconf(_SC_PAGESIZE);
    uintptr_t lo = (uintptr_t)addr & ~(page - 1);
    uintptr_t hi = ((uintptr_t)addr + len + page - 1) & ~(page - 1);
    return mprotect((void *)lo, hi - lo, PROT_READ | PROT_EXEC | (writable ? PROT_WRITE : 0));
}

/* The game's wait (0x81f7950) sleeps the whole remainder and the timer is
 * restarted after it wakes, so oversleep adds up (144 -> ~142 fps), and it
 * busy-waits the last 2 ms. This one sleeps to absolute deadlines instead. */
static int64_t period_ns, deadline_ns;

__attribute__((force_align_arg_pointer))   /* the game only keeps 4-byte stack alignment */
static void pace_frame(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t now = ts.tv_sec * 1000000000LL + ts.tv_nsec, next = deadline_ns + period_ns;
    if (next <= now) { deadline_ns = now; return; }   /* late: restart the grid, don't catch up */
    deadline_ns = next;
    ts = (struct timespec){ next / 1000000000, next % 1000000000 };
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR)
        ;
}

/* The main loop calls the wait right before the second load; the wait has no
 * side effects and its result is unused. That call now goes to pace_frame
 * (cdecl, the game's arguments are ignored). */
static int redirect_wait(unsigned char **sites, int n, uintptr_t text, uintptr_t text_end) {
    unsigned char *call = NULL;
    for (int i = 0; i < n; i++) {
        unsigned char *c = sites[i] - 5;
        if (!in_text(c, 5, text, text_end) || c[0] != 0xe8) continue;
        const unsigned char *t = rel32_target(c);
        /* exactly one such call, to a function starting push esi; push ebx; sub esp,imm8 */
        if (call || !in_text(t, 4, text, text_end) || memcmp(t, "\x56\x53\x83\xec", 4)) return 0;
        call = c;
    }
    if (!call) return 0;
    int32_t rel = (int32_t)((uintptr_t)pace_frame - (uintptr_t)(call + 5));   /* wraps: reaches anywhere */
    if (make_writable(call, 5, 1)) return 0;
    memcpy(call + 1, &rel, 4);
    make_writable(call, 5, 0);
    return 1;
}

static int patch(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size; (void)data;
    uintptr_t base = info->dlpi_addr, text = 0, text_end = 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type == PT_LOAD && (ph->p_flags & PF_X) && !text)
            text = base + ph->p_vaddr, text_end = text + ph->p_memsz;
    }

    unsigned char *sites[MAX_SITES];
    const unsigned char *target = NULL;
    int n = 0;
    for (unsigned char *p = (unsigned char *)text; text && p + sizeof interval_load <= (unsigned char *)text_end; p++) {
        if (memcmp(p, interval_load, sizeof interval_load)) continue;
        const unsigned char *c = following_call(p, text, text_end);
        if (!c) continue;
        if (n == MAX_SITES || (target && rel32_target(c) != target)) { n = 0; break; }
        target = rel32_target(c);
        sites[n++] = p;
    }
    if (n != 2) {
        fprintf(stderr, TAG "unknown game build, fps limit not changed\n");
        return 1;
    }

    const char *env = getenv("DS_FPS_LIMIT");
    int limit = env && *env ? env_int("DS_FPS_LIMIT", -1) : 0;
    if (limit < 0) {
        fprintf(stderr, TAG "DS_FPS_LIMIT=%s is not a number >= 0 or \"game\", using 0\n", env);
        limit = 0;
    }
    interval = limit ? 1.0f / limit : 0.0f;   /* 0: the game's wait returns at once */

    float *slot = &interval;
    for (int i = 0; i < n; i++) {
        if (make_writable(sites[i], 6, 1)) return 1;
        sites[i][0] = 0xa1;   /* mov eax,[slot] ; nop */
        memcpy(sites[i] + 1, &slot, 4);
        sites[i][5] = 0x90;
        make_writable(sites[i], 6, 0);
    }
    if (limit) {
        period_ns = 1000000000 / limit;
        int paced = redirect_wait(sites, n, text, text_end);
        fprintf(stderr, TAG "fps limit %d%s\n", limit, paced ? "" : " (game's own wait)");
    }
    return 1;   /* the first object is the executable; stop there */
}

__attribute__((constructor))
static void init_fps(void) {
    const char *env = getenv("DS_FPS_LIMIT");
    if (is_game() && !(env && strcmp(env, "game") == 0))
        dl_iterate_phdr(patch, NULL);
}
