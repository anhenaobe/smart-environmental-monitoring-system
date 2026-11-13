# Descripción del sistema

Versión: 0.2 (borrador para revisión en clase)

## 1. Problema y propósito

En laboratorios, bodegas de reactivos o cuartos de equipos, la temperatura y la humedad deben mantenerse dentro de un rango, pero normalmente nadie las mide de forma continua: se revisa un termómetro de vez en cuando, o nunca. Cuando algo sale mal (un aire acondicionado que falla un fin de semana) no queda registro de cuándo empezó ni de cuánto duró.

El **Smart Environmental Monitoring System** es un dispositivo autónomo que mide la temperatura y la humedad relativa de un espacio interior cada segundo, guarda su historial y lo presenta a personas no expertas de dos formas:

- **Sin abrir nada:** dos LED en la carcasa dicen si todo está bien, si hay una alarma o si el equipo tiene una falla.
- **Desde el navegador:** una página web local con los valores actuales, la gráfica del historial y una página de diagnóstico para soporte técnico.

El equipo se comporta como un producto: el usuario lo conecta, lo configura una sola vez siguiendo el manual (bring-up) y lo deja funcionando. Si se va a reubicar o entregar a otra persona, un botón lo devuelve al estado de fábrica.

## 2. Escenario de uso

1. El administrador saca el equipo de la caja, lo conecta y abre en su computador la dirección impresa en la etiqueta.
2. Completa el formulario: nombre ("Bodega reactivos"), contraseña, umbrales (15–30 °C, 30–70 % HR). Guarda. El LED verde queda fijo.
3. Durante la semana, cualquier persona que entra ve el LED verde. Un sábado el aire se apaga: la temperatura pasa de 30 °C y el LED rojo empieza a parpadear.
4. El lunes, el administrador abre el historial y ve que la alarma empezó el sábado a las 14:10 y que la temperatura llegó a 33 °C.
5. Si un día la web deja de mostrar datos, soporte técnico abre la página de diagnóstico y ve, por ejemplo, que el STM32 reporta errores I²C: el problema es el cable del sensor, no Linux.

## 3. Arquitectura

```
┌──────────┐   I²C   ┌───────────────┐  UART 115200 8N1  ┌────────────────────────┐  Ethernet / USB  ┌───────────┐
│  SHT31   │◄───────►│ NUCLEO-L432KC │◄─────────────────►│ Milk-V Duo S (Linux)   │◄────────────────►│ Navegador │
└──────────┘         │ LED + botón   │                   │ envmond + web + logs   │                  └───────────┘
                     └───────────────┘                   └────────────────────────┘
```

| Subsistema | Componente | Responsabilidades |
|---|---|---|
| Sensado | SHT31 (I²C) | Medición de temperatura y humedad |
| Embebido | NUCLEO-L432KC | Adquisición, validación (CRC, rango), detección de fallas del sensor, watchdog, LED, botón de factory reset, protocolo UART, autoinstrumentación (tiempo de lazo, pila) |
| Linux | Milk-V Duo S | Servicio en C `envmond`: recepción y validación de tramas, agregación y registro, alarmas, configuración persistente, bring-up y factory reset, instrumentación, servidor web |
| Usuario | Navegador + LED | Visualización, configuración inicial, ajustes, diagnóstico |

**División de responsabilidades.** El STM32 hace todo lo que depende del tiempo real o del hardware (leer el sensor a tiempo, el botón, los LED, el watchdog) y sigue funcionando aunque Linux no responda. Linux hace todo lo que necesita almacenamiento, red o interfaz gráfica. Así una falla en Linux no detiene la medición, y una falla del sensor se reporta sin bloquear la web.

## 4. Hardware

| Elemento | Función |
|---|---|
| NUCLEO-L432KC (STM32L432KC, 256 KB flash, 64 KB RAM) | Adquisición y firmware |
| Milk-V Duo S (SG2000, 512 MB RAM, Ethernet 100 Mbps) | Linux, registros e interfaz web |
| Sensor SHT31 o equivalente | Temperatura y humedad (I²C) |
| 2 LED (verde, rojo) + resistencias | Indicadores de estado |
| Pulsador (empotrado en la carcasa) | Factory reset |
| Alimentación USB 5 V | Alimentación del prototipo |
| Carcasa ventilada | Protección; el sensor queda separado del calor de las placas |

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

**Factory reset.** El botón está en el STM32, no en Linux, para que funcione aunque la web no responda. Tras 10 s sostenido, el STM32 envía `BTN,RST`; Linux confirma con `ACK,RST`, borra los registros y por último la configuración, y vuelve a bring-up. Borrar la configuración al final garantiza que un corte de energía a mitad del proceso deje el equipo configurado o en bring-up, nunca en un estado intermedio.

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
