/* F13–F24. The engine turns SDL scancodes into its key codes (SDL 1.2 style)
 * with a uint16 table indexed by scancode - 4. F1–F12 give 282–293, but
 * F13–F24 (scancodes 0x68–0x73) give 0, so the game never sees them and they
 * can't be bound in Controls. Both the event handler and the Controls screen's
 * key naming read this one table, so filling those entries fixes both.
 *
 * F13–F15 get their SDL 1.2 codes 294–296, which the game already has names for.
 * F16–F24 have no SDL 1.2 code; they get the unused codes 323–331 (the game's
 * IsKeyDown takes 1–402; 400–402 are its combined Alt/Ctrl/Shift). The game's
 * Lua has no names for those, and a key without a name breaks the text of
 * prompts and Controls, so F16–F24 stay off until the "Linux options" mod,
 * which adds the names, asks for them (keys_enable_extra, see config.c). */
#include "common.h"
#include <link.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define FIRST_SCANCODE 4
#define SC_F13 0x68
#define SC_F16 0x6b
#define SC_F24 0x73
#define F13_CODE 294
#define F16_CODE 323

static uint16_t *table;
static int segment_prot;

/* a..z, 1..9, 0, Enter, Escape, Backspace, Tab, Space: scancodes 4..44 */
static const uint16_t start[] = {
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's',
    't', 'u', 'v', 'w', 'x', 'y', 'z', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 13, 27, 8, 9, 32,
};

static uint16_t *entry(int scancode) { return &table[scancode - FIRST_SCANCODE]; }

static int find_table(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    int *found = data;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD || (ph->p_flags & PF_W)) continue;
        const unsigned char *p = (const unsigned char *)(info->dlpi_addr + ph->p_vaddr);
        const unsigned char *end = p + ph->p_filesz;
        for (; p + 0xe4 * 2 <= end; p += 2) {
            if (memcmp(p, start, sizeof start) || ++*found > 1) continue;
            table = (uint16_t *)p;
            segment_prot = PROT_READ | (ph->p_flags & PF_X ? PROT_EXEC : 0);
        }
    }
    return 1;   /* the first object is the executable; stop there */
}

/* Fills scancodes first..last with codes from code on, if they are still unset. */
static void set_codes(int first, int last, int code) {
    uintptr_t page = (uintptr_t)sysconf(_SC_PAGESIZE);
    uintptr_t lo = (uintptr_t)entry(first) & ~(page - 1);
    uintptr_t hi = ((uintptr_t)(entry(last) + 1) + page - 1) & ~(page - 1);
    for (int sc = first; sc <= last; sc++)
        if (*entry(sc)) return;   /* another build, or done already */
    if (mprotect((void *)lo, hi - lo, segment_prot | PROT_WRITE)) {
        fprintf(stderr, TAG "cannot enable F%d-F%d\n", first - SC_F13 + 13, last - SC_F13 + 13);
        return;
    }
    for (int sc = first; sc <= last; sc++)
        *entry(sc) = (uint16_t)(code + sc - first);
    mprotect((void *)lo, hi - lo, segment_prot);
}

INTERNAL void keys_enable_extra(void) {
    static int done;
    if (table && !done) {
        done = 1;
        set_codes(SC_F16, SC_F24, F16_CODE);
    }
}

__attribute__((constructor))
static void init_keys(void) {
    if (!is_game()) return;
    int found = 0;
    dl_iterate_phdr(find_table, &found);
    /* exactly one, with F1 and F12 where expected: the table we know */
    if (found == 1 && *entry(0x3a) == 282 && *entry(0x45) == 293) {
        set_codes(SC_F13, SC_F16 - 1, F13_CODE);
    } else {
        table = NULL;
        fprintf(stderr, TAG "unknown game build, F13-F24 not enabled\n");
    }
}
