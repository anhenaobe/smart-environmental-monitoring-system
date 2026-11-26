/* Temporary integration probe. NOT envmond; no configuration/history/reset writes.
 * A simulated ACK is opt-in and is never evidence of a durable factory reset.
 */
#define _GNU_SOURCE
#include "env_protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define SYS_PERIOD_MS 1000ull
#define WRITE_TIMEOUT_MS 500ull
#define SEQ_MODULO 100000000ul
static volatile sig_atomic_t running = 1;
static void stop(int signo) { (void)signo; running = 0; }
static unsigned long long ms_now(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) {
        perror("clock_gettime");
        exit(1);
    }
    return (unsigned long long)t.tv_sec * 1000ull + (unsigned)t.tv_nsec / 1000000ull;
}

static void usage(const char *name) {
    fprintf(stderr,
        "Usage: %s --confirm-data-uart [--duration SECONDS] [--sys-state 0-3]\n"
        "       [--simulate-reset-ack | --loopback] /dev/ttyXXX\n"
        "  Verify physical pins, device-tree mapping, console and owner first.\n"
        "  Sends SYS every second, defaults to SYS,0.\n"
        "  --duration: positive seconds, max 86400; fails if no valid ENV (normal mode).\n"
        "  --simulate-reset-ack: reply to BTN,RST without any data deletion.\n"
        "  Without this flag, BTN is reported but NOT acknowledged.\n"
        "  --loopback: isolated local TX-RX jumper only, STM32 disconnected;\n"
        "              sends diagnostic LPB frames, expects exact echoes (3s default).\n", name);
}
static bool number(const char *s, unsigned long max, unsigned long *out) {
    if (!*s) return false;
    unsigned long value = 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return false;
        unsigned long digit = (unsigned long)(*s - '0');
        if (digit > max || value > (max - digit) / 10ul) return false;
        value = value * 10ul + digit;
    }
    *out = value;
    return true;
}
static bool serial_device(const char *device) {
    /* Restrict to hardware serial terminals, common USB-UARTs and test PTYs. */
    const char *prefixes[] = {"/dev/ttyS", "/dev/ttyAMA", "/dev/ttyUSB", "/dev/ttyACM", "/dev/pts/"};
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        size_t length = strlen(prefixes[i]);
        if (!strncmp(device, prefixes[i], length)) {
            unsigned long index;
            return number(device + length, UINT32_MAX, &index);
        }
    }
    return false;
}
static bool active_console(const char *device) {
    /* Resolve symlinks beforehand; compare tty tokens, not substrings. */
    const char *name = strrchr(device, '/');
    name = name ? name + 1 : device;
    if (!strcmp(name, "console") || !strcmp(name, "tty") || !strcmp(name, "tty0"))
        return true;
    const char *sources[] = {"/sys/class/tty/console/active", "/proc/consoles", "/proc/cmdline"};
    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i) {
        FILE *f = fopen(sources[i], "r");
        if (!f) continue; /* Manual confirmation remains mandatory. */
        char token[512];
        bool found = false;
        if (i == 1) {
            char line[512];
            while (fgets(line, sizeof(line), f))
                if (sscanf(line, "%511s", token) == 1 && !strcmp(token, name)) { found = true; break; }
        } else {
            while (fscanf(f, "%511s", token) == 1) {
                const char *p = token;
                if (i == 2) {
                    if (strncmp(p, "console=", 8)) continue;
                    p += 8;
                    char *comma = strchr(token + 8, ',');
                    if (comma) *comma = '\0';
                }
                if (!strcmp(p, name)) { found = true; break; }
            }
        }
        fclose(f);
        if (found) return true;
    }
    return false;
}
static void close_exclusive(int fd) {
    (void)ioctl(fd, TIOCNXCL);
    close(fd);
}
static int configure_port(int fd, const struct termios *original) {
    struct termios t = *original;
    cfmakeraw(&t);
    t.c_iflag = 0; /* Includes software flow control IXON/IXOFF/IXANY. */
    t.c_oflag = 0;
    t.c_lflag = 0;
    t.c_cflag = (t.c_cflag & ~(CSIZE | PARENB | CSTOPB | HUPCL)) | CS8 | CREAD | CLOCAL;
#ifdef CRTSCTS
    t.c_cflag &= ~CRTSCTS;
#endif
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    if (cfsetispeed(&t, B115200) || cfsetospeed(&t, B115200)) return -1;
    return tcsetattr(fd, TCSANOW, &t);
}
static int write_all(int fd, const char *buf, size_t len) {
    const unsigned long long deadline = ms_now() + WRITE_TIMEOUT_MS;
    size_t pos = 0;
    while (pos < len) {
        if (!running) { errno = EINTR; return -1; }
        unsigned long long now = ms_now();
        if (now >= deadline) { errno = ETIMEDOUT; return -1; }
        ssize_t n = write(fd, buf + pos, len - pos);
        if (n > 0) { pos += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return -1;
        struct pollfd p = {.fd = fd, .events = POLLOUT};
        int rc = poll(&p, 1, (int)(deadline - now));
        if (rc < 0 && errno == EINTR) continue;
        if (rc < 0) return -1;
        if (rc == 0) { errno = ETIMEDOUT; return -1; }
        if (p.revents & (POLLHUP | POLLERR | POLLNVAL)) { errno = EIO; return -1; }
    }
    return 0;
}
static int send_payload(int fd, const char *payload) {
    char frame[ENV_MAX_FRAME + 1u];
    size_t n = env_frame(frame, sizeof(frame), payload);
    if (!n) { errno = EINVAL; return -1; }
    return write_all(fd, frame, n);
}
struct counts {
    unsigned long env_ok, env_sensor_error, sta, btn, malformed, unknown, sequence_jumps;
    unsigned long sys_tx, simulated_ack;
    unsigned long last_seq;
    bool have_seq;
};
/* Split retaining empty fields, unlike strtok(). */
static size_t split(char *s, char **fields, size_t cap) {
    size_t n = 1;
    fields[0] = s;
    for (char *p = s; *p; ++p) {
        if (*p != ',') continue;
        if (n == cap) return 0;
        *p = '\0';
        fields[n++] = p + 1;
    }
    return n;
}
static bool digits(const char *s, size_t width, unsigned long max, unsigned long *out) {
    return (!width || strlen(s) == width) && number(s, max, out);
}
static bool fw_version(const char *s) {
    unsigned parts = 1;
    bool digit = false;
    for (; *s; ++s) {
        if (*s >= '0' && *s <= '9') digit = true;
        else if (*s == '.' && digit && parts < 3) { ++parts; digit = false; }
        else return false;
    }
    return parts == 3 && digit;
}
enum received { UNKNOWN, MALFORMED, ENV_OK, ENV_SENSOR_ERROR, STA, BTN };
static enum received classify(const char *payload, unsigned long *sequence) {
    char copy[ENV_MAX_FRAME + 1u];
    strcpy(copy, payload); /* Passed only a bounded, NUL-terminated env_rx payload. */
    char *f[7];
    size_t count = split(copy, f, 7);
    unsigned long state, magnitude, dummy;
    if (!strncmp(payload, "ENV,", 4) || !strcmp(payload, "ENV")) {
        if (count != 5 || !digits(f[1], 8, SEQ_MODULO - 1u, sequence) ||
            !digits(f[4], 1, 3, &state)) return MALFORMED;
        if (state) return (!*f[2] && !*f[3]) ? ENV_SENSOR_ERROR : MALFORMED;
        if ((*f[2] != '+' && *f[2] != '-') ||
            !digits(f[2] + 1, 5, *f[2] == '-' ? 4500u : 13000u, &magnitude) ||
            !digits(f[3], 5, 10000u, &dummy)) return MALFORMED;
        return ENV_OK;
    }
    if (!strncmp(payload, "STA,", 4) || !strcmp(payload, "STA")) {
        if (count != 7 || !digits(f[1], 0, UINT32_MAX, &dummy) ||
            !digits(f[2], 0, UINT32_MAX, &dummy) || !digits(f[3], 1, 4, &dummy) ||
            !fw_version(f[4]) || !digits(f[5], 5, 99999u, &dummy) ||
            !digits(f[6], 5, 99999u, &dummy)) return MALFORMED;
        return STA;
    }
    if (!strncmp(payload, "BTN,", 4) || !strcmp(payload, "BTN"))
        return !strcmp(payload, "BTN,RST") ? BTN : MALFORMED;
    return UNKNOWN;
}
static int handle_payload(int fd, const char *payload, bool simulate_ack,
                          struct counts *stats, unsigned long long elapsed_ms) {
    unsigned long seq = 0;
    enum received kind = classify(payload, &seq);
    switch (kind) {
    case MALFORMED:
        stats->malformed++;
        fprintf(stderr, "RX malformed payload: %s\n", payload);
        return 0;
    case UNKNOWN:
        stats->unknown++;
        fprintf(stderr, "RX unknown payload: %s\n", payload);
        return 0;
    case ENV_OK: stats->env_ok++; break;
    case ENV_SENSOR_ERROR: stats->env_sensor_error++; break;
    case STA: stats->sta++; break;
    case BTN: stats->btn++; break;
    }
    fprintf(stdout, "RX %s [t=%llums]\n", payload, elapsed_ms);
    fflush(stdout);
    if (kind == ENV_OK || kind == ENV_SENSOR_ERROR) {
        if (stats->have_seq && seq != (stats->last_seq + 1ul) % SEQ_MODULO) {
            stats->sequence_jumps++;
            fprintf(stderr, "ENV sequence discontinuity: %08lu -> %08lu (gap, duplicate or reboot)\n",
                    stats->last_seq, seq);
        }
        stats->last_seq = seq;
        stats->have_seq = true;
    }
    if (kind == BTN) {
        if (!simulate_ack) {
            fprintf(stderr, "BTN,RST observed; no ACK, no reset.\n");
        } else {
            if (send_payload(fd, "ACK,RST") < 0) return -1;
            stats->simulated_ack++;
            fprintf(stderr, "TX ACK,RST (SIMULATED ONLY; no reset.pending, no factory reset)\n");
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    bool confirmed = false, simulate_ack = false, loopback = false, state_set = false;
    unsigned long duration = 0, sys_state = 0;
    static const struct option options[] = {
        {"confirm-data-uart", no_argument, NULL, 'c'},
        {"duration", required_argument, NULL, 'd'},
        {"sys-state", required_argument, NULL, 's'},
        {"simulate-reset-ack", no_argument, NULL, 'a'},
        {"loopback", no_argument, NULL, 'l'},
        {"help", no_argument, NULL, 'h'}, {NULL, 0, NULL, 0}
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "", options, NULL)) != -1) {
        switch (opt) {
        case 'c': confirmed = true; break;
        case 'a': simulate_ack = true; break;
        case 'l': loopback = true; break;
        case 'd': if (!number(optarg, 86400u, &duration) || !duration) { usage(argv[0]); return 2; } break;
        case 's': if (!number(optarg, 3u, &sys_state)) { usage(argv[0]); return 2; } state_set = true; break;
        case 'h': usage(argv[0]); return 0;
        default: usage(argv[0]); return 2;
        }
    }
    if (!confirmed || optind != argc - 1 || (loopback && (simulate_ack || state_set))) {
        usage(argv[0]); return 2;
    }
    if (loopback && !duration) duration = 3;
    char device[PATH_MAX];
    if (!realpath(argv[optind], device)) { perror("resolve UART path"); return 1; }
    struct stat st;
    if (stat(device, &st) < 0) { perror("stat UART"); return 1; }
    if (!S_ISCHR(st.st_mode) || strncmp(device, "/dev/", 5) != 0) {
        fprintf(stderr, "Refusing non-/dev character device: %s\n", device);
        return 1;
    }
    if (active_console(device)) {
        fprintf(stderr, "Refusing active/kernel console: %s\n", device);
        return 1;
    }
    if (!serial_device(device)) {
        fprintf(stderr, "Refusing unrecognized serial device: %s\n", device);
        return 1;
    }

    struct sigaction sa = {.sa_handler = stop};
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("sigaction");
        return 1;
    }
    int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { perror("open UART"); return 1; }
    if (ioctl(fd, TIOCEXCL) < 0) { perror("TIOCEXCL"); close(fd); return 1; }
    struct termios original;
    if (tcgetattr(fd, &original) < 0) { perror("tcgetattr"); close_exclusive(fd); return 1; }

    int result = 0;
    if (configure_port(fd, &original) < 0) {
        perror("termios configuration");
        (void)tcsetattr(fd, TCSANOW, &original);
        close_exclusive(fd);
        return 1;
    }
    env_rx_t rx;
    env_rx_init(&rx);
    struct counts stats = {0};
    unsigned long loop_tx = 0, loop_echo = 0, loop_other = 0;
    bool loop_seen = false;
    char loop_expected[24] = {0};
    char sys[6] = "SYS,0";
    sys[4] = (char)('0' + sys_state);
    const unsigned long long start = ms_now();
    unsigned long long next_sys = start;
    fprintf(stderr, "UART diagnostic opened %s (115200 8N1), %s each second; Ctrl+C to exit\n",
            device, loopback ? "LPB loopback" : sys);
    if (simulate_ack) fprintf(stderr, "WARNING: reset ACK simulation enabled; no real factory reset performed.\n");

    while (running) {
        unsigned long long now = ms_now();
        if (duration && now - start >= duration * 1000ull) break;
        if (now >= next_sys) {
            if (loopback) {
                snprintf(loop_expected, sizeof(loop_expected), "LPB,%08lu", loop_tx % SEQ_MODULO);
                loop_seen = false;
            }
            if (send_payload(fd, loopback ? loop_expected : sys) < 0) {
                if (running) { perror(loopback ? "write LPB" : "write SYS"); result = 1; }
                break;
            }
            if (loopback) loop_tx++;
            else stats.sys_tx++;
            /* Skip missed ticks; no burst of heartbeats following a stall. */
            next_sys = now + SYS_PERIOD_MS;
        }
        struct pollfd p = {.fd = fd, .events = POLLIN};
        int rc = poll(&p, 1, 100);
        if (rc < 0) {
            if (errno == EINTR) continue;
            perror("poll UART"); result = 1; break;
        }
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fprintf(stderr, "UART poll error/hangup (revents=0x%x)\n", (unsigned)p.revents);
            result = 1; break;
        }
        if (p.revents & POLLIN) {
            char bytes[256], payload[ENV_MAX_FRAME + 1u];
            ssize_t n = read(fd, bytes, sizeof(bytes));
            if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            if (n < 0) { perror("read UART"); result = 1; break; }
            for (ssize_t j = 0; j < n && running; ++j) {
                if (!env_rx_byte(&rx, bytes[j], payload, sizeof(payload))) continue;
                if (loopback) {
                    if (loop_tx && !loop_seen && !strcmp(payload, loop_expected)) {
                        ++loop_echo; loop_seen = true;
                        printf("ECHO %s\n", payload); fflush(stdout);
                    } else {
                        ++loop_other;
                        fprintf(stderr, "Unexpected loopback payload: %s\n", payload);
                    }
                    continue;
                }
                if (handle_payload(fd, payload, simulate_ack, &stats, ms_now() - start) < 0) {
                    if (running) { perror("write simulated ACK"); result = 1; }
                    break;
                }
            }
            if (result) break;
        }
    }

    fprintf(stderr,
        "SUMMARY sys_tx=%lu env_ok=%lu env_sensor_error=%lu sta=%lu btn=%lu "
        "invalid_frames=%u malformed_payloads=%lu unknown_payloads=%lu sequence_jumps=%lu simulated_ack=%lu\n",
        stats.sys_tx, stats.env_ok, stats.env_sensor_error, stats.sta, stats.btn,
        rx.errors, stats.malformed, stats.unknown, stats.sequence_jumps, stats.simulated_ack);
    if (duration && !running) {
        fprintf(stderr, "Timed test interrupted before completion.\n");
        result = 1;
    }
    if (loopback) {
        fprintf(stderr, "LOOPBACK tx=%lu echo=%lu other=%lu invalid_frames=%u\n",
                loop_tx, loop_echo, loop_other, rx.errors);
        if (!running || loop_tx != duration || loop_echo != loop_tx || loop_other || rx.errors) result = 1;
    } else if (duration && (stats.env_ok + stats.env_sensor_error == 0)) {
        fprintf(stderr, "Timed test did not observe any valid ENV frame.\n");
        result = 1;
    }
    if (tcsetattr(fd, TCSANOW, &original) < 0) { perror("restore termios"); result = 1; }
    close_exclusive(fd);
    return result;
}
