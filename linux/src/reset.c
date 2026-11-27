/*
 * Factory reset recuperable de envmond (ver reset.h).
 */
#define _GNU_SOURCE

#include "reset.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int posix_crear(const char *ruta) {
    return open(ruta, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
}

static int posix_sincronizar(int fd) { return fsync(fd); }

static int posix_sincronizar_dir(const char *dir) {
    int fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return -1;
    int r = fsync(fd);
    if (r != 0 && errno == EINVAL) r = 0;
    close(fd);
    return r;
}

static int posix_borrar(const char *ruta) {
    return (unlink(ruta) == 0 || errno == ENOENT) ? 0 : -1;
}

static const reset_fs_ops_t OPS_POSIX = {
    posix_crear, posix_sincronizar, posix_sincronizar_dir, posix_borrar
};

const reset_fs_ops_t *reset_fs_ops_posix(void) { return &OPS_POSIX; }

static bool ruta(char *buf, size_t n, const char *dir, const char *archivo) {
    int r = snprintf(buf, n, "%s/%s", dir, archivo);
    return r > 0 && (size_t)r < n;
}

bool reset_pendiente(const char *dir_config) {
    char m[512];
    struct stat st;
    return ruta(m, sizeof(m), dir_config, RESET_MARCADOR) && stat(m, &st) == 0;
}

int reset_marcar(const char *dir_config, const reset_fs_ops_t *ops) {
    if (ops == NULL) ops = &OPS_POSIX;
    char m[512];
    if (!ruta(m, sizeof(m), dir_config, RESET_MARCADOR)) return -1;
    int fd = ops->crear(m);
    if (fd < 0) return -1;
    if (ops->sincronizar(fd) != 0) {
        close(fd);
        goto retirar;
    }
    if (close(fd) != 0) goto retirar;
    if (ops->sincronizar_dir(dir_config) != 0) goto retirar;
    return 0;
retirar:
    /* No durable: no se confirma el reset. Se retira para que un arranque
     * posterior no ejecute un reset que nunca se confirmo con ACK. */
    (void)ops->borrar(m);
    return -1;
}

/* Borra los archivos regulares de dir (no los subdirectorios). Devuelve 0,
 * 1 si dir no existe (nada que borrar) o -1 si fallo un borrado. */
static int vaciar_datos(const char *dir, const reset_fs_ops_t *ops) {
    DIR *d = opendir(dir);
    if (d == NULL) return errno == ENOENT ? 1 : -1;
    int res = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        struct stat st;
        if (fstatat(dirfd(d), e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
            if (errno == ENOENT) continue;
            res = -1;
            continue;
        }
        if (!S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode)) continue;
        char p[768];
        if (!ruta(p, sizeof(p), dir, e->d_name) || ops->borrar(p) != 0) res = -1;
    }
    closedir(d);
    return res;
}

int reset_completar(const char *dir_config, const char *dir_datos, const reset_fs_ops_t *ops) {
    if (ops == NULL) ops = &OPS_POSIX;
    char cfg[512], tmp[512], m[512];
    if (!ruta(cfg, sizeof(cfg), dir_config, "config") ||
        !ruta(tmp, sizeof(tmp), dir_config, "config.tmp") ||
        !ruta(m, sizeof(m), dir_config, RESET_MARCADOR))
        return -1;
    /* Orden: historial, luego configuracion, por ultimo el marcador. */
    const int datos = vaciar_datos(dir_datos, ops);
    if (datos < 0) return -1;
    if (datos == 0 && ops->sincronizar_dir(dir_datos) != 0) return -1;
    if (ops->borrar(cfg) != 0 || ops->borrar(tmp) != 0) return -1;
    if (ops->sincronizar_dir(dir_config) != 0) return -1;
    if (ops->borrar(m) != 0) return -1;
    if (ops->sincronizar_dir(dir_config) != 0) return -1;
    return 0;
}
