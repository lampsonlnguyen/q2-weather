SYSROOT ?= build/sysroot
CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -pedantic
Q2_CC ?= clang
Q2_LD ?= ld.lld
Q2_FLAGS = -D_GNU_SOURCE --target=mipsel-linux-gnu --sysroot=$(SYSROOT) \
	-march=mips32r2 -mabi=32 -mfp64 -mnan=2008 -mabs=2008
Q2_CFLAGS = -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -fno-strict-aliasing
Q2_LDFLAGS = -m elf32ltsmip -e __start --dynamic-linker /lib/ld-linux-mipsn8.so.1 \
	--image-base=0x400000 -z noexecstack --hash-style=sysv --build-id=none -s
Q2_SRC = src/main.c src/http.c src/weather.c src/art.c src/crt-q2.c
Q2_OBJ = $(patsubst src/%.c,build/q2/%.o,$(Q2_SRC))
DIST = dist/.weather
# The condition covers are drawn at build time (tools/draw_icons.py, Pillow).
ICON_NAMES = cloudy drizzle fog overcast partly rain showers snow snowshowers sunny thunder
DIST_ICONS = $(patsubst %,$(DIST)/icons/%.jpg,$(ICON_NAMES))

# The Q2 glibc runtime places libcurl in /usr/lib; copied sysroots may
# instead have it in /lib or omit it entirely. Extract it when needed.
Q2_CURL = $(SYSROOT)/lib/libcurl.so.4
Q2_ROOTFS ?= build/q2-rootfs.squashfs
# The host build declares the curl calls it makes itself, so any libcurl.so links.
HOST_CURL ?= -lcurl

all: dist/q2-weather.zip

$(SYSROOT)/usr/include/errno.h:
	@echo 'Q2 sysroot not initialized. Run: sh tools/sysroot.sh' >&2
	@exit 1

build/q2/%.o: src/%.c src/weather.h src/http.h | $(SYSROOT)/usr/include/errno.h
	mkdir -p $(@D)
	$(Q2_CC) $(Q2_FLAGS) $(Q2_CFLAGS) -c $< -o $@

$(Q2_CURL):
	sh tools/repair-libcurl.sh "$(SYSROOT)" "$(Q2_ROOTFS)"

$(DIST)/q2-weather: $(Q2_OBJ) $(Q2_CURL)
	mkdir -p $(@D)
	$(Q2_LD) $(Q2_LDFLAGS) -o $@ $(Q2_OBJ) $(SYSROOT)/lib/libc.so.6 \
		$(SYSROOT)/lib/libm.so.6 $(SYSROOT)/lib/libpthread.so.0 \
		$(SYSROOT)/lib/libgcc_s.so.1 $(Q2_CURL) \
		$(SYSROOT)/lib/ld-linux-mipsn8.so.1

$(DIST)/q2app: q2app
	mkdir -p $(@D)
	cp $< $@

$(DIST)/run: run
	mkdir -p $(@D)
	cp $< $@
	chmod 755 $@

$(DIST)/icon.png: assets/icon.png
	mkdir -p $(@D)
	cp $< $@

build/icons/.stamp: tools/draw_icons.py
	python3 tools/draw_icons.py build/icons
	touch $@

$(DIST)/icons/%.jpg: build/icons/.stamp
	mkdir -p $(@D)
	cp build/icons/$*.jpg $@

dist/q2-weather.zip: $(DIST)/q2-weather $(DIST)/run $(DIST)/q2app $(DIST)/icon.png $(DIST_ICONS)
	cd dist && rm -f q2-weather.zip && zip -qX q2-weather.zip .weather/q2-weather .weather/run \
		.weather/q2app .weather/icon.png .weather/icons/*.jpg

build/test_weather: src/weather.c src/weather.h tests/test_weather.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/weather.c tests/test_weather.c -lm

build/host/q2-weather: src/main.c src/http.c src/weather.c src/art.c src/weather.h src/http.h src/art.h
	mkdir -p build/host
	$(CC) -std=gnu11 -DHOST -O2 -Wall -Wextra -Werror -o $@ \
		src/main.c src/http.c src/weather.c src/art.c $(HOST_CURL) -lm -lpthread

build/test_http: src/http.c src/http.h tests/test_http.c
	mkdir -p build
	$(CC) -std=gnu11 -DHOST -O2 -Wall -Wextra -Werror -o $@ src/http.c tests/test_http.c -lm

build/test_art: src/art.c src/art.h tests/test_art.c
	mkdir -p build
	$(CC) -std=gnu11 -DHOST -O2 -Wall -Wextra -Werror -o $@ src/art.c tests/test_art.c

host: build/host/q2-weather

check: build/test_weather build/test_http build/test_art build/host/q2-weather
	sh -n run
	./build/test_weather
	./build/test_http
	./build/test_art
	python3 -I tests/test_pages.py build/host/q2-weather
	@echo 'Weather card app checks passed'

icons: build/icons/.stamp

format:
	clang-format -i src/*.c src/*.h tests/*.c

format-check:
	clang-format --dry-run --Werror src/*.c src/*.h tests/*.c

clean:
	# Preserve build/sysroot and build/q2-rootfs.squashfs: these are expensive to recreate.
	rm -rf build/q2 build/host build/icons build/test_weather build/test_http build/test_art dist

clean-all:
	rm -rf build dist

.PHONY: all check format format-check clean clean-all host icons
