# Matriz de verificación

Versión: 0.1 (borrador)

**Tipos:** U = unitaria, I = integración, S = sistema / aceptación.
**Estado:** Pendiente · En curso · Aprobada · Falló.

Regla del equipo: **quien implementa no verifica su propia funcionalidad.** Asignación según [roles.md](roles.md); *Int. N* = Integrante N, hasta registrar los nombres.

## Pruebas unitarias

| ID | Req. | Prueba | Criterio de aceptación | Implementa | Verifica | Herramienta | Estado |
|---|---|---|---|---|---|---|---|
| PU-01 | RF-02 | CRC-8 del SHT31 (polinomio 0x31, init 0xFF) con los vectores de la hoja de datos | `0xBEEF` → `0x92`; los vectores corruptos se rechazan | Andrew | Int. 3 | Unity (host) | Pendiente |
| PU-02 | RF-01 | Conversión de los valores crudos a centésimas de °C y % HR | Valores límite (0, 65535) y valores típicos dentro de ±1 LSB | Andrew | Int. 3 | Unity (host) | Pendiente |
| PU-03 | RF-10 | Generación de la trama `ENV` en el firmware | Coincide byte a byte con la referencia, incluidos el checksum y los valores negativos | Andrew | Int. 3 | Unity (host) | Pendiente |
| PU-04 | RF-11 | Parser de Linux: tramas válidas | Todos los tipos se decodifican correctamente | Andrew | Int. 3 | CMocka/Unity | Pendiente |
| PU-05 | RF-11 | Parser de Linux: checksum malo, trama truncada, > 64 bytes, basura antes de `$`, tipo desconocido | Se rechazan, el contador correcto aumenta y no hay fallos | Andrew | Int. 3 | CMocka/Unity + Valgrind | Pendiente |
| PU-06 | RF-12 | Detección de tramas perdidas por `seq`, incluido el desbordamiento 99999999 → 0 | El número de tramas perdidas es correcto; el desbordamiento no cuenta como pérdida | Andrew | Int. 3 | CMocka/Unity | Pendiente |
| PU-07 | RF-43 | Guardado atómico de la configuración | Simular la interrupción antes del `rename`: queda la configuración anterior, intacta | Int. 2 | Andrew | CMocka/Unity | Pendiente |
| PU-08 | RF-60–61 | Lectura de métricas desde archivos `/proc` de muestra | Los valores calculados coinciden con los esperados para los archivos de prueba | Int. 3 | Int. 2 | CMocka/Unity | Pendiente |
| PU-09 | RF-21 | Agregación por minuto (promedio, mínimo, máximo) | Coincide con un cálculo de referencia; ignora las lecturas con estado ≠ 0 | Int. 2 | Andrew | CMocka/Unity | Pendiente |
| PU-10 | RF-51 | Máquina de estados del botón: antirrebote, sostenido 10 s, soltado antes de tiempo | `BTN,RST` solo a los 10 s; soltar a los 9 s no lo genera | Andrew | Int. 3 | Unity (host) | Pendiente |

## Pruebas de integración

| ID | Req. | Prueba | Criterio de aceptación | Implementa | Verifica | Herramienta | Estado |
|---|---|---|---|---|---|---|---|
| PI-01 | — | Bring-up de la Milk-V: arranque desde la microSD, acceso a la consola, Ethernet | Shell accesible; `ping` al equipo responde | Andrew | Int. 4 | Terminal serie, `ping` | Pendiente |
| PI-02 | RF-01 | STM32 + sensor: lectura y salida por la consola de depuración | Valores plausibles; coinciden con el instrumento de referencia | Andrew | Int. 4 | ST-LINK, terminal serie, instrumento de referencia | Pendiente |
| PI-03 | RF-10 | STM32 → Linux con datos ficticios | Linux recibe 100 % de las tramas durante 10 min | Andrew | Int. 4 | `cat`/`stty` en Linux, analizador lógico | Pendiente |
| PI-04 | RF-13 | Linux → STM32: latido `SYS` | El LED cambia según `SYS`; cortar el latido produce el patrón "sin Linux" en ≤ 6 s | Andrew | Int. 4 | Terminal serie, observación | Pendiente |
| PI-05 | RF-03, RF-04 | Desconexión y reconexión del sensor en caliente | Estado 1 en ≤ 2 s; lecturas válidas en ≤ 2 s tras reconectar; sin reinicio del STM32 | Andrew | Int. 4 | Terminal serie | Pendiente |
| PI-06 | RF-05 | Watchdog: forzar un bloqueo del lazo principal | El STM32 se reinicia y `STA` reporta `causa_reset = 2` | Andrew | Int. 4 | ST-LINK (firmware de prueba), terminal serie | Pendiente |
| PI-07 | RF-20 | Arranque automático del servicio | Tras reiniciar, el servicio recibe tramas sin intervención | Andrew | Int. 4 | `ps`, registros del servicio | Pendiente |
| PI-08 | RF-30 | Interfaz web: valores actuales e historial | Los valores web coinciden con las tramas en ≤ 2 s | Int. 2 | Andrew | Navegador, `curl` | Pendiente |
| PI-09 | RF-23 | Alarma por umbral | Forzar el umbral (calentar el sensor con la mano): alarma en web y LED | Andrew + Int. 2 | Int. 4 | Navegador, observación | Pendiente |
| PI-10 | RF-60–63 | Página de diagnóstico | CPU/memoria coinciden con `top`/`free`; bytes de red con `/proc/net/dev`; puertos con `netstat`/`ss` | Int. 3 | Int. 2 | `top`, `free`, `ss`, navegador | Pendiente |
| PI-11 | RNF-03 | Fugas de memoria del servicio | Valgrind sin "definitely lost" tras 30 min con un puerto serie virtual | Andrew | Int. 3 | Valgrind + `socat` (host) | Pendiente |

## Pruebas de bring-up

| ID | Req. | Prueba | Criterio de aceptación | Implementa | Verifica | Herramienta | Estado |
|---|---|---|---|---|---|---|---|
| PB-01 | RF-40 | Arranque sin configuración | LED verde a 1 Hz; la web redirige a la página de configuración | Andrew + Int. 2 | Int. 4 | Observación, navegador | Pendiente |
| PB-02 | RF-42 | Datos inválidos en la configuración | Se rechazan con un mensaje claro; no se guarda nada | Int. 2 | Andrew | Navegador | Pendiente |
| PB-03 | RF-41, RF-44 | Configuración completa | Pasa a funcionamiento normal (LED verde fijo) sin reiniciar manualmente | Andrew + Int. 2 | Int. 4 | Navegador, observación | Pendiente |
| PB-04 | RF-43 | Corte de energía durante el guardado (repetir 10 veces) | El equipo siempre arranca en un estado coherente | Int. 2 | Int. 4 | Desconexión física | Pendiente |
| PB-05 | RF-45 | Bring-up hecho por una persona ajena al equipo usando solo el manual | Lo completa sin ayuda; se anotan los puntos confusos | Int. 4 | Persona externa | Manual de usuario | Pendiente |

## Pruebas de factory reset

| ID | Req. | Prueba | Criterio de aceptación | Implementa | Verifica | Herramienta | Estado |
|---|---|---|---|---|---|---|---|
| PF-01 | RF-51 | Botón presionado menos de 10 s | No hay reset; los LED vuelven a su estado previo | Andrew | Int. 4 | Observación, terminal serie | Pendiente |
| PF-02 | RF-50, RF-52 | Botón presionado 10 s | `BTN,RST` → `ACK,RST` → el equipo vuelve a bring-up; sin configuración ni registros | Andrew | Int. 4 | Terminal serie, navegador | Pendiente |
| PF-03 | RF-53 | Corte de energía en medio del reset (repetir 10 veces) | El equipo siempre arranca configurado o en bring-up, nunca en un estado intermedio | Andrew | Int. 4 | Desconexión física | Pendiente |
| PF-04 | RF-54 | Reset con el servicio Linux detenido | El STM32 muestra el patrón "sin ACK" | Andrew | Int. 4 | Observación | Pendiente |
| PF-05 | RF-40–52 | Ciclo completo: bring-up → uso → reset → bring-up | Funciona igual que en el primer arranque | Andrew + Int. 2 | Int. 4 | Navegador, observación | Pendiente |

## Pruebas de sistema

| ID | Req. | Prueba | Criterio de aceptación | Implementa | Verifica | Herramienta | Estado |
|---|---|---|---|---|---|---|---|
| PS-01 | RNF-04 | Funcionamiento continuo durante 24 h | Sin reinicios inesperados; tramas perdidas < 0,1 %; memoria estable | Todos | Int. 4 | Página de diagnóstico, registros | Pendiente |
| PS-02 | RNF-07 | Exactitud frente al instrumento de referencia | Diferencia dentro de la tolerancia en 3 puntos (ambiente, calor, humedad) | Andrew | Int. 4 | Instrumento de referencia | Pendiente |
| PS-03 | RNF-06 | Sesgo térmico por la carcasa | Temperatura con carcasa vs. sin carcasa tras 1 h: diferencia < 0,5 °C | Int. 4 | Andrew | Instrumento de referencia | Pendiente |
| PS-04 | RNF-08 | Escrituras en el almacenamiento | ≤ 1 escritura/min | Int. 2 | Int. 3 | `/proc/diskstats`, SystemTap si está disponible | Pendiente |
