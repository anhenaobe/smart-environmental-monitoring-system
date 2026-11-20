# Bring-up v0.1: firmware y diagnóstico UART para clase

**Estado:** versión inicial sin prueba física ni compilación ARM. El código se guarda en la rama `feature/bringup-v0-offline`, separado de `main` hasta verificarlo con las placas. No sustituye el `envmond` asignado a Mateo.

**STM32:** PlatformIO para NUCLEO-L432KC; inicialización HSI16 y SysTick explícito, USART1 PA9/PA10 (115200 8N1), I²C1 PB6/PB7, lectura SHT31 con CRC y tramas `ENV`. El valor `I2C1_TIMING_PROVISIONAL` se debe recalcular y verificar antes de las pruebas reales. No están implementados aún el watchdog, los LED de producto, `STA` completa ni el botón de factory reset.

**Milk-V:** `linux/uart_probe.c` es una utilidad temporal, no el servidor definitivo. Lee tramas y transmite `SYS,0`; `linux/milkv_readonly_probe.sh` inspecciona la placa sin modificar pinmux ni servicios. La UART secundaria de la Milk-V **no ha sido identificada ni comprobada**. No ocupar la consola de Linux como UART de datos.

**Pruebas offline (Linux/WSL con GCC):**

```bash
make -C tests check
make -C linux
python3 tests/test_uart_virtual.py
```

Estas pruebas se ejecutaron en host y pasaron. No demuestran que STM32, SHT31 o Milk-V estén funcionando. La compilación ARM sigue pendiente:

```bash
pio run -d firmware -e nucleo_l432kc
```

**Prueba en clase:** identificar imagen/kernel/pinmux de Milk-V, verificar vector `SysTick_Handler` del ELF, comprobar GPIO de 3,3 V y GND común, conectar TX/RX cruzados, medir el I²C y recibir `ENV`/`SYS` entre las placas. No realizar `upload` sin revisar placa, cableado y configuración I²C. Los resultados de la matriz permanecen pendientes.

El código fue preparado con asistencia de ChatGPT a partir de las lecciones de `uCuPViewer`, y su verificación hasta ahora se limita al host.