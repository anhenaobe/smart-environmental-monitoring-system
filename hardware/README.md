# Hardware

## Lista de materiales

| Elemento | Cantidad | Estado |
|---|---|---|
| NUCLEO-L432KC o NUCLEO-L412KB | 1 | Disponibilidad por confirmar |
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

Candidato de UART dedicada (verificar pinmux y dispositivo en Debian antes de conectar): J3 pin 11 (B11/UART2_TX) → PA10/D0; J3 pin 13 (B12/UART2_RX) ← PA9/D1; J3 pin 6 (GND) ↔ GND. J3 es de 3,3 V; **no usar J4** (1,8 V), ni J3 8/10 (consola UART0). Las líneas de alimentación de las placas no se unen.

Consultar el mapa de SHT31, LED, pulsador y puentes de soldadura en [firmware/README.md](../firmware/README.md). La alternativa L412KB conserva el protocolo, pero requiere su propio binario. Verificar modelo y revisión impresos; no cargar el binario de una placa en la otra.

Fuentes: [ST UM1956, tablas de pines Nucleo-32](https://www.st.com/resource/en/user_manual/um1956-stm32-nucleo32-boards-mb1180-stmicroelectronics.pdf), [Milk-V Duo S](https://milkv.io/docs/duo/getting-started/duos).

## Aplicación documental y posición del sensor

El elemento sensible SHT31 debe estar en el aire próximo a los documentos de **un gabinete pequeño**, separado térmicamente de las placas Milk-V y STM32. Evitar puerta, rejilla, sol, calefacción y condensación. Registrar la ubicación y verificar sesgo térmico (PS-03), exactitud (PS-02) y representatividad antes de afirmar que el punto caracteriza todo el compartimento. Un sensor no caracteriza una bodega completa.

[Fundamentación y recomendaciones de colocación](../docs/criterios-conservacion.md).

## Carcasa

Prototipo ventilado. El sensor queda separado de las placas o en la zona ventilada. Quedan accesibles: botón de reset, LED, Ethernet y alimentación.
