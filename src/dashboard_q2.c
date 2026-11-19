/* SPDX-License-Identifier: MIT */
/* Full-screen graphical Library app. Its music/player audio remains untouched. */
#include "dashboard.h"
#include "weather.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define INPUT_SOCKET "/tmp/q2-weather-view.input.sock"
#define BROWSE_SOCKET "/tmp/q2-weather.browse.sock"
#define STATE_PATH "/mnt/mmc/.weather/state"
#define SERVICE_PATH "/mnt/mmc/.weather/q2-weather"
#define REFRESH_MS 1000

static volatile sig_atomic_t interrupted;

static void stop(int signal_code) {
    (void)signal_code;
    interrupted = 1;
}

static unsigned long now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return 0;
    return (unsigned long)ts.tv_sec * 1000 + (unsigned long)ts.tv_nsec / 1000000;
}

static int connected_service(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", BROWSE_SOCKET);
    int ok = !connect(fd, (struct sockaddr *)&addr, sizeof addr);
    close(fd);
    return ok;
}

static pid_t start_service(void) {
    if (connected_service()) return 0;
    pid_t pid = fork();
    if (pid == 0) {
        execl(SERVICE_PATH, "q2-weather", (char *)NULL);
        _exit(127);
    }
    return pid > 0 ? pid : -1;
}

static void request_refresh(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return;
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", BROWSE_SOCKET);
    if (!connect(fd, (struct sockaddr *)&addr, sizeof addr)) {
        const char message[] = "Q2SPOT/1\n1\trefresh\t\n";
        (void)send(fd, message, sizeof message - 1, MSG_NOSIGNAL);
        shutdown(fd, SHUT_WR);
    }
    close(fd);
}

static int read_cache(Dashboard *ui) {
    char text[4096];
    FILE *file = fopen(STATE_PATH, "rb");
    if (!file) return 0;
    size_t len = fread(text, 1, sizeof text - 1, file);
    fclose(file);
    text[len] = 0;
    WeatherApp next;
    weather_init(&next);
    if (weather_read(&next, text)) return 0;
    if (memcmp(&ui->weather, &next, sizeof next) == 0) return 0;
    ui->weather = next;
    ui->redraw = 1;
    return 1;
}

static int open_input(void) {
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof address.sun_path, "%s", INPUT_SOCKET);
    unlink(INPUT_SOCKET);
    if (bind(fd, (struct sockaddr *)&address, sizeof address)) {
        close(fd);
        return -1;
    }
    return fd;
}

int main(void) {
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    signal(SIGPIPE, SIG_IGN);
    Dashboard ui;
    dashboard_init(&ui);
    (void)read_cache(&ui);
    pid_t service = start_service();
    int fb = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fb < 0) {
        if (service > 0) kill(service, SIGTERM);
        return 1;
    }
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    if (ioctl(fb, FBIOGET_VSCREENINFO, &var) ||
        ioctl(fb, FBIOGET_FSCREENINFO, &fix)) {
        close(fb);
        if (service > 0) kill(service, SIGTERM);
        return 1;
    }
    unsigned stride = fix.line_length / sizeof(uint32_t);
    unsigned front = var.yoffset;
    unsigned rows = fix.line_length ? fix.smem_len / fix.line_length : 0;
    if (var.xres != DASH_HEIGHT || var.yres != DASH_WIDTH ||
        var.bits_per_pixel != 32 || fix.line_length % sizeof(uint32_t) ||
        stride < DASH_HEIGHT || rows < DASH_WIDTH || front > rows - DASH_WIDTH) {
        close(fb);
        if (service > 0) kill(service, SIGTERM);
        return 1;
    }
    void *mapped = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    if (mapped == MAP_FAILED) {
        close(fb);
        if (service > 0) kill(service, SIGTERM);
        return 1;
    }
    uint32_t *pixels = mapped;
    int input = open_input();
    if (input < 0) {
        munmap(mapped, fix.smem_len);
        close(fb);
        if (service > 0) kill(service, SIGTERM);
        return 1;
    }
    int pan = rows >= 2 * DASH_WIDTH && (front == 0 || front == DASH_WIDTH);
    unsigned shown = front;
    unsigned long last_poll = 0;
    while (!interrupted && !ui.quit) {
        unsigned long now = now_ms();
        if (now - last_poll >= REFRESH_MS) {
            (void)read_cache(&ui);
            last_poll = now;
        }
        if (ui.redraw) {
            unsigned target = pan ? (shown ? 0 : DASH_WIDTH) : shown;
            dashboard_render(&ui, pixels + target * stride, stride);
            if (pan) {
                var.yoffset = target;
                if (!ioctl(fb, FBIOPAN_DISPLAY, &var))
                    shown = target;
                else {
                    pan = 0;
                    dashboard_render(&ui, pixels + shown * stride, stride);
                }
            }
            ui.redraw = 0;
        }
        struct pollfd pfd = {input, POLLIN, 0};
        int got = poll(&pfd, 1, 200);
        if (got > 0 && (pfd.revents & POLLIN)) {
            char msg[80];
            ssize_t count = recv(input, msg, sizeof msg - 1, 0);
            if (count > 0) {
                msg[count] = 0;
                (void)dashboard_input(&ui, msg);
            }
        } else if (got < 0 && errno != EINTR) {
            break;
        }
    }
    if (shown != front) {
        var.yoffset = front;
        (void)ioctl(fb, FBIOPAN_DISPLAY, &var);
    }
    close(input);
    unlink(INPUT_SOCKET);
    munmap(mapped, fix.smem_len);
    close(fb);
    if (service > 0) kill(service, SIGTERM);
    return 0;
}
