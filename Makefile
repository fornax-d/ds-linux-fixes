# make           build/ds_fixes.so (needs a 32-bit C toolchain: gcc-multilib / lib32-glibc)
# make prebuilt  also copy it to prebuilt/
# make check     show the glibc versions and libraries it needs

CC     ?= cc
CFLAGS ?= -O2
RUNTIME_SDL ?= /usr/lib/i386-linux-gnu/sdl2-compat/libSDL2-2.0.so.0
SRC := src/common.c src/sdl.c src/fps.c src/glx.c
BUILD ?= build

all: $(BUILD)/ds_fixes.so

# The soname makes it stand in for the game's libSDL2; linking against an empty
# library whose soname is the runtime path records that path as a dependency.
$(BUILD)/ds_fixes.so: $(SRC) src/common.h $(BUILD)/runtime_sdl.so
	$(CC) -m32 $(CFLAGS) -Wall -Wextra -shared -fPIC -DRUNTIME_SDL='"$(RUNTIME_SDL)"' \
		-Wl,-soname,libSDL2-2.0.so.0 -o $@ $(SRC) \
		-Wl,--no-as-needed $(BUILD)/runtime_sdl.so -l:libdl.so.2

$(BUILD)/runtime_sdl.so:
	mkdir -p $(BUILD)
	printf '' | $(CC) -m32 -shared -nostdlib -Wl,-soname,$(RUNTIME_SDL) -o $@ -x c -

prebuilt: all
	mkdir -p prebuilt
	cp $(BUILD)/ds_fixes.so prebuilt/

check: all
	@printf 'glibc: '; objdump -T $(BUILD)/ds_fixes.so | grep -o 'GLIBC_[0-9.]*' | sort -uV | tr '\n' ' '; echo
	@readelf -dW $(BUILD)/ds_fixes.so | grep -E 'NEEDED|SONAME'

clean:
	rm -rf build

.PHONY: all prebuilt check clean
