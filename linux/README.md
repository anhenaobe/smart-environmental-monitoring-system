# Linux: servicio envmond y herramienta de bring-up

El software de la **Milk-V Duo S (SG2000)** se ejecuta en Linux; no se programa
con el binario STM32. El servicio definitivo `envmond` (responsable Mateo) debe
validar tramas, agregar muestras, guardar histórico/configuración, gestionar
alarmas/reset y servir HTTP según [interfaces](../docs/interfaces.md) y
[protocolo UART v0.2](../docs/protocolo-uart.md). Este directorio contiene por
ahora herramientas de prueba **temporales, independientes de ese servicio**:

- `uart_probe.c`: utilidad C de diagnóstico, UART raw 115200/8N1 sin flujo,
  latido `SYS,0` cada segundo, lectura/validación ENV, STA y BTN; permite
  probar otros estados y simular ACK explícitamente. No registra ni controla
  el clima, no instala servicios, no escribe configuración ni borra histórico
  o crea `reset.pending`.
- `milkv_readonly_probe.sh`: inventario de la Milk-V. No abre los tty ni cambia
  pinmux/GPIO/servicios.
- `Makefile` y [pruebas PTY](../tests/test_uart_virtual.py): compilación nativa
  Linux, sin dependencias adicionales fuera de GCC, make y Python 3 para tests.

## 1. Entorno real conocido y compilación

En la sesión de uCuPViewer del 5 de noviembre de 2026 se observó en la **Milk-V**:
`Linux duos 5.10.4-20240527-2+ riscv64`, Debian GNU/Linux forky/sid,
GCC 16.2.0-1 Debian, Python 3.14.7, systemd, cvi_pinmux y raíz ext4 en microSD.
La VM intermediaria era Debian 13 x86_64, no el sistema de la Milk-V.
En ese kernel estaba deshabilitado CONFIG_SPI; eso **no demuestra** disponibilidad
de una UART secundaria. Revalidar la imagen instalada antes de conectar.
[Registro histórico comprobable](https://github.com/anhenaobe/uCuPViewer/blob/main/docs/debug/milkv_usb_diagnostico.md).

Prueba offline en Linux/WSL/VM, desde la raíz de este repo:

```sh
make -C linux clean check
make -C tests check
```

Para ejecutar en la Milk-V, transferir las fuentes de **esta misma revisión**.
Si tiene SSH accesible por Ethernet/Wi-Fi, desde el PC Linux/VM (sin escribir
credenciales en comandos; sustituir el host y usuario por los verificados):

```sh
# Se conserva la estructura relativa requerida por el Makefile.
tar czf /tmp/sem-linux.tar.gz linux/Makefile linux/uart_probe.c \
    linux/milkv_readonly_probe.sh linux/README.md \
    firmware/include/env_protocol.h firmware/src/env_protocol.c
scp /tmp/sem-linux.tar.gz usuario@HOST_MILK:/tmp/
```

En la consola real de Milk-V (`uname -m` debe corresponder a su modo de arranque):

```sh
mkdir -p "$HOME/sem-uart-bringup"
tar xzf /tmp/sem-linux.tar.gz -C "$HOME/sem-uart-bringup"
cd "$HOME/sem-uart-bringup"
sh linux/milkv_readonly_probe.sh | tee /tmp/sem-milkv-inventory.txt
gcc -dumpmachine
make -C linux clean all CC=gcc
file linux/uart_probe          # en la imagen histórica: ELF64 RISC-V
./linux/uart_probe --help
```

Si no hay `make`, compilar nativamente con:
```sh
gcc -std=gnu11 -O2 -Wall -Wextra -Werror -Ifirmware/include \
    linux/uart_probe.c firmware/src/env_protocol.c -o linux/uart_probe
```

Si no hay conexión de red, transferir el mismo archivo al medio de almacenamiento
por una vía autorizada; no asumir que el firmware USB-NCM del fabricante funciona
en esta imagen Debian. Conservar la consola de administración (J3 pines 8/10),
que ya se recuperó en el proyecto anterior con el USB-TTL Prolific de 3,3 V
mediante VM Debian: [procedimiento histórico](https://github.com/anhenaobe/uCuPViewer/blob/main/docs/debug/milkv_procedimiento_recuperacion.md).
El ST-LINK de la NUCLEO es **otro dispositivo USB**; se usa para grabar/depurar
la STM32, no para acceder a la consola Linux. Si se usa otro adaptador USB-TTL
para pruebas del enlace, verificar que es de 3,3 V y distinguirlo del Prolific
de consola. No conectar su línea VCC.

La compilación cruzada es opcional: `make -C linux clean all CC=riscv64-linux-gnu-gcc`
solo si se dispone de ese compilador y sysroot/ABI compatibles con el sistema
Debian realmente instalado; comprobar `file` y `readelf -h -l linux/uart_probe`.
La compilación host no genera un binario ejecutable en RISC-V. Se prefiere GCC
nativo en la Milk-V histórica, que evita incompatibilidades de libc y linker.
Este trabajo verificó host y PTY, no una ejecución en la Milk-V ni un build cruzado.

## 2. Identificar UART de datos, sin tocar la consola

La [tabla oficial Duo S](https://milkv.io/docs/duo/getting-started/duos#header-j3)
confirma la ruta **candidata** que no reutiliza J3 23/24 empleados como GPIO SPI
en uCuPViewer:

| Señal | Milk-V Duo S (J3 a 3,3 V) | STM32 (USART dedicada, ver firmware) |
|---|---|---|
| TX Milk-V → RX STM32 | 11 / B11 / UART2_TX (VIVO_D10) | RX |
| TX STM32 → RX Milk-V | 13 / B12 / UART2_RX (VIVO_D9) | TX |
| Masa | 14 (GND) o 6/20/25 | GND |

No unir alimentación de placas, ni usar J4 (señales de **1,8 V**), ni los pines
J3 8/10 de consola UART0. No usar pin 9 como GND (en revisión V1.1 es GPIO bajo).
Retirar el cableado SPI anterior, con las placas apagadas, antes de conectar
esta configuración. Comprobar orientación física J3, continuidad y niveles.
No conectar aún la STM32 para probar pinmux o loopback de la Milk-V.

1. Ejecutar el inventario de §1. Buscar `serial@04160000` / dispositivo con
   dirección `4160000.serial` (UART2); la base corresponde al
   [DTS CV181x del SDK oficial](https://github.com/milkv-duo/duo-buildroot-sdk/blob/5b1a8a0bd87bed8eec38b1670c790a4041c50155/build/boards/default/dts/cv181x/cv181x_base.dtsi).
   Contrastar aliases, `/sys/class/tty/ttyS*/device/of_node` y `/proc/tty/driver/serial`.
   **No asumir que el nombre es /dev/ttyS2**. Una entrada ttyS sin hardware
   registrado o un DT deshabilitado no basta. Verificar que no sea consola en
   `/proc/cmdline`, `/sys/class/tty/console/active`, `/proc/consoles` y
   `/proc/device-tree/chosen/stdout-path`.
2. Confirmar que los pines realmente tienen su función UART2. El
   [pinlist oficial CV181x](https://github.com/milkv-duo/duo-buildroot-sdk/blob/5b1a8a0bd87bed8eec38b1670c790a4041c50155/linux_5.10/drivers/pinctrl/cvitek/cv181x_pinlist_swconfig.h)
   asigna VIVO_D10 = B11 y VIVO_D9 = B12; función UART2_TX/RX = selector 7.
   En la imagen Debian histórica se usó `cvi_pinmux -r VIVO_D8` y otros
   pines del SPI; para estos candidatos, `cvi_pinmux -r VIVO_D10` y
   `cvi_pinmux -r VIVO_D9` son los comandos de inspección propuestos, aún
   no ensayados físicamente. Si se dispone de `duo-pinmux`:
   `duo-pinmux -r B11` y `duo-pinmux -r B12`;
   [manual oficial pinmux](https://milkv.io/docs/duo/application-development/pinmux).
3. Si la función es diferente, registrar la configuración anterior e
   identificar cualquier periférico/driver que la use. **No ejecutar el
   siguiente cambio sin verificar que esos pines están libres, la UART2 está
   habilitada en el driver/DT, la STM32 desconectada y que se autoriza modificar
   pinmux.** Según la herramienta realmente instalada y su ayuda:
   `cvi_pinmux -w VIVO_D10/UART2_TX` y
   `cvi_pinmux -w VIVO_D9/UART2_RX`
   (sintaxis [fuente SG200x del SDK](https://github.com/milkv-duo/cvi-pinmux/blob/5b90da9f46fdc132e524f58f7f1bd96b028449c7/sg200x/cvi_pinmux.c)),
   o `duo-pinmux -w B11/UART2_TX` y `duo-pinmux -w B12/UART2_RX` si se usa el
   otro programa. Volver a leer el mux. No usar ambas herramientas a ciegas,
   no escribir registros con devmem, no asumir que el cambio persiste al reiniciar.
   Si la UART no está disponible en kernel/DT o no se puede identificar con
   confianza, detener aquí: documentar el bloqueo para decidir con Mateo,
   no reemplazar imagen, kernel ni ocupar consola.
4. Sustituir el ejemplo `PORT` por el dispositivo **verificado** en paso 1;
   conservar informe `sem-milkv-inventory.txt`. Comprobar dueño
   con `fuser -v "$PORT"` o `lsof "$PORT"` con permisos adecuados y
   revisar los servicios `envmond` y `serial-getty`. No matar procesos ni
   deshabilitar servicios automáticamente. Si ocupado, resolver con su
   responsable antes de iniciar. No usar `chmod 666`. El probe exige
   confirmación y rechaza consolas detectables por Linux, usa TIOCEXCL
   y restaura termios al salir (salvo desconexión física, SIGKILL, corte de
   energía); TIOCEXCL **no** prueba que ningún otro proceso tenía ya abierto
   el dispositivo, por tanto la comprobación previa sigue siendo necesaria.

## 3. Loopback físico local (separado del protocolo)

Con placas apagadas, STM32 y otros dispositivos **desconectados** de UART2 y
confirmado el mux UART2, unir únicamente J3 pin 11 (TX) con J3 pin 13 (RX)
mediante una resistencia serie de 1 kΩ para limitar una posible contienda
accidental durante el arranque (no sustituye verificar pinmux).
Encender la Milk-V, volver a confirmar mux y propietario del puerto: si el
reinicio restauró otras funciones, no continuar hasta resolverlo según §2.
Ejecutar desde
el repo (el siguiente nombre es **ejemplo**, sustituirlo por el verificado):

```sh
PORT=/dev/ttyS2
./linux/uart_probe --confirm-data-uart --loopback --duration 3 "$PORT"
```

Esperar `ECHO LPB,00000000`, `ECHO LPB,00000001`, `ECHO LPB,00000002`,
resumen `LOOPBACK tx=3 echo=3 other=0 invalid_frames=0` y código de salida 0.
`LPB` es una trama **solo de loopback** y no pertenece al contrato del producto.
Sin eco, errores, ecos duplicados o inesperados, el código de salida no es 0.
La prueba verifica ida/vuelta local, **no** la comunicación con STM32.
Apagar, quitar el jumper 11–13 y solo entonces conectar las dos placas según
tabla. Nunca unir dos salidas ni dejar ese puente durante la prueba real.

## 4. Integración STM32 ↔ Milk-V

Después de cargar el firmware del target correspondiente (ver [firmware](../firmware/README.md)),
sin otros procesos usando el tty:

```sh
./linux/uart_probe --confirm-data-uart --duration 20 "$PORT"
```

Esperar tramas `RX ENV,... [t=...ms]` (cada ~1 s); `STA` al inicio y cada
~10 s. Si inició la STM32 antes que el probe se puede perder la STA inicial,
pero debe verse la periódica. El resumen separa `env_ok` (mediciones válidas),
`env_sensor_error` (estado 1/2/3), STA, BTN, checksum/framing agregados
(`invalid_frames`), errores semánticos y tipos desconocidos. `sequence_jumps`
incluye saltos, duplicados o reinicios; no implica por sí solo pérdida de datos.
El timestamp monotónico indica recepción Linux, no instante exacto de adquisición.
La prueba acotada termina con error si no aparece ninguna ENV de formato válido
(incluso estados de error de sensor son válidos en el protocolo). Salida 0
**no equivale a PASS de integración**: revisar siempre los contadores.
El estado por defecto enviado es SYS,0 (bring-up).

Probar estados SYS simulados y desaparición/recuperación de latido, usando
ensayos independientes:

```sh
./linux/uart_probe --confirm-data-uart --sys-state 1 "$PORT"
# Ctrl+C; esperar más de 5 s en la STM32.
# Debe indicar Linux no disponible, sin detener las ENV.
# Reabrir el mismo tty de datos, no la consola:
./linux/uart_probe --confirm-data-uart --sys-state 2 --duration 20 "$PORT"
```

Verificar los LED según el protocolo; el probe no puede certificar por sí mismo
que el micro recibió SYS. Interrumpir solo el probe, no la alimentación ni
la consola. Registrar secuencia antes/después; durante la pausa no hay un
receptor que pueda certificar todas las ENV, por lo que observar TX STM32
pasivamente con analizador lógico. Los estados son forzados, no alarmas reales.

Ensayo de botón sin ACK: ejecutar sin bandera, presionar el botón de usuario
10 segundos; `BTN,RST` esperado, hasta cinco solicitudes a 1 s si no llega ACK.
Observar indicación de fallo y no reintento mientras se mantiene presionado;
soltar antes de una nueva prueba. El probe nunca borra datos.

Ensayo de recepción ACK (simulación explícita):
```sh
./linux/uart_probe --confirm-data-uart --simulate-reset-ack "$PORT"
```
Presionar 10 s; se observa BTN y se transmite ACK al recibir cada solicitud
correcta. El log advierte `SIMULATED ONLY; no reset.pending, no factory reset`.
La STM32 debe dejar de retransmitir tras ACK, según protocolo. **No es una prueba
de reset persistente**; solo se realiza sin `envmond` conectado. La transacción
durable `reset.pending`, borrado y reanudación requieren implementación de Mateo
y pruebas específicas. No marcar pruebas de factory reset completo por este modo.

### Ensayo de 10 min (PI-03)

Usar el modo de datos ficticios previsto en firmware, inequívocamente rotulado
como tal, sin registrar esas muestras como mediciones de conservación. Iniciar
la transmisión estable, abrir una captura de **600 s** con SYS,0:

```sh
./linux/uart_probe --confirm-data-uart --duration 600 "$PORT" \
    > /tmp/sem-600s-rx.log 2> /tmp/sem-600s-diag.log
cat /tmp/sem-600s-diag.log
grep -c '^RX ENV,' /tmp/sem-600s-rx.log
```

Criterio formal: contar exactamente 600 ENV transmitidas en el intervalo elegido
de 10 min a 1 Hz, con sus 600 secuencias consecutivas recibidas, únicamente
estado 0, 0 errores de trama/checksum, 0 malformadas/desconocidas y 0 saltos de
secuencia, contrastadas **contra TX por analizador lógico**. Debido a la fase
de arranque y pequeñas diferencias de relojes, una ventana de 600 s en Linux
puede caer justo sobre una frontera de trama: registrar la anomalía, no
redondear el conteo ni inventar muestras perdidas. Repetir sincronizando una
ventana de 600 ENV a partir de la primera trama completa (p. ej. captura de
602 s con inicio y fin definidos por la captura TX); verificar correspondencia
de esas mismas 600 tramas dentro de la ventana de referencia de 10 min.
Verificar 115200 baudios ±2 % mediante instrumento (112896–117504 baudios),
no solo leer termios.
Sin instrumento ni evidencia física completa no se puede marcar PI-03 PASS.

Al finalizar, cerrar el probe, conservar los logs de evidencia junto con fecha,
placa, firmware/commit, tty identificado y captura del analizador, y comprobar
que no quedó otro proceso usando el puerto. No cambiar configuración o servicios
para la entrega sin autorización.

## 5. Qué se verificó sin placas

`make -C linux check` usa un pseudo-terminal de Linux para verificar raw 8N1,
SYS periódico, parsing y rechazo, secuencia/wrap, ausencia de ACK por defecto,
ACK simulado, loopback, ausencia de ENV, desconexión y restauración de termios
en cierres normales. Esto no verifica el kernel/DT real, pines, voltajes,
transmisión eléctrica, cadencia de la STM32 ni adquisición del SHT31.
