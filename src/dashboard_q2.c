/* SPDX-License-Identifier: MIT */
/* Full-screen graphical Library app. Its music/player audio remains untouched. */
#include "dashboard.h"
#include "theme.h"
#include "lang.h"
#include "weather.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
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

#define INPUT_SOCKET "/tmp/q2-weather.input.sock"
#define BROWSE_SOCKET "/tmp/q2-weather.browse.sock"
#define STATE_PATH "/mnt/mmc/.weather/state"
#define SERVICE_PATH "/mnt/mmc/.weather/q2-weather-service"
#define UNITS_PATH "/mnt/mmc/.weather/units"
#define STATUS_PATH "/mnt/mmc/.weather/network-status"
#define STARTUP_LOG "/mnt/mmc/.weather/startup.log"
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
        execl(SERVICE_PATH, "q2-weather-service", (char *)NULL);
        /* Startup failures occur before any HTTP request can create a log. */
        FILE *file = fopen(STARTUP_LOG, "ab");
        if (file) {
            fprintf(file, "service exec failed: errno=%d\n", errno);
            fclose(file);
        }
        _exit(127);
    }
    if (pid < 0) {
        FILE *file = fopen(STARTUP_LOG, "ab");
        if (file) {
            fprintf(file, "service fork failed: errno=%d\n", errno);
            fclose(file);
        }
    }
    return pid > 0 ? pid : -1;
}

/* Q2SPOT/1 service is an internal helper, not a second installed card app.
 * Keep network and city lookup off the display/input thread. */
static int service_command(const char *op, const char *arg, char *response, size_t cap) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof address.sun_path, "%s", BROWSE_SOCKET);
    struct timeval timeout = {2, 0};
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    if (connect(fd, (struct sockaddr *)&address, sizeof address)) {
        close(fd);
        return -1;
    }
    char request[256];
    int count = snprintf(request, sizeof request, "Q2SPOT/1\n1\t%s\t%s\n",
                         op, arg ? arg : "");
    if (count < 0 || count >= (int)sizeof request ||
        send(fd, request, (size_t)count, MSG_NOSIGNAL) != count) {
        close(fd);
        return -1;
    }
    shutdown(fd, SHUT_WR);
    if (response && cap > 1) {
        size_t used = 0;
        ssize_t got;
        while (used + 1 < cap && (got = recv(fd, response + used, cap - used - 1, 0)) > 0)
            used += (size_t)got;
        if (!used) {
            close(fd);
            return -1;
        }
        response[used] = 0;
    }
    close(fd);
    return 0;
}

static _Atomic int city_result; /* 0 pending, 2 results, -1 no results */
static _Atomic int city_generation;
static pthread_mutex_t city_mutex = PTHREAD_MUTEX_INITIALIZER;
static WeatherCityChoice city_choices[WEATHER_CITY_RESULTS_MAX];
static int city_count;
static char city_job[48];

static int parse_choices(char *reply, WeatherCityChoice out[WEATHER_CITY_RESULTS_MAX]) {
    int count = 0;
    char *save = NULL;
    for (char *line = strtok_r(reply, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        const char *prefix = "action\tsavecity\t";
        if (strncmp(line, prefix, strlen(prefix))) continue;
        char *name = line + strlen(prefix);
        char *tab = strchr(name, '\t');
        if (!tab) continue;
        *tab++ = 0;
        char *region = tab;
        tab = strchr(region, '\t');
        if (!tab) continue;
        *tab++ = 0;
        double lat = 0, lon = 0;
        if (sscanf(tab, "%lf %lf", &lat, &lon) != 2 ||
            lat < -90 || lat > 90 || lon < -180 || lon > 180 ||
            !name[0] || count == WEATHER_CITY_RESULTS_MAX) continue;
        WeatherCityChoice *choice = &out[count];
        snprintf(choice->name, sizeof choice->name, "%s", name);
        snprintf(choice->region, sizeof choice->region, "%s", region);
        choice->latitude = lat;
        choice->longitude = lon;
        ++count;
    }
    return count;
}

static void *city_lookup(void *unused) {
    (void)unused;
    char query[sizeof city_job];
    snprintf(query, sizeof query, "%s", city_job);
    int generation = atomic_load(&city_generation);
    char response[4096];
    WeatherCityChoice found[WEATHER_CITY_RESULTS_MAX] = {0};
    int count = -1;
    if (!service_command("citysearch", query, response, sizeof response)) {
        for (int attempt = 0; attempt < 18 && !interrupted; attempt++) {
            sleep(1);
            if (service_command("cities", "", response, sizeof response)) continue;
            /* The weather service marks its in-progress searches as refresh. */
            if (strstr(response, "\trefresh\t")) continue;
            count = parse_choices(response, found);
            break;
        }
    }
    if (generation == atomic_load(&city_generation)) {
        pthread_mutex_lock(&city_mutex);
        city_count = count > 0 ? count : 0;
        if (count > 0) memcpy(city_choices, found, (size_t)count * sizeof found[0]);
        pthread_mutex_unlock(&city_mutex);
        atomic_store(&city_result, count > 0 ? 2 : -1);
    }
    return NULL;
}

/* Q2 Pod stores its Display theme, Light Mode and accent in this INI.
 * Light=2 follows the firmware's 07:00–19:00 automatic day interval. */
static void read_theme(Dashboard *ui) {
    FILE *file = fopen("/mnt/data/config.ini", "rb");
    if (!file) return;
    char data[16384];
    size_t len = fread(data, 1, sizeof data, file);
    fclose(file);
    time_t now = time(NULL);
    struct tm local;
    int hour = localtime_r(&now, &local) ? local.tm_hour : 0;
    WeatherTheme theme;
    weather_theme_parse(&theme, data, len, hour);
    if (ui->theme_light != theme.light || ui->theme_id != theme.style ||
        ui->theme_accent != theme.accent || ui->wheel_percent != theme.wheel ||
        ui->language_id != theme.language) {
        if (ui->wheel_percent != theme.wheel) ui->wheel_rem = 0;
        ui->theme_light = theme.light;
        ui->theme_id = theme.style;
        ui->theme_accent = theme.accent;
        ui->wheel_percent = theme.wheel;
        if (ui->language_id != theme.language) weather_language_select(theme.language);
        ui->language_id = theme.language;
        ui->redraw = 1;
    }
}

static int load_units(void) {
    FILE *file = fopen(UNITS_PATH, "rb");
    if (!file) return 0;
    int unit = fgetc(file) == '1';
    fclose(file);
    return unit;
}

static void save_units(int unit) {
    FILE *file = fopen(UNITS_PATH, "wb");
    if (file) {
        (void)fputc(unit ? '1' : '0', file);
        (void)fclose(file);
    }
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

static void read_network_status(Dashboard *ui) {
    char next[sizeof ui->network_status] = {0};
    FILE *file = fopen(STATUS_PATH, "rb");
    if (file) {
        (void)fgets(next, sizeof next, file);
        fclose(file);
        next[strcspn(next, "\n")] = 0;
    }
    if (strcmp(ui->network_status, next)) {
        snprintf(ui->network_status, sizeof ui->network_status, "%s", next);
        ui->redraw = 1;
    }
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
    ui.units_f = load_units();
    ui.theme_accent = 0;
    ui.wheel_percent = 50;
    weather_language_select(0);
    read_theme(&ui);
    (void)read_cache(&ui);
    read_network_status(&ui);
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
    unsigned long last_service_check = 0;
    unsigned long last_theme_check = 0;
    int initial_refresh = service > 0; /* Newly launched helper refreshes itself. */
    int last_units = ui.units_f;
    while (!interrupted && !ui.quit) {
        unsigned long now = now_ms();
        if (now - last_theme_check >= 10000) {
            read_theme(&ui);
            last_theme_check = now;
        }
        if (now - last_service_check >= 10000) {
            if (!connected_service()) {
                pid_t restarted = start_service();
                if (restarted > 0) {
                    service = restarted;
                    initial_refresh = 1;
                }
            }
            last_service_check = now;
        }
        if (!initial_refresh && connected_service()) {
            (void)service_command("refresh", "", NULL, 0);
            initial_refresh = 1;
        }
        if (now - last_poll >= REFRESH_MS) {
            (void)read_cache(&ui);
            read_network_status(&ui);
            /* Show startup failures even when there is no weather.log yet. */
            if (!connected_service()) {
                const char *message = "Weather service unavailable";
                if (strcmp(ui.network_status, message)) {
                    snprintf(ui.network_status, sizeof ui.network_status, "%s", message);
                    ui.redraw = 1;
                }
            }
            last_poll = now;
        }
        int city_state = atomic_load(&city_result);
        if (ui.search_result == 1 && city_state) {
            if (city_state == 2) {
                pthread_mutex_lock(&city_mutex);
                ui.match_count = city_count;
                memcpy(ui.matches, city_choices, (size_t)city_count * sizeof city_choices[0]);
                pthread_mutex_unlock(&city_mutex);
                ui.match_selected = 0;
                ui.page = 5;
                ui.wheel_rem = 0;
                ui.search_result = 0;
            } else {
                ui.search_result = -1;
            }
            atomic_store(&city_result, 0);
            ui.redraw = 1;
        }
        if (ui.search_request) {
            ui.search_request = 0;
            snprintf(city_job, sizeof city_job, "%s", ui.city_query);
            atomic_fetch_add(&city_generation, 1);
            atomic_store(&city_result, 0);
            ui.search_result = 1;
            pthread_t worker;
            if (!pthread_create(&worker, NULL, city_lookup, NULL))
                pthread_detach(worker);
            else
                ui.search_result = -1;
        }
        if (ui.save_request) {
            ui.save_request = 0;
            if (ui.match_selected >= 0 && ui.match_selected < ui.match_count) {
                char coordinates[90], response[512];
                WeatherCityChoice *selected = &ui.matches[ui.match_selected];
                snprintf(coordinates, sizeof coordinates, "%d %.6f %.6f",
                         ui.city_target, selected->latitude, selected->longitude);
                if (!service_command("savecity", coordinates, response, sizeof response) &&
                    strstr(response, "\tok\t")) {
                    ui.search_result = 2;
                    ui.slot = ui.city_target;
                } else {
                    ui.search_result = -1;
                }
                ui.redraw = 1;
            }
        }
        if (ui.refresh_request) {
            ui.refresh_request = 0;
            (void)service_command("refresh", "", NULL, 0);
        }
        if (ui.clear_request) {
            ui.clear_request = 0;
            (void)service_command("clear", "", NULL, 0);
        }
        if (ui.units_f != last_units) {
            last_units = ui.units_f;
            save_units(last_units);
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
        int got = poll(&pfd, 1, 1000);
        if (got > 0 && (pfd.revents & POLLIN)) {
            char msg[160];
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
