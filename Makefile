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
DASH = dist/.weather-view
DASH_SRC = src/dashboard.c src/dashboard_render.c src/dashboard_q2.c src/art.c src/weather.c src/crt-q2.c
DASH_OBJ = $(patsubst src/%.c,build/dashboard/%.o,$(DASH_SRC))
SPRITES = $(addprefix build/icons/,sunny.jpg partly.jpg cloudy.jpg overcast.jpg fog.jpg drizzle.jpg rain.jpg showers.jpg thunder.jpg snow.jpg snowshowers.jpg night.jpg nightcloud.jpg wind.jpg humidity.jpg icon.png icon-40.png)

all: dist/q2-weather.zip

$(SYSROOT)/usr/include/errno.h:
	@echo 'Q2 sysroot not initialized. Run: sh tools/sysroot.sh' >&2
	@exit 1

build/q2/%.o: src/%.c src/weather.h src/http.h | $(SYSROOT)/usr/include/errno.h
	mkdir -p $(@D)
	$(Q2_CC) $(Q2_FLAGS) $(Q2_CFLAGS) -c $< -o $@

build/weather_sprites.inc: tools/draw_weather_icons.py
	python3 tools/draw_weather_icons.py

build/dashboard/render.stamp: build/weather_sprites.inc
	@mkdir -p $(@D)
	@touch $@

build/dashboard/dashboard_render.o: build/weather_sprites.inc
build/dashboard/%.o: src/%.c src/dashboard.h src/weather.h src/art.h | $(SYSROOT)/usr/include/errno.h
	mkdir -p $(@D)
	$(Q2_CC) $(Q2_FLAGS) $(Q2_CFLAGS) -Ibuild -c $< -o $@

$(DASH)/q2-weather-view: $(DASH_OBJ)
	mkdir -p $(@D)
	$(Q2_LD) $(Q2_LDFLAGS) -o $@ $^ $(SYSROOT)/lib/libc.so.6 \
		$(SYSROOT)/lib/libm.so.6 $(SYSROOT)/lib/libpthread.so.0 \
		$(SYSROOT)/lib/libgcc_s.so.1 $(SYSROOT)/lib/ld-linux-mipsn8.so.1

$(DASH)/q2app: .weather-dashboard-q2app
	mkdir -p $(@D)
	cp $< $@

$(DASH)/icon.png: build/icons/icon.png
	mkdir -p $(@D)
	cp $< $@

$(DIST)/q2-weather: $(Q2_OBJ)
	mkdir -p $(@D)
	$(Q2_LD) $(Q2_LDFLAGS) -o $@ $^ $(SYSROOT)/lib/libc.so.6 \
		$(SYSROOT)/lib/libm.so.6 $(SYSROOT)/lib/libpthread.so.0 \
		$(SYSROOT)/lib/libgcc_s.so.1 $(SYSROOT)/lib/libcurl.so.4 \
		$(SYSROOT)/lib/ld-linux-mipsn8.so.1

build/icons/sunny.jpg: tools/draw_weather_icons.py
	python3 tools/draw_weather_icons.py

$(filter-out build/icons/sunny.jpg,$(SPRITES)): build/icons/sunny.jpg
	@test -f $@

$(DIST)/icons.stamp: $(SPRITES)
	mkdir -p $(DIST)/icons
	cp build/icons/*.jpg $(DIST)/icons/
	cp build/icons/icon.png build/icons/icon-40.png $(DIST)/
	@touch $@

$(DIST)/q2app: q2app
	mkdir -p $(@D)
	cp $< $@

$(DIST)/run: run
	mkdir -p $(@D)
	cp $< $@
	chmod 755 $@

dist/q2-weather.zip: $(DIST)/q2-weather $(DIST)/q2app $(DIST)/run $(DIST)/icons.stamp $(DASH)/q2-weather-view $(DASH)/q2app $(DASH)/icon.png
	cd dist && zip -qX q2-weather.zip .weather/q2-weather .weather/q2app .weather/run .weather/icon.png .weather/icon-40.png .weather/icons/*.jpg .weather-view/q2-weather-view .weather-view/q2app .weather-view/icon.png

build/test_weather: src/weather.c src/weather.h tests/test_weather.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/weather.c tests/test_weather.c -lm

build/host/q2-weather: src/main.c src/http.c src/weather.c src/art.c src/weather.h src/http.h src/art.h
	mkdir -p build/host
	$(CC) -std=gnu11 -DHOST -O2 -Wall -Wextra -Werror -o $@ \
		src/main.c src/http.c src/weather.c src/art.c -lcurl -lm

build/test_http: src/http.c src/http.h tests/test_http.c
	mkdir -p build
	$(CC) -std=gnu11 -DHOST -O2 -Wall -Wextra -Werror -o $@ src/http.c tests/test_http.c -lm

build/test_art: src/art.c src/art.h tests/test_art.c
	mkdir -p build
	$(CC) -std=gnu11 -DHOST -O2 -Wall -Wextra -Werror -o $@ src/art.c tests/test_art.c

build/test_dashboard: tests/test_dashboard.c src/dashboard.c src/dashboard.h src/weather.h
	mkdir -p build
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -o $@ tests/test_dashboard.c src/dashboard.c

host: build/host/q2-weather

check: build/test_weather build/test_http build/test_art build/test_dashboard build/host/q2-weather
	./build/test_weather
	./build/test_http
	./build/test_art
	./build/test_dashboard
	sh -n run

format:
	clang-format -i src/*.c src/*.h tests/*.c

format-check:
	clang-format --dry-run --Werror src/*.c src/*.h tests/*.c

clean:
	# Preserve build/sysroot and build/q2-rootfs.squashfs: these are expensive to recreate.
	rm -rf build/q2 build/dashboard build/host build/weather_sprites.inc build/test_weather build/test_http build/test_art build/test_dashboard dist

clean-all:
	rm -rf build dist

.PHONY: all check format format-check clean clean-all host
