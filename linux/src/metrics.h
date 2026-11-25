#ifndef METRICS_H
#define METRICS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define METRICS_MAX_IFACES 8
#define METRICS_MAX_PORTS  32
#define METRICS_IFACE_NAME 16

typedef struct {
    char name[METRICS_IFACE_NAME];
    uint64_t rx_bytes;
    uint64_t tx_bytes;
} net_iface_t;

typedef struct {
    char proto[8];      /* "tcp" o "udp" */
    uint16_t port;
    uint32_t inode;
} open_port_t;

typedef struct {
    /* RF-60: CPU, memoria y almacenamiento */
    double cpu_usage_pct;
    uint64_t mem_total_kb;
    uint64_t mem_free_kb;
    uint64_t mem_available_kb;
    uint64_t disk_total_kb;
    uint64_t disk_free_kb;

    /* RF-61: Red y puertos en escucha */
    size_t iface_count;
    net_iface_t ifaces[METRICS_MAX_IFACES];
    size_t port_count;
    open_port_t ports[METRICS_MAX_PORTS];

    /* RF-64: Consumo propio del proceso envmond */
    uint64_t proc_rss_kb;
    double proc_cpu_pct;
    int proc_open_fds;
} sys_metrics_t;

/* Rutas configurables para producción (/proc) o pruebas unitarias (PU-08) */
typedef struct {
    const char *proc_stat;
    const char *proc_meminfo;
    const char *proc_net_dev;
    const char *proc_net_tcp;
    const char *proc_net_udp;
    const char *proc_self_status;
    const char *proc_self_stat;
    const char *proc_self_fd_dir;
    const char *disk_mount_path;
} metrics_paths_t;

/* Devuelve las rutas reales de Linux (/proc/...) */
metrics_paths_t metrics_default_paths(void);

/* Recolecta todas las métricas del sistema en out. Devuelve 0 si tuvo éxito. */
int metrics_collect(const metrics_paths_t *paths, sys_metrics_t *out);

#endif /* METRICS_H */
