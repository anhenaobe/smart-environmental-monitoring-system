/*
 * Herramienta de prueba: escribe en <dir>/config una configuracion valida
 * con el perfil sugerido (18-22 °C, 35-50 % HR) usando config_guardar, para
 * arrancar envmond en modo normal durante la integracion en host.
 * uso: crear_config <dir> [temp_max_c]
 */
#include <stdio.h>
#include <stdlib.h>

#include "config.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "uso: crear_config <dir> [temp_max_c]\n");
        return 2;
    }
    config_t c;
    config_sugerida(&c);
    if (argc > 2) c.temp_max_centi = atoi(argv[2]) * 100;
    if (config_hash_password("clave-segura-1", c.admin_hash) != 0 ||
        config_guardar(argv[1], &c, NULL) != 0) {
        fprintf(stderr, "crear_config: no se pudo guardar en %s\n", argv[1]);
        return 1;
    }
    return 0;
}
