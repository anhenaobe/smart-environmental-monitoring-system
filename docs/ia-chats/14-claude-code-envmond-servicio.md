# Registro de asistencia IA #14: servicio `envmond` (Fase B)

Fecha: 2026-11-27. Responsable: Mateo. Herramienta: Claude Code (Anthropic, modelo Claude Opus 5.5), en la rama `feat/envmond-servicio` creada desde `origin/feat/envmond-logica` (`53376fa`), porque depende de la Fase A (PR #9).

Este archivo resume la sesión y no es una exportación completa de la conversación. Continúa el registro #13 (Fase A).

## Decisiones de Mateo

- **Rama:** crear la Fase B desde `feat/envmond-logica` y hacer merge de `main` cuando se integre el PR #9. En el primer commit, recalcular los conteos de `roles.md` tras la reasignación de verificaciones.
- **HTTP e hilos:** opción A, CivetWeb con sus propios hilos (`num_threads` mínimo) y un mutex corto. El lazo principal es el único dueño de la UART, la alarma, el historial y el reset; los manejadores HTTP solo leen una instantánea o piden aplicar configuración. Corregir `arquitectura.md` §5 y D4 explicando por qué se descartaron el servidor propio en `poll` y Mongoose.
- **UART:** al principio, `/dev/ttyS2` por defecto (UART2 de la Milk-V).
- **Cambio de hardware (durante la fase):** se perdió la Milk-V Duo S y la SBC pasa a ser una **Raspberry Pi Zero 2 W** con Raspberry Pi OS (Debian, systemd, aarch64). La UART por defecto pasa a `/dev/serial0` (configurable con `--uart` y en la unidad), y la guía documenta `dtoverlay=disable-bt` y la consola serie desactivada. No cambia nada más del diseño. `arquitectura.md` §3 y `hardware/` se actualizarán en un PR aparte, después de acordarlo con Andrew.
- **Servicio:** usuario `envmon` en el grupo `dialout`, `NoNewPrivileges` y `ProtectSystem=strict`, verificando con la documentación de systemd que pueda escribir en `/etc/envmon` y `/var/lib/envmon` (`CAP_NET_BIND_SERVICE` queda para la Fase C).
- **Reset:**
  - borra los archivos regulares de `/var/lib/envmon/` sin el directorio, y luego `config`;
  - un `BTN` repetido es idempotente y siempre recibe `ACK`;
  - `SYS,3` se mantiene 2 s.
- **Bring-up:** funcionan el parser, los contadores y `SYS,0`, sin historial ni alarmas.
- **Integración:** con el reloj real.
- **Mutación:** las cuatro propuestas.
- **PR:** contra `main`, aclarando que depende del PR #9; reviewer Juan Fernando.

## Trabajo de la herramienta

- **Conteos de `roles.md`:** recalculados desde la matriz con un script que reproduce los valores anteriores sobre `origin/main`. Resultado: Andrew 26/4, Mateo 31/10, Juan Fernando 9/39. Se comprobó que nadie verifica lo que implementa.
- **Contradicción detectada antes de programar:** "un solo hilo con `poll`" frente a CivetWeb, que usa sus propios hilos. Se presentaron las opciones A, B y C y Mateo eligió la A.
- **Código** (archivos nuevos en `linux/src/`, `linux/tests/` y `linux/systemd/`, más `linux/Makefile`, `linux/README.md` §6 y `.gitignore`):
  - `uart.{c,h}`: modo crudo 115200 8N1 no bloqueante y búfer de salida.
  - `reset.{c,h}`: marcador durable y borrado idempotente, con operaciones inyectables.
  - `servicio.{c,h}`: modos, `SYS` cada 1 s, `BTN`/`ACK`, `SYS,3` durante 2 s, reintento del borrado, `servicio_aplicar_config` e instantánea para `/api/estado`.
  - `envmond.c`: `poll`, `signalfd`, reapertura de la UART cada 5 s y `/dev/serial0` por defecto.
  - `envmond.service`, `sysusers.d`, `tmpfiles.d` y `make install`.
- **Comprobación de systemd** en `systemd.exec(5)` (systemd 257): `ConfigurationDirectory=` no cambia el dueño a `User=`, por eso se agregó `tmpfiles.d` además de `ReadWritePaths`. Con el límite de arranques por defecto, `RestartSec=1` dejaría el servicio detenido tras 5 caídas en 10 s, por eso se agregó `StartLimitIntervalSec=0`. `systemd-analyze verify` no reporta directivas desconocidas.
- **Pruebas:**
  - CMocka: 4 de `uart` (vectores y pseudoterminal), 5 de `reset` (PU-12: corte antes de cada una de las 13 operaciones, errores, idempotencia) y 13 de `servicio`.
  - Integración en host con `socat` y `sim_stm32`: 28 comprobaciones, y 38 bajo Valgrind (0 errores, sin fugas).
- **Errores propios corregidos durante la sesión** (de la herramienta, no del diseño):
  - `UART_RUTA_MAX` de 128 bytes era corto para rutas largas; pasó a 256.
  - El simulador imprimía la trama con `$` y checksum, y el script no la reconocía.
  - Una expectativa de PU-12 no contemplaba el último `fsync`.
  - El teardown de `test_reset` dejaba un archivo temporal.
- **Mutación**, con los cuatro mutantes detectados:
  - `ACK` antes del marcador: fallan 4 pruebas;
  - sin reset al arrancar: falla 1;
  - sin `SYS,2`: falla 1;
  - `config` antes que el historial: falla 1.
- **Documentación:** `arquitectura.md` (§5, tabla de archivos y D4), guía de estudio (Fase B), este registro, la fila 14 de `uso-ia.md` y `roles.md`.

## Pendiente

- Verificación formal de PU-12 por Juan Fernando. Los estados de la matriz no se cambiaron.
- En la Raspberry Pi Zero 2 W:
  - habilitar la UART principal y comprobar que `/dev/serial0` apunta a `ttyAMA0` (PI-01);
  - instalar la unidad y comprobar los permisos bajo `ProtectSystem=strict`;
  - hacer `kill -9` (PI-07);
  - correr `make integracion-larga` de 30 min (PI-11) en el host o en la placa.
- Hacer merge de `main` cuando se integre el PR #9.
