# Hardware

## Lista de materiales

| Elemento | Cantidad | Estado |
|---|---|---|
| NUCLEO-L432KC o NUCLEO-L412KB | 1 | Disponibilidad por confirmar |
| Milk-V Duo S + microSD | 1 | _Por verificar_ |
| DHT11 (identificar revisión y módulo) | 1 | Disponible; revisión por verificar |
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

Consultar el mapa de DHT11, LED, pulsador y puentes de soldadura en [firmware/README.md](../firmware/README.md). La alternativa L412KB conserva el protocolo, pero requiere su propio binario. Verificar modelo y revisión impresos; no cargar el binario de una placa en la otra.

Fuentes: [ST UM1956, tablas de pines Nucleo-32](https://www.st.com/resource/en/user_manual/um1956-stm32-nucleo32-boards-mb1180-stmicroelectronics.pdf), [Milk-V Duo S](https://milkv.io/docs/duo/getting-started/duos).

## Sensor DHT11

Conectar DATA a PB5/D11 del NUCLEO, GND a GND y VCC a 3,3 V **solo si la variante admite esa tensión**. Usar pull-up de 4,7 kΩ a 3,3 V si no está incorporado; identificar el orden de pines impreso (no asumir que los módulos de tres pines tienen el mismo pinout). Cable corto. Si la variante requiere 5 V, verificar el circuito y adaptar niveles antes de conectar DATA; no llevar 5 V directamente al GPIO por defecto. No se utiliza I²C. La revisión antigua puede requerir un periodo superior a 5 s para máxima exactitud; el firmware de este PR adopta 3 s para V1.3. Ver [fuente y limitaciones](../docs/criterios-conservacion.md).

## Aplicación documental y posición del sensor

El elemento sensible DHT11 debe estar en el aire próximo a los documentos de **un gabinete pequeño**, separado térmicamente de las placas Milk-V y STM32. Evitar puerta, rejilla, sol, calefacción y condensación. Registrar la ubicación y verificar sesgo térmico (PS-03), exactitud (PS-02) y representatividad antes de afirmar que el punto caracteriza todo el compartimento. Un sensor no caracteriza una bodega completa.

[Fundamentación y recomendaciones de colocación](../docs/criterios-conservacion.md).

## Carcasa

Prototipo ventilado. El sensor queda separado de las placas o en la zona ventilada. Quedan accesibles: botón de reset, LED, Ethernet y alimentación.
