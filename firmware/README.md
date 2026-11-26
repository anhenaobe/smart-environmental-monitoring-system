# Firmware STM32 — v0.2.0

Código común C/HAL para **NUCLEO-L432KC** (256 KiB flash, 64 KiB SRAM) y
**NUCLEO-L412KB** (128 KiB flash, 40 KiB SRAM). Mismo circuito y contrato UART;
no intercambiar binarios: cada target tiene su propio startup, macro del MCU
y script de enlace. El compañero Linux es `envmond`; el firmware no gestiona
umbrales, historial ni almacenamiento de configuración.

## 1. Preparación y programación

En el PC, instalar PlatformIO Core (`python3 -m pip install platformio==6.1.18` o
la extensión oficial de VS Code). En la raíz del repositorio:

```sh
# Compilar ambas placas (no las programa).
pio run -d firmware
# Compilar solamente la disponible.
pio run -d firmware -e nucleo_l432kc
pio run -d firmware -e nucleo_l412kb
# Programar UNA placa, tras comprobar la referencia impresa en ella.
pio run -d firmware -e nucleo_l432kc -t upload
# O, en caso de NUCLEO-L412KB:
pio run -d firmware -e nucleo_l412kb -t upload
```

El ELF y binario se generan en `firmware/.pio/build/<target>/`. Conectar
el USB Micro-B de ST-LINK para compilar/programar/depurar; **el puerto COM
virtual de ST-LINK no es la UART de datos** (ver pinout).

Las versiones de las herramientas se fijan en [platformio.ini](platformio.ini).
El firmware real reporta `fw=0.2.0` en `STA`.

## 2. Cableado (válido para las dos NUCLEO-32 MB1180)

| Señal | GPIO | Arduino / conector | Conexión |
|---|---|---|---|
| USART1 TX (AF7) | PA9 | D1 / CN3.1 | RX UART dedicada de Milk-V |
| USART1 RX (AF7) | PA10 | D0 / CN3.2 | TX UART dedicada de Milk-V |
| GND | — | CN3.4 o CN4.2 | GND común SBC–MCU–sensor |
| I²C1 SCL (AF4) | PB6 | D5 / CN3.8, A5 / CN4.7 si SB16 cerrado | SCL del SHT31 |
| I²C1 SDA (AF4) | PB7 | D4 / CN3.7, A4 / CN4.8 si SB18 cerrado | SDA del SHT31 |
| Sensor 3.3 V | — | +3V3 / CN4.14 | VDD del SHT31 |
| LED verde | PB3 | D13 / CN4.15 | LD3 a bordo (SB15 cerrado) |
| LED rojo | PB0 | D3 / CN3.6 | Ánodo LED vía 330 Ω, cátodo a GND |
| Botón factory reset | PA12 | D2 / CN3.5 | Pulsador normalmente abierto a GND, pull-up interno |
| Pulso de lazo (solo diagnóstico) | PB1 | D6 / CN3.9 | Analizador lógico, entrada alta impedancia |

- **No conectar 5 V ni unir las salidas de alimentación de ambas placas.**
  Alimentarlas por USB de forma independiente durante las pruebas. UART a
  3,3 V, TX/RX cruzados. Confirmar pinmux/niveles de Milk-V antes de conectar.
- Retirar el jumper de demostración D2–GND, si viene instalado. Si D2 queda
  a GND iniciará una solicitud de reset a los 10 s. El botón B1 del NUCLEO es
  **NRST** de MCU, no el botón de aplicación.
- Comprobar los puentes SB16/SB18 con continuidad (placa sin alimentación);
  conectan los GPIO I²C PB6/PB7 también a los pines A5/A4. Usar SCL/SDA por
  A5/A4 si los puentes están cerrados, o D5/D4 si están abiertos; nunca
  conectar otras funciones a esos cuatro pines. PA5 y PA6 quedan flotantes
  por código. No soldar/desoldar los puentes solo para este proyecto.
- Las líneas del SHT31 necesitan pull-up a **3,3 V** (p.ej. 4,7 kΩ si el
  breakout no las trae; confirmar con multímetro). Dirección I²C de 7 bits
  por defecto `0x44`. Si su placa usa `0x45`, añadir
  `-DSHT31_ADDR=0x45` a `build_flags` del ambiente escogido y recompilar.
  No es necesario modificar el código HAL.
- No ocupar la consola Linux para la UART de datos. USART1 en PA9/PA10
  deja libres PA2/PA15, reservados para el VCP de ST-LINK.

Fuentes oficiales de ST:
[UM1956, tablas 7–8 y 15–16, figura 8](https://www.st.com/resource/en/user_manual/um1956-stm32-nucleo32-boards-mb1180-stmicroelectronics.pdf),
[DS11451 STM32L432KC](https://www.st.com/resource/en/datasheet/stm32l432kc.pdf),
[DS12469 STM32L412KB](https://www.st.com/resource/en/datasheet/stm32l412kb.pdf).
[SHT3x-DIS, Sensirion (tablas 4–6, 8–10)](https://sensirion.com/media/documents/213E6A3B/63A5A569/Datasheet_SHT3x_DIS.pdf).
Las tablas de funciones alternativas de ambos MCU confirman AF4 (I²C1) y
AF7 (USART1).

## 3. Funciones y límites

- HSI16 como reloj del MCU y kernel I²C a 16 MHz (no depende de ST-LINK ni
  de cristal externo). SysTick de 1 ms, con vector fuerte `SysTick_Handler`
  que ejecuta `HAL_IncTick`.
- I²C estándar nominal conservador <100 kHz, `TIMINGR=0x30411417`:
  PRESC=3, SCLDEL=4, SDADEL=1, SCLH=20, SCLL=23. El valor se deriva del reloj
  de 16 MHz y márgenes de modo estándar; verificar nivel, tiempos de subida
  y frecuencia con analizador/oscilo antes de darlo por validado. El reloj
  observado puede variar por el RC y la carga del bus.
- Lectura SHT31 de alta repetibilidad cada 1 s: comando 0x2400 sin clock
  stretching, espera no bloqueante de 20 ms, lectura de 6 bytes, CRC-8 de
  temperatura y humedad y conversión a centésimas sin coma flotante.
- `ENV` transmite siempre una observación por segundo (incluyendo falla,
  sin números falsos en lecturas inválidas). Ante NACK/timeout/bus no
  disponible se reporta código 1 y se reinicializa el periférico para
  reintentar en la siguiente muestra; una falla física persistente puede
  impedir recuperar el bus. Los CRC inválidos y lecturas fuera del rango
  de operación −40…125 °C reportan estados 2 y 3.
- Recepción UART 115200 8N1 por interrupción, cola acotada de 128 bytes,
  parser XOR, re-sincronización por `$`; se descartan tramas inválidas.
  Se sigue sensando incluso si `SYS` no llega (>5 s).
- `STA` al arrancar y cada 10 s, con contadores, causa de reset, versión,
  tiempo máximo de lazo y pila mínima libre observada. El IWDG tiene
  timeout nominal aproximado de 4 s (LSI interno; tolerancia física
  pendiente). Se refresca exclusivamente al final del lazo principal.
- El máximo del lazo (en µs) usa DWT, con saturación a 99999;
  la pila se instrumenta con un patrón de 32 bits en una reserva de 4 KiB
  al tope de SRAM definida por `ld/`. El reporte es conservador: la
  porción ya activa al comenzar `main` cuenta como usada. No es un
  medidor de la RAM total ni una protección contra desbordamiento.
- Ambos LED representan la prioridad del [protocolo §5](../docs/protocolo-uart.md).
  Sin latido, la salida es doble destello rojo cada 2 s; con error de reset,
  tres destellos rojos por periodo de 2 s hasta una nueva pulsación. En
  los primeros 5 s de arranque hay una gracia para el primer latido.
- Botón con antirrebote de 50 ms. A partir de la pulsación estable y
  después de 10 s sostenidos, TX `BTN,RST`. Envía a lo sumo 5 solicitudes
  a intervalos de 1 s, espera ACK 1 s tras la quinta y reporta fallo si
  falta. Soltar antes de 10 s no envía nada; al iniciar una solicitud,
  sigue esperando ACK aunque se suelte el botón. Para un nuevo reset,
  liberar y volver a presionar; un ACK no permite disparar otro por la
  misma pulsación. Linux es responsable del borrado persistente.

## 4. Pruebas diagnósticas sin producto final

Los targets normales son los únicos que se compilan por defecto. Los otros
targets usan números de versión distintos para identificarlos inequívocamente.
Sustituir `nucleo_l432kc` por `nucleo_l412kb` si esa es la placa.

**Sensor sintético** (solo pruebas de UART, LED e integración):
```sh
pio run -d firmware -e nucleo_l432kc_synthetic -t upload
```
Reporta versión `0.2.90` y mediciones constantes **inventadas** 21,50 °C,
45,00 % HR. No valida el SHT31 ni las condiciones del gabinete. Mantener
la placa desconectada de cualquier instalación real y borrar historial
de prueba antes de evaluar el monitoreo.

**Watchdog (PI-06) e instrumentación (PI-16)**:
```sh
pio run -d firmware -e nucleo_l432kc_watchdog_test -t upload
```
Versión `0.2.91`. Sin debugger detenido, a los 15 s desde un arranque que
no fue IWDG bloquea voluntariamente el lazo; cerca de 4 s después debe
reiniciar y emitir `STA` con causa 2. No se repite el bloqueo después de
ese reset IWDG; tras 15 s del nuevo arranque llama a una función con
arreglo local volátil de 2048 bytes. Comparar pila libre de `STA` a los
10 y 20 s del nuevo arranque: debería disminuir. Medir el pulso PB1/D6
(abarca el lazo, salvo pequeños costos de GPIO y medición) con el
analizador lógico; comparar el máximo observado en un intervalo de
`STA`. No conectar LED o carga en D6 durante esa prueba.

**Reprogramar siempre con target normal después de estos diagnósticos.**

Para ensayar botón/led/CRC/conversión sin hardware, usar (desde la raíz)
`make -C tests check`; la máquina de estados está en
`src/env_control.c`, sin dependencias de HAL.

## 5. Checklist física

1. Identificar referencia NUCLEO y medir 3,3 V; retirar jumper D2–GND.
2. Compilar/programar el target normal, conectar LED rojo y pulsador.
3. Usar adaptador UART TTL de 3,3 V en PA9/PA10/GND (no RS232 ±12 V).
   Abrir terminal a 115200 8N1; observar `STA` de arranque y `ENV` a 1 Hz.
4. Conectar sensor, verificar lecturas y medir I²C. Desconectar/reconectar
   sensor; comprobar `ENV` con estado 1 y regreso a 0 en ≤2 s.
5. Conectar UART dedicada de Milk-V sólo después de identificarla y
   verificar cableado. Iniciar utilidad de prueba del lado SBC; comprobar
   `SYS`, `STA` y `ENV` conforme al protocolo.
6. Desconectar UART Linux (no alimentación MCU) y esperar >5 s; comprobar
   que continúa la medición y el LED da la señal de enlace interrumpido.
7. Mantener botón 9 s (no se transmite BTN) y ≥10 s (solicitud), probar
   el ACK simulado y la ausencia de ACK. No usar el botón con una
   instalación configurada de producción sin querer borrar sus datos.
8. Ejecutar watchdog e instrumentación con firmware diagnóstico; volver a
   flashear el normal. Realizar la prueba continua de 24 h del conjunto.

La compilación y pruebas unitarias no verifican el funcionamiento eléctrico:
queda pendiente programar y ejecutar en ambas NUCLEO, comprobar SHT31,
cableado Milk-V, formas de onda, reset real, precisión y prueba continua.
Los resultados formales se registran en la matriz del proyecto después de
ejecutar las pruebas físicas; no se declaran PASS anticipadamente.
