# Firmware STM32 (NUCLEO-L432KC)

Responsable: subsistema embebido y hardware.

## Funciones

- Lectura del SHT31 por I²C (single shot, alta repetibilidad) cada 1 s, con verificación de CRC-8 (polinomio 0x31, init 0xFF).
- Detección de fallas del sensor y reintento sin reiniciar.
- Envío de `ENV` (1 s) y `STA` (10 s) por UART según [docs/protocolo-uart.md](../docs/protocolo-uart.md).
- Recepción de `SYS` (latido) y `ACK`.
- LED de estado (verde/rojo) y botón de factory reset (sostenido 10 s, con antirrebote).
- Watchdog independiente (IWDG); causa de reset leída de `RCC_CSR`.

## Estructura prevista

La lógica pura (CRC, conversión, armado de tramas, máquina del botón) va en módulos sin dependencias de HAL, para poder probarla en el host con Unity (PU-01, PU-02, PU-03, PU-10).

## Pines

_Por definir: I²C, UART hacia la Milk-V, LED y botón._
