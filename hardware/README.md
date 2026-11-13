# Hardware

## Lista de materiales

| Elemento | Cantidad | Estado |
|---|---|---|
| NUCLEO-L432KC | 1 | Disponible |
| Milk-V Duo S + microSD | 1 | _Por verificar_ |
| Módulo SHT31 (o equivalente I²C) | 1 | _Por verificar_ |
| LED verde + LED rojo + resistencias | 1 c/u | _Por verificar_ |
| Pulsador | 1 | _Por verificar_ |
| Cable Ethernet | 1 | _Por verificar_ |
| Fuentes / cables USB 5 V | 2 | _Por verificar_ |
| Protoboard / baquela perforada, cables | — | _Por verificar_ |

Antes de comprar se verifica qué componentes ya están disponibles.

## Conexionado

| Milk-V Duo S (J3) | STM32 |
|---|---|
| UART TX | UART RX |
| UART RX | UART TX |
| GND | GND |

_Pines exactos por definir._ Las líneas de alimentación de las placas no se unen.

## Carcasa

Prototipo ventilado. El sensor queda separado de las placas o en la zona ventilada. Quedan accesibles: botón de reset, LED, Ethernet y alimentación.
