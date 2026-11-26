# Protocolo de comunicación UART STM32 ↔ Milk-V Duo S

Versión: 0.3 (borrador). Cambios frente a 0.2, acordado entre Andrew (STM32) y Mateo (Linux), 2026-11-26: anchos de los campos de `STA`, `hr` ≤ 10000, reglas de detección de tramas perdidas y reinicios por `seq` (§3) y clasificación de errores de recepción (§2). Ninguna trama cambia de formato.

Cambios de 0.2 frente a 0.1: `STA` agrega `lazo_max_us` y `pila_libre_b` (RF-07). Los checksums de todos los ejemplos se recalcularon con un script.

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

#### Clasificación de errores

Cada línea descartada suma exactamente un contador. Las comprobaciones se aplican en el orden de la tabla: la primera que falla decide el contador.

| Orden | Caso | Contador |
|---|---|---|
| 1 | Llega un `$` antes del `\n` de la línea en curso (trama truncada) | `err_trama` |
| 1 | 64 bytes sin `\n` | `err_trama` |
| 2 | Línea de menos de 7 bytes, sin `\r` antes del `\n` o sin `*` antes de `<CS>` | `err_trama` |
| 2 | `<CS>` que no son dos dígitos hexadecimales en mayúscula | `err_trama` |
| 2 | Carga con bytes no imprimibles o con un segundo `*` | `err_trama` |
| 3 | Estructura correcta pero el XOR no coincide con `<CS>` | `err_checksum` |
| 4 | `<TIPO>` que no son tres letras mayúsculas seguidas de `,` o del `*` | `err_trama` |
| 4 | `<TIPO>` bien formado pero no esperado en esta dirección (en Linux: distinto de `ENV`, `STA` y `BTN`, incluidos `SYS` y `ACK`) | `desconocidas` |
| 5 | Tipo conocido con número de campos incorrecto o algún campo fuera del formato de §3 | `err_trama` |

Los bytes descartados antes de un `$` (regla 1) no suman ningún contador. Las tramas perdidas por `seq` se cuentan aparte (§3, «Detección de tramas perdidas y reinicios»).

## 3. Mensajes STM32 → Milk-V

### `ENV`: medición (cada 1 s)

```
$ENV,<seq>,<temp>,<hr>,<estado>*<CS>
```

| Campo | Formato | Descripción |
|---|---|---|
| `seq` | 8 dígitos decimales | Consecutivo; vuelve a 0 después de 99999999 (~3 años a 1 Hz). Sirve para detectar tramas perdidas. |
| `temp` | signo + 5 dígitos | Temperatura en centésimas de °C. `+02450` = 24,50 °C; `-00520` = −5,20 °C. |
| `hr` | 5 dígitos, ≤ 10000 | Humedad relativa en centésimas de %. `05310` = 53,10 %. El máximo es `10000` (100,00 %); un valor mayor se descarta como error de trama. |
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
| `uptime_s` | 1 a 10 dígitos decimales (valor ≤ 4294967295): segundos desde el último arranque del STM32 |
| `err_i2c` | 1 a 10 dígitos decimales (valor ≤ 4294967295): errores I²C acumulados desde el arranque |
| `causa_reset` | 1 dígito, 0–4. Leída de `RCC_CSR`: 0 = encendido/BOR, 1 = pin NRST, 2 = watchdog IWDG, 3 = software, 4 = otra |
| `fw` | Versión del firmware, `MAJ.MIN.PAT`: tres números decimales de al menos un dígito separados por `.`, con un máximo de 11 caracteres (por ejemplo `0.2.0` o `10.200.3000`). *(Propuesta v0.3, pendiente de confirmar por Andrew en la revisión del PR.)* |
| `lazo_max_us` | 5 dígitos exactos: duración máxima de una iteración del lazo principal desde el último `STA`, en µs (medida con un temporizador o DWT `CYCCNT`); se satura en 99999. Se reinicia tras cada envío. *(v0.2, RF-07)* |
| `pila_libre_b` | 5 dígitos exactos: bytes de pila nunca usados desde el arranque (la pila se rellena con un patrón al iniciar y se busca el primer byte modificado). *(v0.2, RF-07)* |

Longitud típica: 40 bytes, dentro del límite de 64.

Los anchos de `uptime_s`, `err_i2c` y `causa_reset` se fijaron en la v0.3; el límite de `fw` es una propuesta de la misma versión. Un campo fuera de estos formatos se descarta como error de trama.

### `BTN`: solicitud de factory reset

```
$BTN,RST*<CS>
```

Se envía cuando el botón de reset lleva 10 s presionado. Se repite cada 1 s hasta recibir `$ACK,RST`, como máximo 5 veces. Si no llega el ACK, el STM32 indica el fallo con los LED (ver §5) y no reintenta hasta que el botón se suelte y se vuelva a presionar.

### Detección de tramas perdidas y reinicios

Linux detecta tramas `ENV` perdidas a partir de `seq` (RF-12, PU-06). Solo cuentan las tramas aceptadas; una trama descartada no cambia la referencia.

1. El primer `ENV` recibido solo fija la referencia.
2. El avance se calcula módulo 10^8: `avance = (seq − anterior) mod 100000000`, y las tramas perdidas son `avance − 1`. Por eso 99999999 → 0 es un avance normal de 1: es el desbordamiento natural, no una pérdida ni un reinicio.
3. Un `seq` repetido (avance 0) no cuenta.
4. Un `STA` con `uptime_s` menor que el del `STA` anterior indica que el STM32 se reinició. El siguiente `ENV` se cuenta desde 0: con `seq` = 0 no hay pérdida; con `seq` = n se cuentan n tramas perdidas.
5. Un avance mayor que medio rango (> 5·10^7) sin `STA` de reinicio se interpreta como un retroceso: el STM32 se reinició y su `STA` se perdió. No suma a las tramas perdidas, el `ENV` se toma como nueva referencia y el reinicio se registra aparte. El desbordamiento natural (avance 1) nunca cae en este caso.

| Caso | Secuencia recibida | Tramas perdidas | Reinicio registrado aparte |
|---|---|---|---|
| Hueco | `ENV` seq 1000, 1001, 1004 | 2 (1002 y 1003) | No |
| Desbordamiento natural | `ENV` seq 99999998, 99999999, 0, 1 | 0 | No |
| Hueco que cruza el desbordamiento | `ENV` seq 99999997, 2 | 4 (99999998, 99999999, 0 y 1) | No |
| `seq` repetido | `ENV` seq 42, 42, 43 | 0 | No |
| Reinicio con `STA` | `STA` uptime_s 3600; `ENV` seq 5000; `STA` uptime_s 3; `ENV` seq 0, 1 | 0 | No (lo indica el `STA`) |
| Reinicio con `STA` y pérdidas | `STA` uptime_s 3600; `ENV` seq 5000; `STA` uptime_s 3; `ENV` seq 2 | 2 (0 y 1) | No (lo indica el `STA`) |
| Retroceso sin `STA` | `ENV` seq 5000, 5001, 0, 1 (se perdió el `STA` del arranque) | 0 | Sí, 1 |

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

Confirma que Linux recibió la solicitud de factory reset **y persistió el marcador `reset.pending` necesario para completarlo después de un corte de alimentación**. Si Linux no puede persistir el marcador, no debe responder con `ACK`; el STM32 seguirá los reintentos especificados.

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

## 7. Pendiente

- Diferenciar en `env_parse` (firmware/src/env_protocol.c) los resultados OK / CHECKSUM / TRAMA, manteniendo compatibilidad con la API actual (propuesta aceptada por Andrew, sin fecha).
