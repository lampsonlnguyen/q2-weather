CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -pedantic
.PHONY: check clean
check: build/test_weather
	./build/test_weather
build/test_weather: src/weather.c src/weather.h tests/test_weather.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/weather.c tests/test_weather.c -lm
clean:
	rm -rf build
