#define _POSIX_C_SOURCE 200809L
#include "metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/statvfs.h>

/* Estado previo para calcular delta de CPU entre llamadas */
static uint64_t prev_total_jiffies = 0;
static uint64_t prev_idle_jiffies = 0;
static uint64_t prev_proc_jiffies = 0;
static bool has_prev_cpu = false;

metrics_paths_t metrics_default_paths(void) {
    metrics_paths_t p = {
        .proc_stat        = "/proc/stat",
        .proc_meminfo     = "/proc/meminfo",
        .proc_net_dev     = "/proc/net/dev",
        .proc_net_tcp     = "/proc/net/tcp",
        .proc_net_udp     = "/proc/net/udp",
        .proc_self_status = "/proc/self/status",
        .proc_self_stat   = "/proc/self/stat",
        .proc_self_fd_dir = "/proc/self/fd",
        .disk_mount_path  = "/"
    };
    return p;
}

static int parse_cpu_and_proc(const char *stat_path, const char *self_stat_path,
                              double *sys_cpu_pct, double *proc_cpu_pct) {
    FILE *f = fopen(stat_path, "r");
    if (!f) return -1;

    uint64_t user = 0, nice = 0, system = 0, idle = 0;
    uint64_t iowait = 0, irq = 0, softirq = 0, steal = 0;
    int scanned = fscanf(f, "cpu  %lu %lu %lu %lu %lu %lu %lu %lu",
                         &user, &nice, &system, &idle,
                         &iowait, &irq, &softirq, &steal);
    fclose(f);
    if (scanned < 4) return -1;

    uint64_t idle_all = idle + iowait;
    uint64_t total_all = user + nice + system + idle_all + irq + softirq + steal;

    /* Ticks del propio proceso desde /proc/self/stat (campos 14 utime y 15 stime) */
    uint64_t proc_ticks = 0;
    FILE *fp = fopen(self_stat_path, "r");
    if (fp) {
        char buf[1024];
        if (fgets(buf, sizeof(buf), fp)) {
            char *rparen = strrchr(buf, ')');
            if (rparen && rparen[1] == ' ') {
                unsigned long utime = 0, stime = 0;
                char state;
                int ppid, pgrp, session, tty_nr, tpgid;
                unsigned flags;
                unsigned long minflt, cminflt, majflt, cmajflt;
                if (sscanf(rparen + 2, "%c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu",
                           &state, &ppid, &pgrp, &session, &tty_nr, &tpgid,
                           &flags, &minflt, &cminflt, &majflt, &cmajflt,
                           &utime, &stime) == 13) {
                    proc_ticks = utime + stime;
                }
            }
        }
        fclose(fp);
    }

    if (!has_prev_cpu || total_all <= prev_total_jiffies) {
        *sys_cpu_pct = 0.0;
        *proc_cpu_pct = 0.0;
    } else {
        uint64_t delta_total = total_all - prev_total_jiffies;
        uint64_t delta_idle = (idle_all >= prev_idle_jiffies) ? (idle_all - prev_idle_jiffies) : 0;
        uint64_t delta_proc = (proc_ticks >= prev_proc_jiffies) ? (proc_ticks - prev_proc_jiffies) : 0;

        *sys_cpu_pct = (double)(delta_total - delta_idle) * 100.0 / (double)delta_total;
        *proc_cpu_pct = (double)delta_proc * 100.0 / (double)delta_total;
    }

    prev_total_jiffies = total_all;
    prev_idle_jiffies = idle_all;
    prev_proc_jiffies = proc_ticks;
    has_prev_cpu = true;
    return 0;
}

static int parse_meminfo(const char *path, uint64_t *total_kb, uint64_t *free_kb, uint64_t *avail_kb) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    int found = 0;
    *total_kb = 0;
    *free_kb = 0;
    *avail_kb = 0;

    while (fgets(line, sizeof(line), f)) {
        uint64_t val = 0;
        if (sscanf(line, "MemTotal: %lu kB", &val) == 1) {
            *total_kb = val;
            found++;
        } else if (sscanf(line, "MemFree: %lu kB", &val) == 1) {
            *free_kb = val;
            found++;
        } else if (sscanf(line, "MemAvailable: %lu kB", &val) == 1) {
            *avail_kb = val;
            found++;
        }
    }
    fclose(f);
    return (found >= 2) ? 0 : -1;
}

static int parse_disk(const char *mount_path, uint64_t *total_kb, uint64_t *free_kb) {
    if (!mount_path) {
        *total_kb = 0;
        *free_kb = 0;
        return 0;
    }
    struct statvfs st;
    if (statvfs(mount_path, &st) != 0) return -1;
    *total_kb = ((uint64_t)st.f_blocks * st.f_frsize) / 1024ULL;
    *free_kb  = ((uint64_t)st.f_bavail * st.f_frsize) / 1024ULL;
    return 0;
}

static int parse_net_dev(const char *path, net_iface_t *ifaces, size_t max_ifaces, size_t *count) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[512];
    *count = 0;
    int line_num = 0;

    while (fgets(line, sizeof(line), f)) {
        line_num++;
        if (line_num <= 2) continue; /* Saltar cabeceras */
        if (*count >= max_ifaces) break;

        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';

        char *name = line;
        while (isspace((unsigned char)*name)) name++;

        uint64_t rx_bytes = 0, rx_packets = 0, rx_errs = 0, rx_drop = 0;
        uint64_t rx_fifo = 0, rx_frame = 0, rx_compressed = 0, rx_multicast = 0;
        uint64_t tx_bytes = 0;

        int n = sscanf(colon + 1, "%lu %lu %lu %lu %lu %lu %lu %lu %lu",
                       &rx_bytes, &rx_packets, &rx_errs, &rx_drop,
                       &rx_fifo, &rx_frame, &rx_compressed, &rx_multicast,
                       &tx_bytes);
        if (n == 9) {
            snprintf(ifaces[*count].name, METRICS_IFACE_NAME, "%.*s", (int)(METRICS_IFACE_NAME - 1), name);
            ifaces[*count].rx_bytes = rx_bytes;
            ifaces[*count].tx_bytes = tx_bytes;
            (*count)++;
        }
    }
    fclose(f);
    return 0;
}

static void parse_proc_net_sockets(const char *path, const char *proto, bool is_tcp,
                                   open_port_t *ports, size_t max_ports, size_t *count) {
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[512];
    int line_num = 0;
    while (fgets(line, sizeof(line), f)) {
        line_num++;
        if (line_num == 1) continue; /* Cabecera */
        if (*count >= max_ports) break;

        unsigned int sl = 0, local_addr = 0, local_port = 0;
        unsigned int rem_addr = 0, rem_port = 0, state = 0;
        unsigned int tx_q = 0, rx_q = 0, tr = 0, tm_when = 0, retrnsmt = 0;
        unsigned int uid = 0, timeout = 0, inode = 0;

        int n = sscanf(line, " %u: %x:%x %x:%x %x %x:%x %x:%x %x %u %u %u",
                       &sl, &local_addr, &local_port, &rem_addr, &rem_port,
                       &state, &tx_q, &rx_q, &tr, &tm_when, &retrnsmt,
                       &uid, &timeout, &inode);
        if (n >= 14) {
            /* TCP_LISTEN = 0x0A; UDP en escucha suele tener estado 0x07 */
            bool listening = is_tcp ? (state == 0x0A) : (state == 0x07);
            if (listening) {
                snprintf(ports[*count].proto, sizeof(ports[*count].proto), "%s", proto);
                ports[*count].port = (uint16_t)local_port;
                ports[*count].inode = inode;
                (*count)++;
            }
        }
    }
    fclose(f);
}

static uint64_t parse_self_rss(const char *status_path) {
    FILE *f = fopen(status_path, "r");
    if (!f) return 0;

    char line[256];
    uint64_t rss_kb = 0;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "VmRSS: %lu kB", &rss_kb) == 1) {
            break;
        }
    }
    fclose(f);
    return rss_kb;
}

static int count_open_fds(const char *fd_dir_path) {
    if (!fd_dir_path) return -1;
    DIR *d = opendir(fd_dir_path);
    if (!d) return -1;

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        count++;
    }
    closedir(d);
    return count;
}

int metrics_collect(const metrics_paths_t *paths, sys_metrics_t *out) {
    if (!paths || !out) return -1;
    memset(out, 0, sizeof(*out));

    if (parse_cpu_and_proc(paths->proc_stat, paths->proc_self_stat,
                           &out->cpu_usage_pct, &out->proc_cpu_pct) != 0) {
        return -1;
    }
    if (parse_meminfo(paths->proc_meminfo, &out->mem_total_kb,
                      &out->mem_free_kb, &out->mem_available_kb) != 0) {
        return -1;
    }
    if (parse_disk(paths->disk_mount_path, &out->disk_total_kb, &out->disk_free_kb) != 0) {
        return -1;
    }
    if (parse_net_dev(paths->proc_net_dev, out->ifaces,
                      METRICS_MAX_IFACES, &out->iface_count) != 0) {
        return -1;
    }

    out->port_count = 0;
    parse_proc_net_sockets(paths->proc_net_tcp, "tcp", true,
                           out->ports, METRICS_MAX_PORTS, &out->port_count);
    parse_proc_net_sockets(paths->proc_net_udp, "udp", false,
                           out->ports, METRICS_MAX_PORTS, &out->port_count);

    out->proc_rss_kb = parse_self_rss(paths->proc_self_status);
    out->proc_open_fds = count_open_fds(paths->proc_self_fd_dir);

    return 0;
}
