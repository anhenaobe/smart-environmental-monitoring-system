# Descripción del sistema

Versión: 0.1 (borrador)

## 1. Propósito

Dispositivo autónomo que mide la temperatura y la humedad relativa de un espacio interior, guarda su historial y lo presenta a usuarios no expertos mediante LED de estado y una interfaz web local. El usuario lo configura en el primer arranque (bring-up) y puede devolverlo al estado de fábrica con un botón.

## 2. Arquitectura

| Subsistema | Componente | Responsabilidades |
|---|---|---|
| Sensado | SHT31 (I²C) | Medición de temperatura y humedad |
| Embebido | NUCLEO-L432KC | Adquisición, validación (CRC, rango), detección de fallas del sensor, watchdog, LED, botón de factory reset, protocolo UART |
| Linux | Milk-V Duo S | Servicio en C: recepción y validación de tramas, agregación y registro, alarmas, configuración persistente, bring-up y factory reset, instrumentación, servidor web |
| Usuario | Navegador + LED | Visualización, configuración inicial, diagnóstico |

## 3. Hardware

| Elemento | Función |
|---|---|
| NUCLEO-L432KC | Adquisición y firmware |
| Milk-V Duo S | Linux, registros e interfaz web (Ethernet 100 Mbps integrada) |
| Sensor SHT31 o equivalente | Temperatura y humedad (I²C) |
| 2 LED (verde, rojo) + resistencias | Indicadores de estado |
| Pulsador | Factory reset |
| Alimentación USB 5 V | Alimentación del prototipo |
| Carcasa ventilada | Protección; el sensor queda separado del calor de las placas |

Conexión UART: TX↔RX cruzados + GND común, lógica de 3,3 V en ambas placas. Las líneas de alimentación de las placas no se unen. Se usa una UART dedicada, no la consola de depuración de Linux. No se usa SPI.

## 4. Estados del dispositivo

```
            (sin configuración)
  Arranque ───────────────────────► BRING-UP ──(configuración guardada)──► NORMAL ◄──► ALARMA
     │                                  ▲                                     │
     └──────(configuración válida)──────┼──────────────────────────────► NORMAL│
                                        │                                     │
                                        └────────── FACTORY RESET ◄──(botón 10 s)
```

## 5. Flujo de datos

1. El STM32 lee el sensor cada segundo y envía `ENV`.
2. Linux valida la trama y actualiza el valor actual (visible en la web).
3. Cada minuto, Linux calcula el promedio, el mínimo y el máximo, y los escribe en el registro.
4. Linux compara con los umbrales y envía `SYS` con el estado (normal o alarma) cada segundo.
5. El STM32 muestra el estado en los LED.

## 6. Elecciones tecnológicas

- **Lenguajes compilados:** C en el firmware y en el servicio Linux (recomendación del curso: herramientas nativas, pocas capas de abstracción, menor uso de recursos).
- **Métricas** leídas directamente de `/proc` y `/sys`.
- **Configuración** en un archivo de texto `clave=valor`, escrito de forma atómica.
- **Registros** en CSV con rotación diaria.
