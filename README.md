# Smart Environmental Monitoring System

Monitor autónomo de temperatura y humedad relativa con **STM32 (NUCLEO-L432KC)** y **Milk-V Duo S**, desarrollado como proyecto del curso de Linux embebido.

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
docs/       Documentación: requerimientos, protocolo, manual, matriz de verificación, roles, uso de IA
firmware/   Firmware del STM32 (NUCLEO-L432KC)
linux/      Servicio en C para la Milk-V (src/, tests/)
web/        Páginas estáticas de la interfaz web
hardware/   Conexionado, lista de materiales, fotos y carcasa
```

## Documentación

- [Descripción del sistema](docs/descripcion-sistema.md)
- [Análisis de requerimientos](docs/requerimientos.md)
- [Protocolo UART](docs/protocolo-uart.md)
- [Matriz de verificación](docs/matriz-verificacion.md)
- [Manual de usuario](docs/manual-usuario.md)
- [Interfaces entre subsistemas](docs/interfaces.md)
- [Roles y aportes](docs/roles.md)
- [Declaración de uso de IA](docs/uso-ia.md)

## Plan de trabajo

El hardware se valida antes de construir el resto del software:

1. Prueba independiente de la Milk-V (arranque, consola, Ethernet).
2. Prueba independiente del STM32 + sensor.
3. Comunicación entre placas con datos ficticios.
4. Integración del sensor real.
5. Funcionamiento continuo y pruebas de fallas.
6. Integración final: web, carcasa, instrumentación, bring-up y factory reset.

## Estado

🚧 En definición (documentación v0.2). Ver las decisiones abiertas en [requerimientos.md](docs/requerimientos.md#6-decisiones-abiertas).
