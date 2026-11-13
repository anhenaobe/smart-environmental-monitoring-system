# Protocolo de comunicación UART STM32 ↔ Milk-V Duo S

Versión: 0.2 (borrador). Cambios frente a 0.1: `STA` agrega `lazo_max_us` y `pila_libre_b` (RF-07). Los checksums de todos los ejemplos se recalcularon con un script.

Este documento es la única definición válida del protocolo. Cualquier cambio en el firmware o en el servicio Linux debe reflejarse primero aquí, y lo acuerdan quien implementa el firmware y quien implementa el servicio Linux.

## 1. Capa física

| Parámetro | Valor |
|---|---|
| Interfaz | UART dedicada (no la consola de depuración de Linux) |
| Velocidad | 115200 baudios |
| Formato | 8N1, sin control de flujo |
| Niveles lógicos | 3,3 V en ambas placas |
| Conexión | TX↔RX cruzados + GND común. **No** se unen las líneas de alimentación. |

## 2. Formato de trama

Texto ASCII, una trama por línea, inspeccionable con cualquier terminal serie:

```
$<TIPO>,<campo1>,<campo2>,...*<CS>\r\n
```

- `$`: inicio de trama.
- `<TIPO>`: tres letras mayúsculas.
- `*<CS>`: checksum, dos dígitos hexadecimales en mayúscula.
- `\r\n`: fin de trama.
- Longitud máxima: 64 bytes, incluidos `$` y `\r\n`.

### Checksum

XOR de todos los bytes **entre** `$` y `*`, sin incluir ninguno de los dos (mismo esquema que NMEA 0183).

```c
uint8_t cs = 0;
for (const char *p = start_after_dollar; *p != '*'; ++p) cs ^= (uint8_t)*p;
```

Ejemplo: para `$ENV,00000042,+02450,05310,0*` el checksum se calcula sobre `ENV,00000042,+02450,05310,0`.

### Reglas de recepción (ambos lados)

1. Se descartan los bytes recibidos hasta encontrar `$`.
2. Si la línea supera 64 bytes sin `\n`, se descarta y se cuenta un error de trama.
3. Si el checksum no coincide, se descarta y se cuenta un error de checksum.
4. Un tipo desconocido se descarta y se cuenta como trama desconocida (no es un error fatal).
5. Los contadores de error se exponen en la página de diagnóstico.

## 3. Mensajes STM32 → Milk-V

### `ENV`: medición (cada 1 s)

```
$ENV,<seq>,<temp>,<hr>,<estado>*<CS>
```

| Campo | Formato | Descripción |
|---|---|---|
| `seq` | 8 dígitos decimales | Consecutivo; vuelve a 0 después de 99999999 (~3 años a 1 Hz). Sirve para detectar tramas perdidas. |
| `temp` | signo + 5 dígitos | Temperatura en centésimas de °C. `+02450` = 24,50 °C; `-00520` = −5,20 °C. |
| `hr` | 5 dígitos | Humedad relativa en centésimas de %. `05310` = 53,10 %. |
| `estado` | 1 dígito | Ver tabla de estados. |

Si `estado ≠ 0`, `temp` y `hr` van vacíos: `$ENV,00000043,,,1*CS`.

| Estado | Significado |
|---|---|
| 0 | Lectura válida |
| 1 | El sensor no responde (NACK en I²C) |
| 2 | CRC del sensor inválido |
| 3 | Lectura fuera del rango físico del sensor |

### `STA`: estado del microcontrolador (cada 10 s y al arrancar)

```
$STA,<uptime_s>,<err_i2c>,<causa_reset>,<fw>,<lazo_max_us>,<pila_libre_b>*<CS>
```

| Campo | Descripción |
|---|---|
| `uptime_s` | Segundos desde el último arranque del STM32 |
| `err_i2c` | Errores I²C acumulados desde el arranque |
| `causa_reset` | Leída de `RCC_CSR`: 0 = encendido/BOR, 1 = pin NRST, 2 = watchdog IWDG, 3 = software, 4 = otra |
| `fw` | Versión del firmware, `MAJ.MIN.PAT` |
| `lazo_max_us` | 5 dígitos: duración máxima de una iteración del lazo principal desde el último `STA`, en µs (medida con un temporizador o DWT `CYCCNT`); se satura en 99999. Se reinicia tras cada envío. *(v0.2, RF-07)* |
| `pila_libre_b` | 5 dígitos: bytes de pila nunca usados desde el arranque (la pila se rellena con un patrón al iniciar y se busca el primer byte modificado). *(v0.2, RF-07)* |

Longitud típica: 40 bytes, dentro del límite de 64.

### `BTN`: solicitud de factory reset

```
$BTN,RST*<CS>
```

Se envía cuando el botón de reset lleva 10 s presionado. Se repite cada 1 s hasta recibir `$ACK,RST`, como máximo 5 veces. Si no llega el ACK, el STM32 indica el fallo con los LED (ver §5) y no reintenta hasta que el botón se suelte y se vuelva a presionar.

## 4. Mensajes Milk-V → STM32

### `SYS`: estado del sistema / latido (cada 1 s)

```
$SYS,<estado_sistema>*<CS>
```

| Valor | Estado |
|---|---|
| 0 | Sin configurar (modo bring-up) |
| 1 | Funcionamiento normal |
| 2 | Alarma: medición fuera de los umbrales configurados |
| 3 | Factory reset en curso |

Si el STM32 pasa más de 5 s sin recibir `SYS`, considera que Linux no está disponible y lo muestra en los LED. El STM32 **sigue midiendo y transmitiendo** aunque no reciba latido.

### `ACK`: confirmación

```
$ACK,RST*<CS>
```

Confirma que Linux recibió la solicitud de factory reset y la va a ejecutar.

## 5. Indicadores LED (STM32)

| LED verde | LED rojo | Significado |
|---|---|---|
| Fijo | Apagado | Funcionamiento normal |
| Parpadeo lento (1 Hz) | Apagado | Modo bring-up: el equipo no está configurado |
| Fijo | Parpadeo lento (1 Hz) | Alarma por umbral |
| Apagado | Fijo | Falla del sensor (estado 1, 2 o 3) |
| Apagado | Doble destello cada 2 s | Sin latido de Linux (> 5 s) |
| Parpadeo rápido (5 Hz) | Parpadeo rápido (5 Hz) | Botón de reset presionado / reset en curso |
| Apagado | 3 destellos | Reset solicitado, pero sin ACK de Linux |

Prioridad cuando coinciden varios estados: reset > falla de sensor > sin latido > alarma > bring-up > normal.

## 6. Ejemplos de sesión

```
STM32 → $STA,00000003,0,0,0.1.0,00850,02048*77
STM32 → $ENV,00000000,+02450,05310,0*42
Linux → $SYS,1*44
STM32 → $ENV,00000001,+02451,05308,0*4B
...
STM32 → $BTN,RST*21          (botón sostenido 10 s)
Linux → $ACK,RST*30
Linux → $SYS,3*46
...                           (Linux reinicia en modo bring-up)
Linux → $SYS,0*45
```

Otros vectores de prueba: `$ENV,00000042,+02450,05310,0*44` y `$ENV,00000043,,,1*6B` (falla del sensor). Estos ejemplos sirven como vectores de referencia para las pruebas unitarias PU-03, PU-04 y PU-05.
