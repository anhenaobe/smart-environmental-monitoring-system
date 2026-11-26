# Registro de asistencia IA #10: parser UART de envmond (PU-04, PU-05, PU-06)

Fecha: 2026-11-26. Responsable: Mateo. Herramienta: Claude Code (Anthropic, modelo Claude Opus 5.5), en la rama `feat/parser`.

Este archivo resume la sesión y reproduce lo esencial de las instrucciones. No es una exportación completa de la conversación. La evidencia técnica está en los commits `c164dd8`, `d85d60c` y `cd26ac9` de la rama.

## Instrucciones de Mateo (resumen)

- Implementar el parser del lado Linux con sus pruebas unitarias PU-04, PU-05 y PU-06, basándose solo en `docs/protocolo-uart.md` (única definición válida del protocolo), las filas PU-04/05/06 de `docs/matriz-verificacion.md`, `docs/interfaces.md` §2, RF-10, RF-11, RF-12 y RF-62 de `docs/requerimientos.md`, y el código de Andrew en `firmware/include/env_protocol.h` y `firmware/src/env_protocol.c`.
- Antes de escribir código, explicar si conviene reutilizar `env_protocol.c` y proponer la API de `linux/src/parser.h`, y esperar el visto bueno.
- C11 sin advertencias con `-std=c11 -Wall -Wextra -Werror`, sin memoria dinámica ni dependencias fuera de la libc, y sin E/S: el parser recibe bytes y devuelve tramas y contadores.
- Reglas de recepción de §2, decodificación de `ENV`, `STA` y `BTN,RST`, y pérdidas por `seq`. Ni el desbordamiento 99999999 → 0 ni el reinicio del STM32 cuentan como pérdida.
- Pruebas CMocka en `linux/tests/test_parser.c`, objetivo `test_parser` en `linux/Makefile` (incluido en `test` y `valgrind`) sin romper los objetivos de Andrew ni de Juan Fernando.
- Restricciones: no tocar `firmware/`, `linux/uart_probe.c`, `linux/src/metrics.*` ni `linux/tests/test_metrics.c`; no cambiar estados de la matriz; commits pequeños en español; ningún push sin confirmación.
- Regla del equipo: nadie verifica lo que implementó. Estas pruebas son de desarrollo; la verificación formal la hace Juan Fernando.

## Propuesta de diseño del asistente

- **Encuadre propio en `linux/src/parser.c`.** `env_parse()` devuelve 0 tanto por checksum malo como por formato inválido, y `env_rx_byte()` acumula todo en un solo contador `errors`. El protocolo exige contar por separado checksum, trama y desconocidas. Reutilizar ese código obligaría a cambiar la API de `firmware/`.
- **API.** `parser_init`, `parser_byte` (byte a byte), `parser_bloque` (por bloque, devuelve los bytes usados para no perder mensajes) y `parser_contadores`. Estructuras `parser_env_t`, `parser_sta_t` (con `fw` como texto) y `parser_msg_t`. `parser_contadores_t` usa los mismos nombres que el objeto `uart` de `/api/estado`.
- **Clasificación.** Un `$` a mitad de línea o 64 bytes sin `\n` cuentan como error de trama. Una estructura bien formada con XOR distinto es error de checksum. Un tipo distinto de `ENV`/`STA`/`BTN` (incluidos `SYS` y `ACK`) es desconocida. Un campo con formato inválido es error de trama.
- **Pérdidas.** `hueco = (seq − (último + 1)) mod 10^8`. El primer `ENV` fija la referencia y un `seq` repetido no suma. Un `STA` con `uptime_s` menor que el anterior marca reinicio y el siguiente `ENV` se cuenta desde 0.

## Decisiones de Mateo

1. **Encuadre propio**, probado con los mismos vectores de `protocolo-uart.md` que usa `tests/test_protocol.c`. Un comentario en `parser.c` apunta a la propuesta de un `env_parse_status()` (OK / CHECKSUM / TRAMA) para acordar con Andrew.
2. **Anchos de `STA`:** `uptime_s` y `err_i2c` de 1 a 10 dígitos con tope uint32; `causa_reset` de un dígito entre 0 y 4; `lazo_max_us` y `pila_libre_b` de 5 dígitos exactos.
3. **Retroceso de `seq` sin `STA` de reinicio:** se trata como reinicio y no suma a `perdidas`. Se cuenta en el campo interno `reinicios_inferidos`, consultable con `parser_reinicios_inferidos()`. No entra en `parser_contadores_t` ni en `/api/estado`, para no cambiar el contrato con Juan Fernando. Regla documentada en el código: un avance de más de medio rango (10^8 / 2) se interpreta como retroceso.
4. **`hr ≤ 10000`:** propuesta del asistente durante la implementación, aceptada. Más de 100,00 % no es una humedad posible, el firmware nunca lo genera y no cabe en `hr_centi` (`uint16_t`). Se rechaza como error de trama.
5. **No editar `protocolo-uart.md`.** Las ambigüedades (anchos de `STA`, retroceso de `seq`) y la propuesta de `env_parse_status()` quedan para hablarlas con Andrew.

## Resultados de verificación (desarrollo, en host)

- Checksums de los 10 vectores de `protocolo-uart.md` recalculados con un script: todos correctos.
- `make -C linux clean all check`: `uart_probe` compila sin advertencias y pasan las 7 pruebas de `test_uart_virtual.py`.
- `make -C linux test`: pasan las 19 pruebas de `test_parser` y las 2 de `test_metrics`.
  - PU-04: vectores del protocolo campo por campo, negativos, campos vacíos con estados 1 a 3, `STA` con anchos extremos, `BTN`, la sesión de §6 en un solo bloque y el límite exacto de 64 bytes.
  - PU-05: checksum malo, tramas truncadas, más de 64 bytes, basura antes de `$`, tipos desconocidos, 33 cargas con campos inválidos, 10 000 tramas aleatorias con cada contador comprobado, 200 000 bytes de ruido y argumentos nulos.
  - PU-06: huecos, repetidos, desbordamiento, reinicio con `STA` y retroceso sin `STA`.
- `make -C linux valgrind`: 0 errores y sin fugas en `test_metrics` y `test_parser`. Las 2 reservas que reporta `test_parser` son de CMocka; el parser no usa memoria dinámica.
- Durante el desarrollo, una prueba de PU-06 falló porque esperaba 5 tramas en lugar de 6. Se corrigió la expectativa de la prueba, no el parser.

## Prueba de mutación

Para comprobar que las pruebas detectan fallos reales, se introdujeron uno a uno cuatro errores deliberados en una copia temporal de `parser.c`, fuera del repositorio:

| Mutante | Resultado |
|---|---|
| Ignorar el reinicio por `STA` (no contar desde 0) | Detectado |
| Contar el checksum malo como error de trama | Detectado |
| Cortar las líneas a 63 bytes en lugar de 64 | Detectado |
| No inferir reinicio cuando `seq` retrocede | Detectado |

## Pendiente

- Verificación formal de PU-04, PU-05 y PU-06 por Juan Fernando, que también actualiza sus estados en la matriz.
- Acordar con Andrew los anchos de `STA`, el retroceso de `seq`, `hr ≤ 10000` y la propuesta de `env_parse_status()`.
