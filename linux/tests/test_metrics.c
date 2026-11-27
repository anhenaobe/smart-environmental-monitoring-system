#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#include <cmocka.h>
#include "metrics.h"

static char tmp_dir_path[256];
static char path_stat[300];
static char path_meminfo[300];
static char path_net_dev[300];
static char path_net_tcp[300];
static char path_net_udp[300];
static char path_self_status[300];
static char path_self_stat[300];
static char path_fd_dir[300];

static void write_temp_file(const char *filepath, const char *content) {
    FILE *f = fopen(filepath, "w");
    assert_non_null(f);
    fputs(content, f);
    fclose(f);
}

static int setup_fixtures(void **state) {
    (void)state;
    snprintf(tmp_dir_path, sizeof(tmp_dir_path), "/tmp/envmon_test_%d", getpid());
    mkdir(tmp_dir_path, 0777);

    snprintf(path_stat, sizeof(path_stat), "%s/stat", tmp_dir_path);
    snprintf(path_meminfo, sizeof(path_meminfo), "%s/meminfo", tmp_dir_path);
    snprintf(path_net_dev, sizeof(path_net_dev), "%s/net_dev", tmp_dir_path);
    snprintf(path_net_tcp, sizeof(path_net_tcp), "%s/net_tcp", tmp_dir_path);
    snprintf(path_net_udp, sizeof(path_net_udp), "%s/net_udp", tmp_dir_path);
    snprintf(path_self_status, sizeof(path_self_status), "%s/self_status", tmp_dir_path);
    snprintf(path_self_stat, sizeof(path_self_stat), "%s/self_stat", tmp_dir_path);
    snprintf(path_fd_dir, sizeof(path_fd_dir), "%s/fd", tmp_dir_path);

    mkdir(path_fd_dir, 0777);
    char fd0[350], fd1[350];
    snprintf(fd0, sizeof(fd0), "%s/0", path_fd_dir);
    snprintf(fd1, sizeof(fd1), "%s/1", path_fd_dir);
    write_temp_file(fd0, "");
    write_temp_file(fd1, "");

    /* Escribir fixtures iniciales */
    write_temp_file(path_stat, "cpu  1000 100 500 8000 50 10 5 0 0 0\n");
    write_temp_file(path_meminfo,
                    "MemTotal:        8192000 kB\n"
                    "MemFree:         4096000 kB\n"
                    "MemAvailable:    6000000 kB\n");
    write_temp_file(path_net_dev,
                    "Inter-|   Receive                                                |  Transmit\n"
                    " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
                    "    lo:     100       1    0    0    0     0          0         0      200       2    0    0    0     0       0          0\n"
                    "  eth0:    1000      10    0    0    0     0          0         0     2000      20    0    0    0     0       0          0\n");
    write_temp_file(path_net_tcp,
                    "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt uid timeout inode\n"
                    "   0: 0100007F:0050 00000000:0000 0A 00000000:0000 00:00000000 00000000     0        0 12345 1 0000000000000000 100 0 0 10 -1\n");
    write_temp_file(path_net_udp,
                    "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt uid timeout inode\n"
                    "   0: 0100007F:0035 00000000:0000 07 00000000:0000 00:00000000 00000000     0        0 54321 1 0000000000000000 100 0 0 10 -1\n");
    write_temp_file(path_self_status, "VmRSS:\t4096 kB\n");
    write_temp_file(path_self_stat, "1234 (envmond) S 1 1234 1234 0 -1 4194304 100 0 0 0 50 20 0 0 20 0 1 0 123456 5242880 100 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n");

    return 0;
}

static int teardown_fixtures(void **state) {
    (void)state;
    char cmd[500];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmp_dir_path);
    int rc = system(cmd);
    (void)rc;
    return 0;
}

static void test_metrics_collection_fixtures(void **state) {
    (void)state;

    metrics_paths_t paths = {
        .proc_stat        = path_stat,
        .proc_meminfo     = path_meminfo,
        .proc_net_dev     = path_net_dev,
        .proc_net_tcp     = path_net_tcp,
        .proc_net_udp     = path_net_udp,
        .proc_self_status = path_self_status,
        .proc_self_stat   = path_self_stat,
        .proc_self_fd_dir = path_fd_dir,
        .disk_mount_path  = "/"
    };

    sys_metrics_t metrics;
    memset(&metrics, 0, sizeof(metrics));

    /* Primera llamada (inicializa CPU delta en 0) */
    int ret = metrics_collect(&paths, &metrics);
    assert_int_equal(ret, 0);

    /* Verificaciones iniciales */
    assert_int_equal(metrics.mem_total_kb, 8192000);
    assert_int_equal(metrics.mem_free_kb, 4096000);
    assert_int_equal(metrics.mem_available_kb, 6000000);
    assert_int_equal(metrics.proc_rss_kb, 4096);
    assert_int_equal(metrics.proc_open_fds, 2); /* 0 y 1 creados en fd */

    assert_int_equal(metrics.iface_count, 2);
    assert_string_equal(metrics.ifaces[0].name, "lo");
    assert_int_equal(metrics.ifaces[0].rx_bytes, 100);
    assert_int_equal(metrics.ifaces[0].tx_bytes, 200);
    assert_string_equal(metrics.ifaces[1].name, "eth0");
    assert_int_equal(metrics.ifaces[1].rx_bytes, 1000);
    assert_int_equal(metrics.ifaces[1].tx_bytes, 2000);

    assert_int_equal(metrics.port_count, 2);
    assert_string_equal(metrics.ports[0].proto, "tcp");
    assert_int_equal(metrics.ports[0].port, 80); /* 0x0050 = 80 */
    assert_string_equal(metrics.ports[1].proto, "udp");
    assert_int_equal(metrics.ports[1].port, 53); /* 0x0035 = 53 */

    /* Segunda llamada con nuevos datos para probar delta de CPU */
    write_temp_file(path_stat, "cpu  2000 200 1000 16000 100 20 10 0 0 0\n");
    write_temp_file(path_self_stat, "1234 (envmond) S 1 1234 1234 0 -1 4194304 100 0 0 0 100 40 0 0 20 0 1 0 123456 5242880 100 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n");

    ret = metrics_collect(&paths, &metrics);
    assert_int_equal(ret, 0);
    assert_true(metrics.cpu_usage_pct >= 0.0);
    assert_true(metrics.proc_cpu_pct >= 0.0);
}

static void test_metrics_real_host(void **state) {
    (void)state;
    metrics_paths_t paths = metrics_default_paths();
    sys_metrics_t metrics;
    memset(&metrics, 0, sizeof(metrics));

    int ret = metrics_collect(&paths, &metrics);
    /* En el host real, /proc suele estar disponible, pero si no, no debe crashear */
    if (ret == 0) {
        assert_true(metrics.mem_total_kb > 0);
    }
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_metrics_collection_fixtures, setup_fixtures, teardown_fixtures),
        cmocka_unit_test(test_metrics_real_host),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
