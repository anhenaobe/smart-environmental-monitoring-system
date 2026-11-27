# Registro de ejecuciones de desarrollo

Los ensayos offline no sustituyen las pruebas de integración física ni la revisión cruzada. La matriz mantiene sus estados hasta contar con evidencia verificada por el integrante asignado.

| Fecha | Prueba | Ejecuta | Resultado | Evidencia / limitación |
|---|---|---|---|---|
| 2026-11-26 | Ensayos host CRC, conversión, tramas y parser (parcial PU-01/02/03) | Codex bajo solicitud de Andrew | Aprobada (offline) | `make -C tests check`: 16 alteraciones de bit del vector CRC; 65536 conversiones comparadas contra fórmulas; vectores ENV/STA/SYS/BTN/ACK; tamaño 64 bytes, recuperación y 100000 bytes pseudoaleatorios. La revisión formal de Juan Fernando sigue pendiente. |
| 2026-11-26 | Máquina de botón y LED (PU-10 parcial, PI-13 lógica) | Codex, host | Aprobada (offline) | `make -C tests check`: rebote <50 ms, pulsación corta/9 s/10 s, 5 envíos, ACK, rearme, prioridades y rollover de reloj. ASan/UBSan en ambos tests C sin hallazgos, con detección de fugas deshabilitada por incompatibilidad del entorno; no sustituye Valgrind ni pruebas físicas. |
| 2026-11-26 | UART simulada, probe de laboratorio (no PU-04/05/06 de envmond) | Codex, host x86-64 | Aprobada (offline) | `make -C linux check`: 7 pruebas PTY; validación 115200 8N1, loopback aislado, SYS, ACK opt-in, trama malformada/CRC, desbordamiento secuencia, desconexión, ausencia de ENV, protección de consola y restauración termios. No se abrió UART física. |
| 2026-11-26 | Compilación ARM (ambas Nucleo, 6 targets; PS-11 preliminar) | Codex | Aprobada (offline) | PlatformIO 6.1.18 / ststm32 19.5.0 / STM32CubeL4 1.18.1 / GCC ARM 7.2.1: seis SUCCESS, ELF ARM y símbolo fuerte SysTick_Handler (T) en todos. Tamaños abajo; sin flashear. |
| 2026-11-26 | Compilación/ejecución Linux en Milk-V | Codex | Pendiente (placa) | Probe compilado y ensayado x86-64; compilador cruzado RISC-V no disponible. Compilación nativa en Debian de Milk-V documentada; kernel/DT/pinmux/UART2 pendientes. |

### Compilación ARM: tamaños y variantes (desarrollo)

Valores en bytes de `arm-none-eabi-size` (text+data, data+bss incluyendo 4 KiB de stack y reserva de heap); no medición del consumo dinámico. El vector se incluye en el ELF; verificar también tamaño de imagen al flashear.

| Target | Flash text+data | RAM reservada data+bss | Resultado de build |
|---|---:|---:|---|
| nucleo_l432kc | 15300 | 5280 | SUCCESS |
| nucleo_l412kb | 15060 | 5280 | SUCCESS |
| nucleo_l432kc_synthetic | 13396 | 5272 | SUCCESS |
| nucleo_l412kb_synthetic | 13156 | 5272 | SUCCESS |
| nucleo_l432kc_watchdog_test | 15428 | 5288 | SUCCESS |
| nucleo_l412kb_watchdog_test | 15188 | 5288 | SUCCESS |

Las dos versiones normales quedan bajo RNF-13 en inspección de tamaño preliminar. Ninguna prueba de matriz se marca aprobada hasta revisión cruzada.

## Migración a DHT11 — firmware v0.3.0

La tabla anterior corresponde al firmware anterior v0.2.0 (SHT31), no al DHT11.

| Fecha | Prueba | Resultado | Evidencia / limitación |
|---|---|---|---|
| 2026-11-26 | Decodificación DHT11 y regresión firmware/UART | Aprobada (host) | `make -C tests check`: cuatro ejecutables; vectores DHT11 V1.3 con temperatura positiva/negativa, checksum aditivo (incluida reducción módulo 256), formato y rango, clasificación de anchos de pulso y vectores de tramas UART; regresión de protocolo/control. La temporización eléctrica no se probó. |
| 2026-11-26 | Compatibilidad con parser real de Mateo | Aprobada (host) | `tests/test_dht11_parser_compat`: DHT11 → centésimas → encuadre UART → parser de envmond (main tras PR #5); tramas válidas, fallas 1/2/3 y STA (contador/versión) aceptadas. No es una prueba de envmond completo. Suite CMocka del parser no ejecutada aquí por falta de cmocka.h. |
| 2026-11-26 | Probe UART — regresión PTY | Aprobada (host) | `make -C linux check`: siete pruebas, sin acceder a UART física. |
| 2026-11-26 | Compilación y enlace ARM directo (6 variantes) | Aprobada (offline) | GCC ARM 7.2.1, HAL STM32CubeL4 1.18.1, startup oficial y scripts de enlace del repositorio, `-Wall -Wextra -Werror`; imágenes ELF/BIN para normal, sintético y watchdog en ambas placas. No se flashearon. |

## Formato de evidencia para clase

Copiar una fila por sesión, guardar logs/fotos/capturas de analizador lógico junto a la evidencia del equipo y enlazarla. Identificar placa, revisión, commit, entorno, versión del binario, cableado y condiciones de ensayo.

| ID | Fecha | Commit y target | Equipo / instrumento | Ejecuta / verifica | Criterio y medición | Resultado | Evidencia |
|---|---|---|---|---|---|---|---|
| PI-01 | | | | | | Pendiente | |
| PI-02 | | | | | | Pendiente | |
| PI-03 | | | | | | Pendiente | |
| PI-04 | | | | | | Pendiente | |
| PI-05 | | | | | | Pendiente | |
| PI-06 | | | | | | Pendiente | |
| PI-12 | | | | | | Pendiente | |
| PI-13 | | | | | | Pendiente | |
| PI-16 | | | | | | Pendiente | |
| PF-01 | | | | | | Pendiente | |
| PF-04 | | | | | | Pendiente | |
