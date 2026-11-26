# Registro de asistencia IA #11: protocolo UART v0.3

Fecha: 2026-11-26. Responsable: Mateo. Herramienta: Claude Code (Anthropic, modelo Claude Opus 5.5), en la rama `docs/protocolo-v0.3` creada desde `origin/main`.

Este archivo resume la sesión y no es una exportación completa de la conversación. Continúa el registro #10 (parser de `envmond`, PR #5).

## Contexto

Andrew (lado STM32) aprobó por chat las interpretaciones del parser: «los tres puntos coinciden con el firmware. Tengamos en cuenta que seq vuelve a cero después de 99999999, para no confundir el desbordamiento natural con un reinicio. La propuesta de diferenciar errores en env_parse la hacemos después, manteniendo compatibilidad.»

## Decisiones de Mateo

- Acordar con Andrew los tres puntos del parser: anchos de `STA`, retroceso de `seq` sin `STA` tratado como reinicio, y `hr` ≤ 10000.
- Escribir la regla de desbordamiento de `seq` a pedido de Andrew: el paso 99999999 → 0 es un avance normal de 1 y no se confunde con un reinicio.
- Dejar el pendiente de `env_parse` (OK / CHECKSUM / TRAMA, manteniendo compatibilidad), como aceptó Andrew.
- Mantener la rama `docs/protocolo-v0.3` separada del PR #5, creada desde `origin/main`.
- Proponer la regla de `fw` (`MAJ.MIN.PAT`, máximo 11 caracteres), marcada en el documento como pendiente de confirmar por Andrew en la revisión del PR. Es el único punto que Andrew no ha aprobado todavía.
- Conservar «(borrador)» en la versión, no cambiar el formato de ninguna trama ni los ejemplos existentes, y no tocar código ni la matriz.

## Trabajo de la herramienta

- **Redacción** de los cambios en `docs/protocolo-uart.md`:
  - versión 0.3 con su línea de cambios;
  - tabla de clasificación de errores en §2, tomada de `linux/src/parser.c` en `origin/feat/parser` con `git show`;
  - `hr` ≤ 10000;
  - anchos de los campos de `STA` y regla propuesta para `fw`;
  - subsección «Detección de tramas perdidas y reinicios» con 5 reglas y 7 ejemplos;
  - sección §7 «Pendiente».
- **Revisión del firmware**, solo lectura de `firmware/src/main.c` y de los linker scripts. Lo que envía cumple todo lo acordado y la propuesta de `fw`:
  - `uptime_s` y `err_i2c` son `uint32_t`, enviados con `%08lu` y `%lu`;
  - `causa_reset` solo toma valores de 0 a 4;
  - `fw` es `0.2.0`, `0.2.90` o `0.2.91`;
  - `lazo_max_us` se satura en 99999;
  - `pila_libre_b` no supera 4096, porque la pila es de 4 KiB;
  - con estado 0, `hr` es ≤ 10000;
  - `seq` vuelve a 0 con `% 100000000u`.
- **Comprobación de checksums:** se recalcularon con un script las 10 tramas del documento y todas son correctas. No se agregaron tramas nuevas.
- **Comprobación de ejemplos:** los 7 casos de la tabla de `seq` y el límite de `hr` (10000 aceptado, 10001 rechazado) se reprodujeron con el parser de `origin/feat/parser`, compilado fuera del repositorio. Los resultados coinciden con el documento.

## Pendiente

- Revisión del PR por Andrew, incluida la confirmación de la regla de `fw`.
