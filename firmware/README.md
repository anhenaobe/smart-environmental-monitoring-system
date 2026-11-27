# Firmware STM32 — v0.3.0 — DHT11

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
El firmware real reporta `fw=0.3.0` en `STA`.

## 2. Cableado (válido para las dos NUCLEO-32 MB1180)

| Señal | GPIO | Arduino / conector | Conexión |
|---|---|---|---|
| USART1 TX (AF7) | PA9 | D1 / CN3.1 | RX UART dedicada de Milk-V |
| USART1 RX (AF7) | PA10 | D0 / CN3.2 | TX UART dedicada de Milk-V |
| GND | — | CN3.4 o CN4.2 | GND común SBC–MCU–sensor |
| DHT11 DATA | PB5 | D11 / CN3.14 | DATA/S del DHT11, pull-up a 3,3 V |
| Sensor 3.3 V | — | +3V3 / CN4.14 | VDD del DHT11 (verificar versión) |
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
- **No usar los antiguos pines PB6/PB7 del SHT31**. DHT11 usa un bus
  digital de una sola línea, no I²C ni Dallas/Maxim 1-Wire. PB5 en modo
  GPIO deja disponible la UART y los pines de botón/LED/depuración.
- Para el DHT11 desnudo de cuatro pines, con rejilla hacia el observador
  y patillas hacia abajo, de izquierda a derecha: VDD, DATA, NC
  (sin conectar), GND. Si es un módulo de tres pines, seguir las
  etiquetas de la PCB; el orden puede variar.
- Conectar DATA a PB5 y una resistencia de **4,7 kΩ a 3,3 V** si el
  módulo no la incluye. Mantener el cable corto. La hoja ASAIR V1.3
  admite alimentación de 3,3 V; comprobar que el sensor/módulo real
  también la admite. Algunas versiones antiguas especifican mínimo
  3,5 V. **No conectar DATA a 5 V**: si necesita alimentación distinta
  de 3,3 V, revisar el pull-up del módulo y acordar adaptación de
  niveles antes de conectarlo. No poner otra resistencia en serie
  para "bajar" el voltaje.
- No ocupar la consola Linux para la UART de datos. USART1 en PA9/PA10
  deja libres PA2/PA15, reservados para el VCP de ST-LINK.

Fuentes oficiales de ST:
[UM1956, tablas 7–8 y 15–16, figura 8](https://www.st.com/resource/en/user_manual/um1956-stm32-nucleo32-boards-mb1180-stmicroelectronics.pdf),
[DS11451 STM32L432KC](https://www.st.com/resource/en/datasheet/stm32l432kc.pdf),
[DS12469 STM32L412KB](https://www.st.com/resource/en/datasheet/stm32l412kb.pdf).
[Manual DHT11 ASAIR/Aosong V1.3_20170331, §§5–7](https://www.aosong.com/userfiles/files/media/DHT11-V1_3%E8%AF%B4%E6%98%8E%E4%B9%A6%EF%BC%88%E8%AF%A6%E7%BB%86%E7%89%88%EF%BC%89.pdf).
Las tablas de pines de ambas NUCLEO confirman PB5/D11 y AF7 (USART1).

## 3. Funciones y límites

- HSI16 como reloj del MCU (no depende de ST-LINK ni de cristal externo).
  SysTick de 1 ms, con vector fuerte `SysTick_Handler` que ejecuta
  `HAL_IncTick`; DWT a 16 ciclos/µs.
- El DHT11 real se consulta **cada 3 s**, respetando el intervalo >2 s
  indicado en ASAIR V1.3. Se espera 2 s antes del primer intento tras
  arrancar; el sensor requiere ≥1 s de estabilización. No se hacen
  ráfagas compensatorias si se atrasa un ciclo.
- El MCU tira DATA a 0 durante 20 ms sin bloquear el lazo. Luego
  escribe 1 en la salida open-drain, lo que deja la línea en alta
  impedancia, y espera la respuesta (aprox. 83 µs baja, 87 µs alta)
  y los 40 bits MSB primero. Cada espera de flanco tiene un plazo
  acotado de hasta 120 µs; los pulsos altos de 0 y 1 (nominales
  23–27 y 68–74 µs) se discriminan con ventanas tolerantes que
  rechazan la zona intermedia. Se comprueba también el flanco final.
  Para detectar la subida de DATA tras liberarla se enmascaran las IRQ
  como máximo ~45 µs, menos de un carácter UART 8N1 a 115200 baudios.
  Durante la captura de respuesta y bits las interrupciones UART/SysTick
  siguen habilitadas; si una interferencia provoca tiempos inválidos,
  se rechaza la lectura y se reintenta a los 3 s. Se difieren las
  transmisiones bloqueantes de BTN/STA mientras el pulso START está
  a 0 (~20 ms) para no prolongarlo. Si se exceden los 30 ms se libera
  la línea y se reporta estado 1. No usar breakpoints dentro de la
  captura de pulsos para validar tiempos.
- Formato DHT11 V1.3: humedad entera, byte de fracción HR obligatorio
  cero, magnitud entera de temperatura, décimas de temperatura en
  los 7 bits inferiores del cuarto byte (bit 7 indica signo negativo),
  y checksum aditivo de los cuatro bytes previos módulo 256. Se admite
  el formato entero de las variantes antiguas (fracción 0). Otros
  formatos con fracción de humedad no se interpretan automáticamente.
  Se aceptan valores −20…60 °C y 5…95 % HR según la hoja V1.3, con
  formato UART preservado en centésimas, sin fingir resolución de 0,01.
  Revisiones antiguas tienen rango más estrecho; comprobar el módulo
  antes de adoptar el rango/precisión nominal de V1.3.
- Una sola trama `ENV` por intento real, al término de la consulta;
  el sensor devuelve su última medición interna, no necesariamente
  instantánea al comienzo del trigger. El MCU **no genera tramas
  intermedias con datos copiados ni repite lecturas en caché**.
  Estado 1 = falta de respuesta, timeout o temporización inválida;
  2 = checksum aditivo incorrecto; 3 = formato o valor inválido
  (checksum correcto). En los tres casos, campos de medición vacíos.
  El campo de `STA` llamado `err_i2c` se conserva por compatibilidad y
  cuenta los estados 1 y 2; el firmware ya no usa I²C.
  Una reconexión puede requerir un nuevo ciclo completo de 3 s
  adicional por el tiempo de estabilización. No se declara un tiempo
  de recuperación comprobado sin conexión física.
- El DHT11 es un sensor básico (precisión nominal ±2 °C / ±5 puntos
  porcentuales de HR para el manual V1.3, en condiciones de referencia);
  la compatibilidad con el perfil documental y la incertidumbre no
  quedan demostradas por el formato numérico ni por esta compilación.
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
Reporta versión `0.3.90` y mediciones constantes **inventadas** 21,50 °C,
45,00 % HR a 1 Hz. No valida el DHT11 ni las condiciones del gabinete. Mantener
la placa desconectada de cualquier instalación real y borrar historial
de prueba antes de evaluar el monitoreo.

**Watchdog (PI-06) e instrumentación (PI-16)**:
```sh
pio run -d firmware -e nucleo_l432kc_watchdog_test -t upload
```
Versión `0.3.91`. Sin debugger detenido, a los 15 s desde un arranque que
no fue IWDG bloquea voluntariamente el lazo; cerca de 4 s después debe
reiniciar y emitir `STA` con causa 2. No se repite el bloqueo después de
ese reset IWDG; tras 15 s del nuevo arranque llama a una función con
arreglo local volátil de 2048 bytes. Comparar pila libre de `STA` a los
10 y 20 s del nuevo arranque: debería disminuir. Medir el pulso PB1/D6
(abarca el lazo, salvo pequeños costos de GPIO y medición) con el
analizador lógico; comparar el máximo observado en un intervalo de
`STA`. No conectar LED o carga en D6 durante esa prueba.

**Reprogramar siempre con target normal después de estos diagnósticos.**

Para ensayar botón/LED, protocolo UART, checksum aditivo, formato y
clasificación de pulsos DHT11 sin hardware, usar (desde la raíz)
`make -C tests check`. La máquina de estados (`src/env_control.c`) y el
decodificador (`src/env_dht11.c`) no dependen de HAL.

## 5. Checklist física

1. Identificar referencia NUCLEO y medir 3,3 V; retirar jumper D2–GND.
2. Compilar/programar el target normal, conectar LED rojo y pulsador.
3. Usar adaptador UART TTL de 3,3 V en PA9/PA10/GND (no RS232 ±12 V).
   Abrir terminal a 115200 8N1; observar `STA` de arranque y `ENV` cada 3 s
   (la variante sintética envía cada segundo).
4. Conectar sensor a PB5, verificar lecturas y, en DATA, el pulso de
   inicio 18–30 ms, el ACK y los pulsos de datos del sensor. Verificar
   que el bus en reposo sea 3,3 V. Desconectar/reconectar el sensor
   (con el cable DATA libre de cortos); observar `ENV` estado 1 y
   posterior regreso a 0. Registrar el tiempo, considerando el intervalo
   de 3 s y el tiempo de estabilización del propio sensor.
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
queda pendiente programar y ejecutar en ambas NUCLEO, comprobar DHT11,
cableado Milk-V, formas de onda, reset real, precisión y prueba continua.
Los resultados formales se registran en la matriz del proyecto después de
ejecutar las pruebas físicas; no se declaran PASS anticipadamente.
