# Bring-up v0.2 · STM32 + Milk-V Duo S

**Objetivo:** dejar el subsistema de adquisición y el enlace bidireccional listos para ensayar en clase con cualquiera de las dos Nucleo. Los ensayos sobre host no demuestran que las placas, sus pines, el sensor ni las temporizaciones físicas funcionen. No integrar en `main` hasta revisión y evidencia.

## 1. Identificar las placas antes de energizar

- STM32: comprobar referencia **NUCLEO-L432KC** o **NUCLEO-L412KB** impresa en la tarjeta y elegir exclusivamente su target. Estas instrucciones suponen Nucleo-32 con ST-LINK, no el MCU desnudo.
- Milk-V: la evidencia de [uCuPViewer, 5/11/2026](https://github.com/anhenaobe/uCuPViewer/blob/737c89fece8e1244e8d76db4acdbf6a78dbf1f58/docs/debug/milkv_usb_diagnostico.md) identifica la placa real como `Linux duos 5.10.4-20240527-2+ riscv64`, Debian forky/sid, GCC 16.2.0-1, systemd y `cvi_pinmux`. No era la VM Debian 13. Revalidar imagen/arquitectura: no reinstalar ni modificar el proyecto anterior.
- SHT31 a 3,3 V, pull-ups I²C a 3,3 V, dirección 0x44 por defecto. Confirmar módulo y puentes.
- Cablear con fuentes desconectadas, alimentación independiente y GND común, sin unir 3V3 ni 5V entre placas. UART TTL de 3,3 V, **no** RS-232. J4 de la Duo S utiliza 1,8 V: no utilizarlo.

## 2. Compilar y verificar offline

En el PC con GCC, Make, Python 3 y PlatformIO:

```sh
make -C tests check
make -C linux check
python -m pip install platformio==6.1.18
pio run -d firmware -e nucleo_l432kc
pio run -d firmware -e nucleo_l412kb
```

Los targets, dirección del SHT31, pinout y variantes especiales se detallan en [firmware/README.md](../firmware/README.md). Para pruebas físicas, cargar **solo** el binario de la placa identificada; primero modo sintético para aislar la UART del I²C. No instalar una variante de prueba como firmware final: utilizar el target normal al finalizar.

## 3. Verificar primero la Milk-V (PI-01)

Desde una sesión Linux independiente (SSH o consola UART0 recuperada por Prolific, como en el proyecto anterior), copiar solamente el repositorio de este monitor a un directorio nuevo y ejecutar:

```sh
sh linux/milkv_readonly_probe.sh
make -C linux clean all
file linux/uart_probe
```

Consultar [linux/README.md](../linux/README.md) para identificar UART2, revisar dueño del dispositivo y pinmux, hacer loopback físico TX→RX **sin STM32 conectada**, y confirmar compilación nativa RISC-V. La UART2 por J3 11/13 es candidata, no un hecho verificado del kernel instalado. No modificar pinmux ni getty sin comprender el mapeo. No usar UART0 como puerto de datos.

## 4. Ensayos por etapas

| Paso | Qué hace Andrew | Qué observar / guardar |
|---|---|---|
| PI-02 parcial | Programar target normal y alimentar STM32 con sensor; conectar UART al adaptador TTL 3,3 V separado de la consola de depuración | Cada segundo `ENV,...,0` con lecturas plausibles; comparar contra termohigrómetro. `STA` al arranque y cada 10 s. |
| PI-03 | Programar target sintético y conectar la UART dedicada ya verificada de Milk-V | 600 muestras ENV consecutivas a 1 Hz sin errores ni saltos, medir baudrate ±2 % con analizador. No almacenar mediciones ficticias en el servicio real. |
| PI-04 / PI-13 | Ejecutar el probe con SYS 0, 1, 2, 3 en sesiones distintas y observar LED | Patrones del §5 del protocolo, medir 1 Hz y 5 Hz ±10 %. |
| PI-05 parcial | Volver al firmware físico; desconectar/reconectar sensor de modo controlado | En ≤2 s estado 1 con campos vacíos, luego retorno a estado 0 en ≤2 s y uptime sin reinicio. La parte HTTP exige envmond real. |
| PI-12 parcial | Detener solo el probe; observar LED y TX con analizador/terminal pasiva | Ausencia de latido en ≤6 s, señalización sin Linux (si sensor sano), STM32 continúa ENV durante 2 min. Volver a lanzar probe. |
| PF-01 / PF-04 parcial | Probar pulsador externo 3 s, 9 s y 10 s sostenidos; probe sin ACK | Ningún BTN en las dos primeras; cinco BTN separados por 1 s y señal de fallo sin ACK. Soltar para rearmar. |
| PF-02 parcial | Ejecutar probe con ACK simulado habilitado, en montaje de laboratorio aislado | BTN y ACK de prueba; comprobar cese de reintentos. **No** hay marcador persistente ni borrado: la prueba real de factory reset queda pendiente con envmond. |
| PI-06 | Programar exclusivamente la variante de prueba de watchdog indicada en firmware/README | Reinicio tras bloqueo intencional y `STA` causa 2. Reprogramar target normal al terminar. |
| PI-16 | Medir tiempo de lazo y verificar la marca de pila mediante debug/analizador | Comparar métricas de STA con medición real; no asumir precisión por compilación. |

## 5. Criterio de entrega de esta etapa

El monitor se considera candidato a integración tras compilar ambos targets, registrar la placa real utilizada, verificar sensor, UART, latidos, LED, botón y watchdog bajo supervisión física. Las alarmas ambientales configurables, persistencia del historial, interfaz HTTP y reset persistente corresponden a `envmond`, no al firmware ni al probe de laboratorio. Los estados de la [matriz](matriz-verificacion.md) siguen pendientes hasta contar con verificación independiente.

Anotar resultados y enlazar evidencias en [tests/registro.md](../tests/registro.md). Si algo falla, guardar la salida de la prueba antes de cambiar firmware o cableado.
