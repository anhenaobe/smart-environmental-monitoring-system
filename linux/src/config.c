/*
 * Configuracion de envmond (ver config.h).
 */
#define _GNU_SOURCE

#include "config.h"

#include <crypt.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONFIG_ARCHIVO     "config"
#define CONFIG_ARCHIVO_TMP "config.tmp"
#define CONFIG_ARCHIVO_MAX 4096u  /* un archivo valido ocupa < 600 bytes */
#define CONFIG_SAL_LEN     16u
#define CONFIG_HASH_SHA512 86u    /* caracteres del resumen de "$6$" */

static const char ALFABETO_CRYPT[] =
    "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

/* Claves del archivo, en el mismo orden que config_campo_t. */
static const char *const CLAVES[CONFIG_N_CAMPOS] = {
    "nombre", "admin_hash", "ntp", "intervalo_registro_s",
    "temp_min", "temp_max", "hr_min", "hr_max", "retencion_dias"
};

/* ---------- validacion de textos ---------- */

static bool es_digito(char c) { return c >= '0' && c <= '9'; }

static bool es_alnum_ascii(char c) {
    return es_digito(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* Nombre: 1..40 bytes; letras ASCII y latinas acentuadas (U+00C0..U+017F,
 * salvo x y division), digitos, espacio y -_.(); al menos un caracter que no
 * sea espacio. Rechaza '=', saltos de linea, control y <>&"'. */
static config_motivo_t validar_nombre(const char *s) {
    if (s == NULL || s[0] == '\0') return CONFIG_MOTIVO_VACIO;
    size_t n = strlen(s);
    if (n > CONFIG_NOMBRE_MAX) return CONFIG_MOTIVO_LONGITUD;
    bool visible = false;
    for (size_t i = 0; i < n; i++) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x80u) {
            if (es_alnum_ascii((char)c) || c == '-' || c == '_' || c == '.' ||
                c == '(' || c == ')') {
                visible = true;
            } else if (c != ' ') {
                return CONFIG_MOTIVO_CARACTER;
            }
            continue;
        }
        /* Secuencia UTF-8 de dos bytes en Latin-1 Supplement / Latin Ext-A. */
        if (i + 1u >= n) return CONFIG_MOTIVO_CARACTER;
        const unsigned char d = (unsigned char)s[i + 1u];
        if (d < 0x80u || d > 0xBFu) return CONFIG_MOTIVO_CARACTER;
        if (c == 0xC3u) {
            if (d == 0x97u || d == 0xB7u) return CONFIG_MOTIVO_CARACTER; /* x / */
        } else if (c != 0xC4u && c != 0xC5u) {
            return CONFIG_MOTIVO_CARACTER;
        }
        visible = true;
        i++;
    }
    return visible ? CONFIG_MOTIVO_OK : CONFIG_MOTIVO_VACIO;
}

static config_motivo_t validar_password(const char *s) {
    if (s == NULL || s[0] == '\0') return CONFIG_MOTIVO_VACIO;
    size_t n = strlen(s);
    if (n < CONFIG_PASSWORD_MIN || n > CONFIG_PASSWORD_MAX) return CONFIG_MOTIVO_LONGITUD;
    for (size_t i = 0; i < n; i++) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x20u || c == 0x7Fu) return CONFIG_MOTIVO_CARACTER;
    }
    return CONFIG_MOTIVO_OK;
}

/* NTP: vacio (sin NTP) o nombre de host: etiquetas de 1..63 caracteres
 * [A-Za-z0-9-], sin '-' al inicio ni al final, separadas por '.'. */
static config_motivo_t validar_ntp(const char *s) {
    if (s == NULL || s[0] == '\0') return CONFIG_MOTIVO_OK;
    size_t n = strlen(s);
    if (n > CONFIG_NTP_MAX) return CONFIG_MOTIVO_LONGITUD;
    size_t etiqueta = 0;
    for (size_t i = 0; i <= n; i++) {
        const char c = s[i];
        if (c == '.' || c == '\0') {
            if (etiqueta == 0u || s[i - 1u] == '-') return CONFIG_MOTIVO_FORMATO;
            etiqueta = 0;
        } else if (es_alnum_ascii(c) || c == '-') {
            if (c == '-' && etiqueta == 0u) return CONFIG_MOTIVO_FORMATO;
            if (++etiqueta > 63u) return CONFIG_MOTIVO_FORMATO;
        } else {
            return CONFIG_MOTIVO_CARACTER;
        }
    }
    return CONFIG_MOTIVO_OK;
}

static bool es_caracter_crypt(char c) {
    return c != '\0' && strchr(ALFABETO_CRYPT, c) != NULL;
}

/* Formato exacto de crypt SHA-512 sin "rounds=": $6$<sal 1..16>$<86>. */
static bool hash_bien_formado(const char *h) {
    if (h == NULL || strncmp(h, "$6$", 3) != 0) return false;
    const char *p = h + 3;
    size_t sal = 0;
    while (es_caracter_crypt(*p)) { p++; sal++; }
    if (sal == 0u || sal > CONFIG_SAL_LEN || *p != '$') return false;
    p++;
    size_t resumen = 0;
    while (es_caracter_crypt(*p)) { p++; resumen++; }
    return *p == '\0' && resumen == CONFIG_HASH_SHA512;
}

bool config_parse_centi(const char *s, int32_t *out) {
    if (s == NULL || out == NULL) return false;
    const char *p = s;
    bool negativo = false;
    if (*p == '-') { negativo = true; p++; }
    int32_t entero = 0;
    size_t digitos = 0;
    while (es_digito(*p)) {
        if (++digitos > 4u) return false;  /* el rango admitido cabe en 4 */
        entero = entero * 10 + (*p - '0');
        p++;
    }
    if (digitos == 0u) return false;
    int32_t frac = 0;
    if (*p == '.') {
        p++;
        if (!es_digito(p[0])) return false;
        frac = (p[0] - '0') * 10;
        p++;
        if (es_digito(*p)) { frac += *p - '0'; p++; }
    }
    if (*p != '\0') return false;
    int32_t v = entero * 100 + frac;
    *out = negativo ? -v : v;
    return true;
}

/* Entero sin signo de 1 a 10 digitos, sin espacios. */
static bool parse_u32(const char *s, uint32_t *out) {
    if (s == NULL || !es_digito(s[0])) return false;
    uint64_t v = 0;
    size_t n = 0;
    for (; es_digito(s[n]); n++) {
        if (n >= 10u) return false;
        v = v * 10u + (uint64_t)(s[n] - '0');
    }
    if (s[n] != '\0' || v > UINT32_MAX) return false;
    *out = (uint32_t)v;
    return true;
}

static config_motivo_t motivo_u32(const char *s, uint32_t min, uint32_t max, uint32_t *out) {
    if (s == NULL || s[0] == '\0') return CONFIG_MOTIVO_VACIO;
    if (!parse_u32(s, out)) return CONFIG_MOTIVO_FORMATO;
    return (*out < min || *out > max) ? CONFIG_MOTIVO_RANGO : CONFIG_MOTIVO_OK;
}

static config_motivo_t motivo_centi(const char *s, int32_t min, int32_t max, int32_t *out) {
    if (s == NULL || s[0] == '\0') return CONFIG_MOTIVO_VACIO;
    if (!config_parse_centi(s, out)) return CONFIG_MOTIVO_FORMATO;
    return (*out < min || *out > max) ? CONFIG_MOTIVO_RANGO : CONFIG_MOTIVO_OK;
}

static config_motivo_t rango_u32(uint32_t v, uint32_t min, uint32_t max) {
    return (v < min || v > max) ? CONFIG_MOTIVO_RANGO : CONFIG_MOTIVO_OK;
}

static config_motivo_t rango_centi(int32_t v, int32_t min, int32_t max) {
    return (v < min || v > max) ? CONFIG_MOTIVO_RANGO : CONFIG_MOTIVO_OK;
}

/* mín < máx y máx - mín >= ancho mínimo; el error se marca en el máximo. */
static void validar_par(config_motivo_t m[CONFIG_N_CAMPOS], config_campo_t cmin,
                        config_campo_t cmax, int32_t min, int32_t max, int32_t ancho) {
    if (m[cmin] != CONFIG_MOTIVO_OK || m[cmax] != CONFIG_MOTIVO_OK) return;
    if (min >= max) m[cmax] = CONFIG_MOTIVO_ORDEN;
    else if (max - min < ancho) m[cmax] = CONFIG_MOTIVO_ESTRECHO;
}

static void validar_orden(config_motivo_t m[CONFIG_N_CAMPOS], int32_t tmin, int32_t tmax,
                          int32_t hmin, int32_t hmax) {
    validar_par(m, CONFIG_CAMPO_TEMP_MIN, CONFIG_CAMPO_TEMP_MAX, tmin, tmax,
                CONFIG_ANCHO_MIN_TEMP_CENTI);
    validar_par(m, CONFIG_CAMPO_HR_MIN, CONFIG_CAMPO_HR_MAX, hmin, hmax,
                CONFIG_ANCHO_MIN_HR_CENTI);
}

static int contar_errores(const config_motivo_t m[CONFIG_N_CAMPOS]) {
    int n = 0;
    for (size_t i = 0; i < CONFIG_N_CAMPOS; i++)
        if (m[i] != CONFIG_MOTIVO_OK) n++;
    return n;
}

static bool copiar(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) return false;
    memcpy(dst, src, n + 1u);
    return true;
}

int config_validar(const config_t *c, config_motivo_t motivos[CONFIG_N_CAMPOS]) {
    config_motivo_t m[CONFIG_N_CAMPOS];
    if (c == NULL) return (int)CONFIG_N_CAMPOS;
    m[CONFIG_CAMPO_NOMBRE] = validar_nombre(c->nombre);
    m[CONFIG_CAMPO_PASSWORD] = hash_bien_formado(c->admin_hash) ? CONFIG_MOTIVO_OK
                                                                : CONFIG_MOTIVO_FORMATO;
    m[CONFIG_CAMPO_NTP] = validar_ntp(c->ntp);
    m[CONFIG_CAMPO_INTERVALO] =
        rango_u32(c->intervalo_registro_s, CONFIG_INTERVALO_MIN, CONFIG_INTERVALO_MAX);
    m[CONFIG_CAMPO_TEMP_MIN] =
        rango_centi(c->temp_min_centi, CONFIG_TEMP_MIN_CENTI, CONFIG_TEMP_MAX_CENTI);
    m[CONFIG_CAMPO_TEMP_MAX] =
        rango_centi(c->temp_max_centi, CONFIG_TEMP_MIN_CENTI, CONFIG_TEMP_MAX_CENTI);
    m[CONFIG_CAMPO_HR_MIN] = rango_centi(c->hr_min_centi, CONFIG_HR_MIN_CENTI, CONFIG_HR_MAX_CENTI);
    m[CONFIG_CAMPO_HR_MAX] = rango_centi(c->hr_max_centi, CONFIG_HR_MIN_CENTI, CONFIG_HR_MAX_CENTI);
    m[CONFIG_CAMPO_RETENCION] =
        rango_u32(c->retencion_dias, CONFIG_RETENCION_MIN, CONFIG_RETENCION_MAX);
    validar_orden(m, c->temp_min_centi, c->temp_max_centi, c->hr_min_centi, c->hr_max_centi);
    if (motivos != NULL) memcpy(motivos, m, sizeof(m));
    return contar_errores(m);
}

int config_desde_formulario(const config_form_t *f, config_t *out,
                            config_motivo_t motivos[CONFIG_N_CAMPOS]) {
    config_motivo_t m[CONFIG_N_CAMPOS];
    config_t c;
    memset(&c, 0, sizeof(c));
    m[CONFIG_CAMPO_NOMBRE] = validar_nombre(f->nombre);
    m[CONFIG_CAMPO_PASSWORD] = validar_password(f->password);
    m[CONFIG_CAMPO_NTP] = validar_ntp(f->ntp);
    m[CONFIG_CAMPO_INTERVALO] = motivo_u32(f->intervalo_registro_s, CONFIG_INTERVALO_MIN,
                                           CONFIG_INTERVALO_MAX, &c.intervalo_registro_s);
    m[CONFIG_CAMPO_TEMP_MIN] = motivo_centi(f->temp_min, CONFIG_TEMP_MIN_CENTI,
                                            CONFIG_TEMP_MAX_CENTI, &c.temp_min_centi);
    m[CONFIG_CAMPO_TEMP_MAX] = motivo_centi(f->temp_max, CONFIG_TEMP_MIN_CENTI,
                                            CONFIG_TEMP_MAX_CENTI, &c.temp_max_centi);
    m[CONFIG_CAMPO_HR_MIN] = motivo_centi(f->hr_min, CONFIG_HR_MIN_CENTI,
                                          CONFIG_HR_MAX_CENTI, &c.hr_min_centi);
    m[CONFIG_CAMPO_HR_MAX] = motivo_centi(f->hr_max, CONFIG_HR_MIN_CENTI,
                                          CONFIG_HR_MAX_CENTI, &c.hr_max_centi);
    m[CONFIG_CAMPO_RETENCION] = motivo_u32(f->retencion_dias, CONFIG_RETENCION_MIN,
                                           CONFIG_RETENCION_MAX, &c.retencion_dias);
    validar_orden(m, c.temp_min_centi, c.temp_max_centi, c.hr_min_centi, c.hr_max_centi);
    if (motivos != NULL) memcpy(motivos, m, sizeof(m));
    int errores = contar_errores(m);
    if (errores > 0) return errores;

    (void)copiar(c.nombre, sizeof(c.nombre), f->nombre);
    (void)copiar(c.ntp, sizeof(c.ntp), f->ntp != NULL ? f->ntp : "");
    if (config_hash_password(f->password, c.admin_hash) != 0) return -1;
    *out = c;
    return 0;
}

void config_sugerida(config_t *c) {
    memset(c, 0, sizeof(*c));
    (void)copiar(c->nombre, sizeof(c->nombre), "Gabinete documental");
    (void)copiar(c->ntp, sizeof(c->ntp), "pool.ntp.org");
    c->intervalo_registro_s = 60u;
    c->temp_min_centi = 1800;
    c->temp_max_centi = 2200;
    c->hr_min_centi = 3500;
    c->hr_max_centi = 5000;
    c->retencion_dias = 7u;
}

/* ---------- hash de la contrasena (PU-15) ---------- */

int config_hash_password(const char *password, char out[CONFIG_HASH_MAX]) {
    if (password == NULL) return -1;
    unsigned char azar[CONFIG_SAL_LEN];
    size_t leidos = 0;
    while (leidos < sizeof(azar)) {
        ssize_t r = getrandom(azar + leidos, sizeof(azar) - leidos, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        leidos += (size_t)r;
    }
    char ajuste[3u + CONFIG_SAL_LEN + 2u] = "$6$";
    for (size_t i = 0; i < CONFIG_SAL_LEN; i++)
        ajuste[3u + i] = ALFABETO_CRYPT[azar[i] % 64u];  /* 256 % 64 == 0: uniforme */
    ajuste[3u + CONFIG_SAL_LEN] = '$';
    ajuste[4u + CONFIG_SAL_LEN] = '\0';

    const char *h = crypt(password, ajuste);
    if (h == NULL || !hash_bien_formado(h) || !copiar(out, CONFIG_HASH_MAX, h)) return -1;
    return 0;
}

bool config_verificar_password(const char *password, const char *hash) {
    if (password == NULL || !hash_bien_formado(hash)) return false;
    const char *h = crypt(password, hash);
    if (h == NULL) return false;
    size_t n = strlen(hash);
    if (strlen(h) != n) return false;
    unsigned char dif = 0;
    for (size_t i = 0; i < n; i++) dif |= (unsigned char)(h[i] ^ hash[i]);
    return dif == 0u;
}

/* ---------- formato del archivo ---------- */

/* 1800 -> "18.0", 1825 -> "18.25", -50 -> "-0.5" (siempre con punto). */
static void formatear_centi(int32_t v, char *buf, size_t n) {
    const char *signo = v < 0 ? "-" : "";
    int32_t a = v < 0 ? -v : v;
    if (a % 10 == 0) snprintf(buf, n, "%s%d.%d", signo, (int)(a / 100), (int)((a % 100) / 10));
    else snprintf(buf, n, "%s%d.%02d", signo, (int)(a / 100), (int)(a % 100));
}

static int serializar(const config_t *c, char *buf, size_t n) {
    char tmin[16], tmax[16], hmin[16], hmax[16];
    formatear_centi(c->temp_min_centi, tmin, sizeof(tmin));
    formatear_centi(c->temp_max_centi, tmax, sizeof(tmax));
    formatear_centi(c->hr_min_centi, hmin, sizeof(hmin));
    formatear_centi(c->hr_max_centi, hmax, sizeof(hmax));
    int r = snprintf(buf, n,
                     "nombre=%s\nadmin_hash=%s\nntp=%s\nintervalo_registro_s=%u\n"
                     "temp_min=%s\ntemp_max=%s\nhr_min=%s\nhr_max=%s\nretencion_dias=%u\n",
                     c->nombre, c->admin_hash, c->ntp, (unsigned)c->intervalo_registro_s,
                     tmin, tmax, hmin, hmax, (unsigned)c->retencion_dias);
    return (r < 0 || (size_t)r >= n) ? -1 : r;
}

static bool ruta(char *buf, size_t n, const char *dir, const char *archivo) {
    int r = snprintf(buf, n, "%s/%s", dir, archivo);
    return r > 0 && (size_t)r < n;
}

static void poner_detalle(char *detalle, size_t n, const char *texto, unsigned linea) {
    if (detalle == NULL || n == 0u) return;
    if (linea > 0u) snprintf(detalle, n, "%s (linea %u)", texto, linea);
    else snprintf(detalle, n, "%s", texto);
}

static bool asignar(config_t *c, config_campo_t campo, const char *v) {
    switch (campo) {
    case CONFIG_CAMPO_NOMBRE: return copiar(c->nombre, sizeof(c->nombre), v);
    case CONFIG_CAMPO_PASSWORD: return copiar(c->admin_hash, sizeof(c->admin_hash), v);
    case CONFIG_CAMPO_NTP: return copiar(c->ntp, sizeof(c->ntp), v);
    case CONFIG_CAMPO_INTERVALO: return parse_u32(v, &c->intervalo_registro_s);
    case CONFIG_CAMPO_TEMP_MIN: return config_parse_centi(v, &c->temp_min_centi);
    case CONFIG_CAMPO_TEMP_MAX: return config_parse_centi(v, &c->temp_max_centi);
    case CONFIG_CAMPO_HR_MIN: return config_parse_centi(v, &c->hr_min_centi);
    case CONFIG_CAMPO_HR_MAX: return config_parse_centi(v, &c->hr_max_centi);
    case CONFIG_CAMPO_RETENCION: return parse_u32(v, &c->retencion_dias);
    default: return false;
    }
}

config_carga_t config_cargar(const char *dir, config_t *out, char *detalle, size_t detalle_n) {
    char path[512];
    if (!ruta(path, sizeof(path), dir, CONFIG_ARCHIVO)) {
        poner_detalle(detalle, detalle_n, "ruta demasiado larga", 0);
        return CONFIG_CARGA_ERROR_IO;
    }
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT) return CONFIG_CARGA_NO_EXISTE;
        poner_detalle(detalle, detalle_n, strerror(errno), 0);
        return CONFIG_CARGA_ERROR_IO;
    }
    char buf[CONFIG_ARCHIVO_MAX + 1u];
    size_t len = 0;
    for (;;) {
        ssize_t r = read(fd, buf + len, sizeof(buf) - 1u - len);
        if (r < 0) {
            if (errno == EINTR) continue;
            poner_detalle(detalle, detalle_n, strerror(errno), 0);
            close(fd);
            return CONFIG_CARGA_ERROR_IO;
        }
        if (r == 0) break;
        len += (size_t)r;
        if (len == sizeof(buf) - 1u) {
            close(fd);
            poner_detalle(detalle, detalle_n, "archivo demasiado grande", 0);
            return CONFIG_CARGA_INVALIDA;
        }
    }
    close(fd);
    buf[len] = '\0';

    /* Un archivo truncado no termina en '\n' o le faltan claves. */
    if (len == 0u || buf[len - 1u] != '\n') {
        poner_detalle(detalle, detalle_n, "archivo vacio o sin salto de linea final", 0);
        return CONFIG_CARGA_INVALIDA;
    }
    if (memchr(buf, '\0', len) != NULL) {
        poner_detalle(detalle, detalle_n, "byte nulo en el archivo", 0);
        return CONFIG_CARGA_INVALIDA;
    }

    config_t c;
    memset(&c, 0, sizeof(c));
    bool visto[CONFIG_N_CAMPOS] = { false };
    unsigned linea = 0;
    char *p = buf;
    while (*p != '\0') {
        linea++;
        char *fin = strchr(p, '\n');  /* existe: el archivo termina en '\n' */
        *fin = '\0';
        char *igual = strchr(p, '=');
        if (igual == NULL || strchr(p, '\r') != NULL) {
            poner_detalle(detalle, detalle_n, "linea sin formato clave=valor", linea);
            return CONFIG_CARGA_INVALIDA;
        }
        *igual = '\0';
        size_t campo = 0;
        while (campo < CONFIG_N_CAMPOS && strcmp(p, CLAVES[campo]) != 0) campo++;
        if (campo == CONFIG_N_CAMPOS) {
            poner_detalle(detalle, detalle_n, "clave desconocida", linea);
            return CONFIG_CARGA_INVALIDA;
        }
        if (visto[campo]) {
            poner_detalle(detalle, detalle_n, "clave repetida", linea);
            return CONFIG_CARGA_INVALIDA;
        }
        visto[campo] = true;
        if (!asignar(&c, (config_campo_t)campo, igual + 1)) {
            poner_detalle(detalle, detalle_n, "valor con formato invalido", linea);
            return CONFIG_CARGA_INVALIDA;
        }
        p = fin + 1;
    }
    for (size_t i = 0; i < CONFIG_N_CAMPOS; i++) {
        if (!visto[i]) {
            char texto[64];
            snprintf(texto, sizeof(texto), "falta la clave %s", CLAVES[i]);
            poner_detalle(detalle, detalle_n, texto, 0);
            return CONFIG_CARGA_INVALIDA;
        }
    }
    config_motivo_t m[CONFIG_N_CAMPOS];
    if (config_validar(&c, m) != 0) {
        for (size_t i = 0; i < CONFIG_N_CAMPOS; i++) {
            if (m[i] != CONFIG_MOTIVO_OK) {
                char texto[64];
                snprintf(texto, sizeof(texto), "valor no valido en %s", CLAVES[i]);
                poner_detalle(detalle, detalle_n, texto, 0);
                break;
            }
        }
        return CONFIG_CARGA_INVALIDA;
    }
    *out = c;
    return CONFIG_CARGA_OK;
}

/* ---------- guardado atomico (RF-43, PU-07) ---------- */

static long posix_escribir(int fd, const void *buf, size_t n) {
    ssize_t r;
    do { r = write(fd, buf, n); } while (r < 0 && errno == EINTR);
    return (long)r;
}

static int posix_sincronizar(int fd) { return fsync(fd); }

static int posix_renombrar(const char *desde, const char *hacia) { return rename(desde, hacia); }

/* "cuando proceda": un sistema de archivos que no admite fsync sobre un
 * directorio devuelve EINVAL; eso no se trata como error. */
static int posix_sincronizar_dir(const char *dir) {
    int fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return -1;
    int r = fsync(fd);
    if (r != 0 && errno == EINVAL) r = 0;
    close(fd);
    return r;
}

static const config_fs_ops_t OPS_POSIX = {
    posix_escribir, posix_sincronizar, posix_renombrar, posix_sincronizar_dir
};

const config_fs_ops_t *config_fs_ops_posix(void) { return &OPS_POSIX; }

int config_guardar(const char *dir, const config_t *c, const config_fs_ops_t *ops) {
    if (ops == NULL) ops = &OPS_POSIX;
    if (config_validar(c, NULL) != 0) return -1;

    char contenido[1024];
    int len = serializar(c, contenido, sizeof(contenido));
    char tmp[512], final[512];
    if (len < 0 || !ruta(tmp, sizeof(tmp), dir, CONFIG_ARCHIVO_TMP) ||
        !ruta(final, sizeof(final), dir, CONFIG_ARCHIVO))
        return -2;

    /* 0600: el archivo contiene el hash de la contrasena. */
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return -2;
    size_t escritos = 0;
    while (escritos < (size_t)len) {
        long r = ops->escribir(fd, contenido + escritos, (size_t)len - escritos);
        if (r <= 0) goto fallo_abierto;
        escritos += (size_t)r;
    }
    if (ops->sincronizar(fd) != 0) goto fallo_abierto;
    if (close(fd) != 0) goto fallo_cerrado;
    if (ops->renombrar(tmp, final) != 0) goto fallo_cerrado;
    /* Desde aqui <dir>/config ya es la configuracion nueva completa. */
    if (ops->sincronizar_dir(dir) != 0) return -2;
    return 0;

fallo_abierto:
    close(fd);
fallo_cerrado:
    unlink(tmp);
    return -2;
}
