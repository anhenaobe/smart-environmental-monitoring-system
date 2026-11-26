# Descripción del sistema

Versión: 0.3 (enfoque en conservación documental, revisión del equipo pendiente)

## 1. Problema y propósito

Los cambios de temperatura y humedad relativa en **gabinetes pequeños que guardan documentos y libros en papel** pueden pasar inadvertidos cuando se inspeccionan ocasionalmente. Sin registro continuo, el responsable desconoce cuándo ocurrieron condiciones fuera de los umbrales adoptados ni qué extremos alcanzó el aire. El NEDCC 2.2 respalda el registro y análisis ambiental como parte de conservación preventiva: [fundamentación](criterios-conservacion.md).

El **Smart Environmental Monitoring System** está diseñado para supervisar **un gabinete documental pequeño, en un único punto representativo**. Mide temperatura y humedad del aire cada segundo, guarda el historial y presenta los valores de dos formas:

- **Sin abrir nada:** dos LED en la carcasa dicen si todo está bien, si hay una alarma o si el equipo tiene una falla.
- **Desde el navegador:** una página web local con los valores actuales, la gráfica del historial y una página de diagnóstico para soporte técnico.

El equipo se plantea como producto: se instala, configura mediante bring-up y consulta sin conocimientos de Linux. Un botón lo devuelve a fábrica. **No deshumidifica ni enfría, no determina si un documento ya se dañó y no representa una sala completa.** Ofrece un perfil editable de **18–22 °C / 35–50 % HR**, no una consigna universal.

## 2. Escenario de uso

1. El encargado del archivo instala la sonda en el aire de un gabinete con documentos en papel y mantiene las placas que desprenden calor fuera del volumen medido.
2. Completa el bring-up con nombre ("Gabinete documental A"), contraseña, hora y umbrales. El formulario sugiere **18–22 °C** y **35–50 % HR**, **editables** según la colección.
3. Fuera de horario la humedad excede el límite configurado. `envmond` indica la alarma y el STM32 refleja el estado en los LED; se guardan extremos y tendencias por intervalos de registro.
4. Al volver, el encargado consulta el historial y determina si debe inspeccionar el gabinete. El histórico por minuto **no revela la duración exacta de excursiones de segundos** sin un registro adicional de eventos.
5. Si la sonda se desconecta, la web distingue una falla de sensor de una medición válida.

Los números de conservación, la incertidumbre de la medición y la limitación de usar una sonda se detallan en [criterios-conservacion.md](criterios-conservacion.md).

## 3. Arquitectura

Resumen. El detalle (vistas lógica, física, de firmware y de software, diagramas de secuencia y decisiones) está en [arquitectura.md](arquitectura.md).

```
┌──────────┐   I²C   ┌───────────────┐  UART 115200 8N1  ┌────────────────────────┐  Ethernet / USB  ┌───────────┐
│  SHT31   │◄───────►│ NUCLEO-L432KC │◄─────────────────►│ Milk-V Duo S (Linux)   │◄────────────────►│ Navegador │
└──────────┘         │ LED + botón   │                   │ envmond + web + logs   │                  └───────────┘
                     └───────────────┘                   └────────────────────────┘
```

| Subsistema | Componente | Responsabilidades |
|---|---|---|
| Sensado | SHT31 (I²C) | Medición de temperatura y humedad |
| Embebido | NUCLEO-L432KC o alternativa NUCLEO-L412KB (por validar) | Adquisición, validación (CRC, rango), detección de fallas del sensor, watchdog, LED, botón de factory reset, protocolo UART, autoinstrumentación (tiempo de lazo, pila) |
| Linux | Milk-V Duo S | Servicio en C `envmond`: recepción y validación de tramas, agregación y registro, alarmas, configuración persistente, bring-up y factory reset, instrumentación, servidor web |
| Usuario | Navegador + LED | Visualización, configuración inicial, ajustes, diagnóstico |

**División de responsabilidades.** El STM32 hace todo lo que depende del tiempo real o del hardware (leer el sensor a tiempo, el botón, los LED, el watchdog) y sigue funcionando aunque Linux no responda. Linux hace todo lo que necesita almacenamiento, red o interfaz gráfica. Así una falla en Linux no detiene la medición, y una falla del sensor se reporta sin bloquear la web.

## 4. Hardware

| Elemento | Función |
|---|---|
| NUCLEO-L432KC (256 KB flash, 64 KB RAM) o NUCLEO-L412KB (128 KB flash, 40 KB RAM) | Adquisición y firmware; seleccionar según disponibilidad y pruebas |
| Milk-V Duo S (SG2000, 512 MB RAM, Ethernet 100 Mbps) | Linux, registros e interfaz web |
| Sensor SHT31 o equivalente | Temperatura y humedad (I²C) |
| 2 LED (verde, rojo) + resistencias | Indicadores de estado |
| Pulsador (empotrado en la carcasa) | Factory reset |
| Alimentación USB 5 V | Alimentación del prototipo |
| Carcasa ventilada | Sonda en el aire del gabinete y separada del calor de Milk-V y STM32 |

Conexión UART: TX↔RX cruzados + GND común, lógica de 3,3 V en ambas placas (el J3 de la Duo S es de 3,3 V; el J4 es de 1,8 V y **no** se usa). Las líneas de alimentación de las placas no se unen. Se usa una UART dedicada, no la UART0 (pines 8 y 10 del J3), que es la consola de Linux. No se usa SPI.

## 5. Software

### Firmware (STM32, C)

| Módulo | Función | Se prueba en el host |
|---|---|---|
| `sht31` | Comando de medición, lectura, CRC-8 y conversión | Sí (CRC y conversión) |
| `protocol` | Armado y lectura de tramas, checksum | Sí |
| `button` | Antirrebote y detección de 10 s | Sí |
| `leds` | Patrones y prioridad de estados | Sí (selección del patrón) |
| `main` | Lazo principal, temporización, watchdog, instrumentación | No (pruebas de integración) |

### Servicio Linux `envmond` (C)

| Módulo | Función |
|---|---|
| `uart` | Abrir y configurar el puerto, lectura no bloqueante |
| `parser` | Validación y decodificación de tramas, contadores de error |
| `logger` | Agregación por intervalo, CSV, rotación y retención |
| `alarm` | Comparación con umbrales, persistencia e histéresis |
| `config` | Lectura, validación y guardado atómico; detección del modo bring-up |
| `reset` | Secuencia del factory reset |
| `metrics` | CPU, memoria, almacenamiento, red y puertos desde `/proc` y `/sys` |
| `http` | Servidor web y endpoints JSON (`/api/estado`, `/api/historial`, `/api/diagnostico`) |

### Web

HTML, CSS y JavaScript mínimo, sin CDN: inicio, historial, diagnóstico, bring-up y ajustes. Las páginas consultan los endpoints JSON cada 1–2 s.

## 6. Estados del dispositivo

```
                 ┌───────────────── configuración válida ─────────────────┐
                 │                                                        ▼
 Encendido ──► ARRANQUE ── sin configuración ──► BRING-UP ── guardar ──► NORMAL ◄────► ALARMA
                                                    ▲                       │            │
                                                    │                       └─────┬──────┘
                                                    │                             │ botón 10 s
                                                    └────────── FACTORY RESET ◄───┘
```

Las fallas (sensor desconectado, Linux sin latido) no son estados del sistema sino condiciones que se superponen a cualquier estado y que los LED muestran con mayor prioridad (ver §5 del [protocolo](protocolo-uart.md)).

## 7. Flujo de datos

1. El STM32 lee el sensor cada segundo y envía `ENV`; cada 10 s envía `STA` con su estado interno.
2. Linux valida la trama y actualiza el valor actual (visible en la web en ≤ 2 s).
3. Cada intervalo de registro (60 s por defecto), Linux calcula el promedio, el mínimo y el máximo, y los escribe en el CSV del día.
4. Linux compara con los umbrales y envía `SYS` con el estado (normal o alarma) cada segundo.
5. El STM32 muestra el estado en los LED.

## 8. Bring-up y factory reset

**Bring-up.** Al arrancar, `envmond` busca `/etc/envmon/config`. Si no existe, entra en modo bring-up: envía `SYS,0` (LED verde a 1 Hz) y la web solo muestra el formulario de configuración. Al guardar, la configuración se valida, se escribe de forma atómica y el equipo pasa a normal sin reiniciarse.

**Factory reset.** El botón está en el STM32, no en Linux, para que funcione aunque la web no responda. Tras 10 s sostenido, el STM32 envía `BTN,RST`; Linux confirma con `ACK,RST`, borra los registros y por último la configuración, y vuelve a bring-up. El servicio persiste un marcador antes de confirmar el reset. Si ocurre un corte después, reanuda el borrado en el siguiente arranque antes de habilitar la web; esto aún debe comprobarse en la microSD real.

## 9. Instrumentación soft

| Qué se monitorea | Fuente |
|---|---|
| Uso de CPU | `/proc/stat` |
| Memoria del sistema y de `envmond` | `/proc/meminfo`, `/proc/self/status` |
| Almacenamiento | `statvfs()`, `/proc/diskstats` |
| Red: bytes de entrada y salida por interfaz | `/proc/net/dev` |
| Red: puertos en escucha | `/proc/net/tcp`, `/proc/net/udp` |
| Periférico UART: bytes, tramas, errores, pérdidas, tiempo desde la última | Contadores de `envmond` |
| STM32: uptime, errores I²C, causa del reset, versión, tiempo de lazo, pila libre | Trama `STA` |

## 10. Elecciones tecnológicas

- **Lenguajes compilados:** C en el firmware y en el servicio Linux (recomendación del curso: herramientas nativas, pocas capas de abstracción, menor uso de recursos).
- **Un solo proceso en Linux:** `envmond` incluye el servidor HTTP, así que no hay que instalar ni configurar Nginx o Apache.
- **Métricas** leídas directamente de `/proc` y `/sys`.
- **Configuración** en un archivo de texto `clave=valor`, escrito de forma atómica.
- **Registros** en CSV con rotación diaria; el estado actual vive en RAM (`/run`) para no desgastar la microSD.
- **Protocolo de texto con checksum**, legible con cualquier terminal serie para depurar.

## 11. Riesgos y lecciones del reto anterior

| Riesgo | Por qué importa | Mitigación |
|---|---|---|
| Un periférico de la Milk-V no funciona como se esperaba | En el reto anterior el SPI convencional de la Milk-V no estuvo disponible y hubo que usar GPIO | La UART dedicada se prueba primero, con un lazo TX→RX (PI-01), antes de escribir el servicio |
| El servidor web no queda demostrado en la placa | En el reto anterior el servidor web en la Milk-V no quedó demostrado en el montaje probado | Servidor embebido en `envmond`; PI-08 se ejecuta en la Duo S, no en un PC |
| Resultados nominales que no se miden | Valores declarados en código (frecuencias, tasas) no equivalen a valores medidos | Todo criterio de la matriz es numérico y medible, con herramienta asignada |
| Revisión de hardware de la Duo S | El pin 9 del J3 es GPIO en la v1.1 y GND desde la v1.2 | Verificar la revisión de la placa antes de cablear |
| Herramientas no disponibles en RISC-V | Valgrind y SystemTap pueden no existir en la imagen de la Duo S | Build de host x86 con puerto serie virtual (`socat`) |
| El usuario no encuentra la IP del equipo | Rompe el bring-up para un usuario no experto | RF-46: dirección fija impresa en la etiqueta |

## 12. Plan de trabajo

1. Prueba independiente de la Milk-V (arranque, consola, Ethernet, red USB, UART dedicada en lazo).
2. Prueba independiente del STM32 + sensor.
3. Comunicación entre placas con datos ficticios.
4. Integración del sensor real.
5. Funcionamiento continuo y pruebas de fallas.
6. Integración final: web, carcasa, instrumentación, bring-up y factory reset.
