# Smart Environmental Monitoring System

Prototipo de **vigilancia microclimática para conservación preventiva de documentos en papel**, usando **STM32 (NUCLEO-L432KC o NUCLEO-L412KB)** y **Milk-V Duo S** en el curso de Linux embebido. Monitoriza un **gabinete documental pequeño**, no una bodega completa.

## Problema que resuelve

Las inspecciones ocasionales de temperatura y humedad no revelan qué ocurrió fuera del horario de atención. Registramos el aire próximo a una colección documental y señalamos desviaciones respecto a los límites **configurados**. No controlamos el clima ni diagnosticamos daño del papel.

**Perfil inicial sugerido, editable:** **18–22 °C** y **35–50 % HR**. No son valores universales: consultar [fundamentación, cifras y limitaciones](docs/criterios-conservacion.md), basada en NEDCC 2.1/2.2, Library of Congress y CCI. La instalación y la exactitud reales siguen pendientes de verificación.
## Funcionamiento

1. Un sensor I²C (SHT31 o equivalente) mide la temperatura y la humedad.
2. El STM32 adquiere y valida las mediciones una vez por segundo.
3. El STM32 envía los datos a la Milk-V por UART con un [protocolo de texto verificable](docs/protocolo-uart.md).
4. La Milk-V registra las mediciones, evalúa las alarmas y sirve una interfaz web local por Ethernet.
5. El usuario consulta los valores actuales, el historial y el diagnóstico del sistema desde el navegador. Los LED del equipo muestran el estado sin necesidad de abrir la web.

```
┌──────────┐   I²C   ┌──────────────┐  UART 115200 8N1  ┌──────────────┐  Ethernet  ┌───────────┐
│  SHT31   │◄───────►│ NUCLEO-L432KC│◄─────────────────►│ Milk-V Duo S │◄──────────►│ Navegador │
└──────────┘         │  LED + botón │                   │ servicio + web│            └───────────┘
                     └──────────────┘                   └──────────────┘
```

## Elementos del curso

| Requisito | Cómo se cumple |
|---|---|
| Comunicación SBC–microcontrolador | UART, ver [protocolo-uart.md](docs/protocolo-uart.md) |
| Sensado de una variable física | Temperatura y humedad relativa |
| Bring-up | Configuración inicial por web en el primer arranque, ver [manual-usuario.md](docs/manual-usuario.md) |
| Factory reset | Botón físico sostenido 10 s → regreso al bring-up |
| Interfaz de usuario | LED de estado + interfaz web de seguimiento |
| Carcasa | Prototipo ventilado, ver [hardware/](hardware/) |
| Instrumentación soft | Página de diagnóstico: CPU, memoria, almacenamiento, red, puertos, UART, estado del STM32 |

## Estructura del repositorio

```
docs/       Documentación: descripción, requerimientos, arquitectura, matriz, protocolo, manual, roles, uso de IA
firmware/   Firmware STM32 (NUCLEO-L432KC / NUCLEO-L412KB)
linux/      Servicio en C para la Milk-V (src/, tests/)
web/        Páginas estáticas de la interfaz web
hardware/   Conexionado, lista de materiales, fotos y carcasa
```

## Documentación

Índice completo y orden de lectura en [docs/README.md](docs/README.md). Los cuatro documentos principales:

1. [Descripción del sistema](docs/descripcion-sistema.md): qué problema resuelve y para quién.
   - [Criterios ambientales](docs/criterios-conservacion.md): fuentes, intervalos, incertidumbre y decisiones de diseño.
2. [Análisis de requerimientos](docs/requerimientos.md): qué debe hacer, con criterios medibles.
3. [Arquitectura de la solución](docs/arquitectura.md): cómo está construido.
4. [Matriz de verificación](docs/matriz-verificacion.md): cómo se demuestra que cumple.

## Equipo

| Integrante | Rol |
|---|---|
| Andrew | Firmware STM32, bring-up físico de Milk-V, cableado, alimentación, montaje y pruebas de hardware |
| Mateo | Servicio Linux `envmond` (UART, datos, alarmas, reset, HTTP/API y páginas de configuración), matriz e IA |
| Juan Fernando | Dashboard e historial, diagnóstico, herramientas de pruebas, carcasa, manual, video y aceptación |

Detalle en [roles.md](docs/roles.md).

## Plan de trabajo

El hardware se valida antes de construir el resto del software:

1. Prueba independiente de la Milk-V (arranque, consola, Ethernet).
2. Prueba independiente del STM32 + sensor.
3. Comunicación entre placas con datos ficticios.
4. Integración del sensor real.
5. Funcionamiento continuo y pruebas de fallas.
6. Integración final: web, carcasa, instrumentación, bring-up y factory reset.

## Pruebas de firmware y enlace

Ver [la guía de bring-up](docs/bringup-v0.md), [firmware/](firmware/) y [linux/](linux/). La utilidad de laboratorio `uart_probe` no reemplaza a `envmond`. No se deben utilizar datos sintéticos como mediciones del gabinete.

## Estado

🚧 Aplicación definida; producto en desarrollo. La validación física y las pruebas de la matriz siguen pendientes. Ver [requerimientos.md](docs/requerimientos.md#6-decisiones-abiertas).
