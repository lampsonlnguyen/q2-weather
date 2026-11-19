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
Q2_SRC = src/main.c src/http.c src/weather.c src/crt-q2.c
Q2_OBJ = $(patsubst src/%.c,build/q2/%.o,$(Q2_SRC))
DIST = dist/.weather

all: dist/q2-weather.zip

$(SYSROOT)/usr/include/errno.h:
	@echo 'Q2 sysroot not initialized. Run: sh tools/sysroot.sh' >&2
	@exit 1

build/q2/%.o: src/%.c src/weather.h src/http.h | $(SYSROOT)/usr/include/errno.h
	mkdir -p $(@D)
	$(Q2_CC) $(Q2_FLAGS) $(Q2_CFLAGS) -c $< -o $@

$(DIST)/q2-weather: $(Q2_OBJ)
	mkdir -p $(@D)
	$(Q2_LD) $(Q2_LDFLAGS) -o $@ $^ $(SYSROOT)/lib/libc.so.6 \
		$(SYSROOT)/lib/libm.so.6 $(SYSROOT)/lib/libpthread.so.0 \
		$(SYSROOT)/lib/libgcc_s.so.1 $(SYSROOT)/lib/libcurl.so.4 \
		$(SYSROOT)/lib/ld-linux-mipsn8.so.1

$(DIST)/q2app: q2app
	mkdir -p $(@D)
	cp $< $@

$(DIST)/run: run
	mkdir -p $(@D)
	cp $< $@
	chmod 755 $@

dist/q2-weather.zip: $(DIST)/q2-weather $(DIST)/q2app $(DIST)/run
	cd dist && zip -qX q2-weather.zip .weather/q2-weather .weather/q2app .weather/run

build/test_weather: src/weather.c src/weather.h tests/test_weather.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/weather.c tests/test_weather.c -lm

build/host/q2-weather: src/main.c src/http.c src/weather.c src/weather.h src/http.h
	mkdir -p build/host
	$(CC) -std=gnu11 -DHOST -O2 -Wall -Wextra -Werror -o $@ \
		src/main.c src/http.c src/weather.c -lcurl -lm

host: build/host/q2-weather

check: build/test_weather build/host/q2-weather
	./build/test_weather
	sh -n run

format:
	clang-format -i src/*.c src/*.h tests/*.c

format-check:
	clang-format --dry-run --Werror src/*.c src/*.h tests/*.c

clean:
	rm -rf build dist

.PHONY: all check format format-check clean host
