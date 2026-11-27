# Registro de asistencia IA #13: lógica de `envmond` (Fase A)

Fecha: 2026-11-27. Responsable: Mateo. Herramienta: Claude Code (Anthropic, modelo Claude Opus 5.5), en la rama `feat/envmond-logica` creada desde `origin/main` (`131498e`).

Este archivo resume la sesión y no es una exportación completa de la conversación. Continúa los registros #10 (parser) y #11 (protocolo v0.3).

## Contexto

Por el cierre del semestre, Mateo encargó a la herramienta implementar `envmond` en tres fases (A: lógica; B: servicio; C: web). Mateo supervisa, decide, revisa y verifica. Esta es la Fase A: módulos `config`, `logger` y `alarm` con las pruebas PU-07, PU-09, PU-11, PU-13, PU-14 y PU-15.

## Decisiones de Mateo

- **Ya tomadas antes de la sesión:** C11 con `-Wall -Wextra -Werror`; un proceso de un solo hilo con `poll`; CivetWeb para HTTP (Fase C); systemd para la supervisión (Fase B); marcador `reset.pending`; CMocka, `socat` y Valgrind; `ENV` cada 3 s.
- **Aprobó el plan de la Fase A** antes de que se escribiera código, con estas decisiones:
  - Rangos: temperatura −20 a 60 °C, HR 5 a 95 %, contraseña 8 a 64, nombre de 1 a 40 bytes con los caracteres propuestos y NTP vacío o nombre de host; mín < máx en temperatura y en HR.
  - Configuración corrupta: se trata como bring-up, no se borra y se registra en el log.
  - Desactivación de la alarma con exactamente la histéresis de RF-24 (0,5 °C y 2 puntos de HR), con la primera lectura en banda; explicar en la guía el escalón entero por la resolución del DHT11.
  - Racha interrumpida por `ENV` inválida o hueco de `seq`; `seq` repetido no cuenta; la alarma se conserva durante "sin datos". RF-25 tal como está (5 s) en una constante con nombre; Mateo propondrá a Andrew subirlo a ~7 s.
  - Formato CSV interno documentado en la guía, no en `interfaces.md`; el contrato con Juan Fernando será la API JSON de la Fase C.
  - Hora: intervalos alineados en UTC, archivo por fecha local, nuevo intervalo si el reloj retrocede y columna `hora_ok`, todo en esta fase.
  - `fsync` por fila y retención al arrancar y al cambiar el día.
  - Los cuatro fallos de la prueba de mutación.
- **Autorizó** documentar la decisión del hash en `requerimientos.md` §6 y en `interfaces.md` §5 (solo esa sección).
- **Decisiones posteriores a la primera revisión:**
  - Validación: exigir `máx − mín ≥ 2 × histéresis` de RF-24 (1,0 °C y 4 puntos de HR), con el motivo `CONFIG_MOTIVO_ESTRECHO`. Es una decisión de validación de Mateo, no una cifra de conservación.
  - Retención: conservar los CSV con fecha anterior a 2026-01-01 (filas sin hora válida).
  - Reasignar de Andrew a Juan Fernando, por disponibilidad de Andrew, la columna «Verifica» de PU-07, PU-09, PU-13, PU-14, PI-07, PI-14 y PB-02 (no PI-08, que implementa Juan Fernando). Registrarlo en `roles.md`.
- **Reviewer del PR:** Juan Fernando, para todas las pruebas de la fase.

## Trabajo de la herramienta

- **Lectura de las fuentes:** requerimientos, arquitectura §5 y §6, interfaces, protocolo v0.3, criterios de conservación, matriz y `linux/`. Señaló textos desactualizados frente a las decisiones (biblioteca HTTP, supervisor, hash) y que el verificador de cuatro de las pruebas es Andrew, no Juan Fernando.
- **Código** (solo archivos nuevos en `linux/src/` y `linux/tests/`, más `linux/Makefile`):
  - `config.{c,h}`: validación por campo con motivo, carga estricta, guardado con `config.tmp` + `fsync` + `rename` + `fsync` del directorio con operaciones inyectables, y hash `crypt` `$6$` con sal de `getrandom`.
  - `alarm.{c,h}`: rachas de 5 lecturas, histéresis y frescura de 5 s (`ALARM_FRESCURA_MS`), con reloj monotónico inyectado.
  - `logger.{c,h}`: agregación por tiempo, CSV diario con fecha local, `hora_ok`, reparación de una fila cortada y retención.
- **Pruebas CMocka:** 22 de `config` (PU-07, PU-14, PU-15), 17 de `alarm` (PU-13 y frescura de RF-25) y 12 de `logger` (PU-09, PU-11). Los cortes de energía se simulan con `fork` + `_exit` en cada paso del guardado; la retención, con 8 días simulados y cambio de mes; la fecha local, con la zona UTC−5.
- **Ejecución:** `make -C linux clean all check`, `test` y `valgrind` (0 errores, sin fugas), y `make -C tests check`, todos aprobados.
- **Prueba de mutación**, en una copia fuera del repositorio. Los cuatro mutantes fueron detectados:
  - `>` por `>=` en el umbral: falla 1 prueba;
  - racha de 4 en vez de 5: fallan 6;
  - sin `fsync` antes del `rename`: fallan 3;
  - sin filtro de estado ≠ 0 en el promedio: fallan 3.
- **Documentación:**
  - `docs/guia-envmond.md`: guía de estudio de la Fase A.
  - Decisión del hash en `requerimientos.md` §6 e `interfaces.md` §5.
  - Este registro, la fila 13 de `uso-ia.md` y la actividad en `roles.md`.
- **Hallazgos que la herramienta reporta, sin cambiar el contrato:**
  - Con `ENV` cada 3 s, una sola trama perdida produce "sin datos" momentáneo; lo cubre una prueba.
  - Si `máx − mín` es menor que el doble de la histéresis, la alarma nunca se apaga. Mateo decidió validarlo.
  - Cuando NTP corregía la hora, el archivo `1970-01-01.csv` se borraba por retención. Mateo decidió conservarlo.

## Pendiente

- Verificación formal de PU-07, PU-09, PU-11, PU-13, PU-14 y PU-15 por Juan Fernando. Los estados de la matriz no se cambiaron.
- Proponer a Andrew el umbral de RF-25 (~7 s).
- Los conteos de participaciones de `roles.md` («Andrew … 11 verificaciones; … Juan Fernando 9 y 32») no se recalcularon tras la reasignación.
- Confirmar en la Duo S que `libcrypt` admite `$6$`.
