# Guía de estudio de `envmond`

Versión: Fase A (lógica: `config`, `alarm`, `logger`) · 2026-11-27 · crece en las fases B (servicio) y C (web).

Esta guía explica **qué hace** el servicio Linux `envmond`, **por qué** está hecho así y **qué alternativas** se descartaron, para poder defenderlo en la sustentación. Las fuentes de verdad siguen siendo [requerimientos.md](requerimientos.md), [arquitectura.md](arquitectura.md), [interfaces.md](interfaces.md) y [protocolo-uart.md](protocolo-uart.md); aquí no se define nada nuevo del contrato.

## 1. Qué es `envmond` en una frase

Un único proceso en C, de un solo hilo, que corre en la Milk-V Duo S: lee las tramas del STM32 por UART, valida cada medición, decide si hay alarma, guarda el historial agregado en la microSD, guarda la configuración del bring-up y la sirve por web.

## 2. Módulos

| Módulo | Estado | Qué hace | Por qué así |
|---|---|---|---|
| `parser` | Integrado (PR #5) | Convierte bytes de la UART en mensajes `ENV`/`STA`/`BTN`, cuenta errores y tramas perdidas por `seq` | No hace E/S ni usa memoria dinámica: se prueba con vectores, sin hardware |
| `metrics` | Integrado (Juan Fernando) | Lee CPU, memoria, red y puertos de `/proc` y `/sys` | RNF-01: sin intérpretes; rutas inyectables para PU-08 |
| `config` | **Fase A** | Valida el formulario, guarda y carga `/etc/envmon/config`, calcula y verifica el hash de la contraseña | El archivo es la única fuente del modo (bring-up o normal); el guardado no puede dejarlo truncado (RF-43) |
| `alarm` | **Fase A** | Decide si la medición es válida y fresca ("sin datos") y si hay alarma de temperatura o de HR | Lógica pura con reloj inyectado: se prueban segundos y minutos en microsegundos |
| `logger` | **Fase A** | Agrega las lecturas por intervalo (promedio, mínimo, máximo) y escribe una fila por intervalo en un CSV diario; aplica la retención | Escribir cada lectura gastaría la microSD (RNF-08) |
| `uart`, lazo principal, `reset` | Fase B | Abrir el puerto, `poll`, latido `SYS`, factory reset | — |
| `http` (CivetWeb), páginas | Fase C | API JSON y páginas de bring-up y ajustes | — |

### 2.1 `config`

**Qué guarda.** Las nueve claves de [interfaces.md §3](interfaces.md): nombre, `admin_hash`, ntp, intervalo, cuatro umbrales y retención. Los umbrales se manejan en **centésimas enteras** (igual que el parser): `18.0` → `1800`. Así no hay coma flotante ni dependencia del *locale* (en un *locale* español `strtod` esperaría coma).

**Validación (RF-42, PU-14).** `config_desde_formulario` revisa **todos** los campos y devuelve un motivo por campo, para que la web muestre cada error junto a su campo:

```c
config_motivo_t m[CONFIG_N_CAMPOS];
int errores = config_desde_formulario(&form, &cfg, m);
/* m[CONFIG_CAMPO_TEMP_MAX] == CONFIG_MOTIVO_ORDEN si mín >= máx */
```

| Campo | Regla |
|---|---|
| Nombre | 1 a 40 bytes; letras (incluidas las latinas acentuadas en UTF-8), dígitos, espacio y `-_.()`. Se rechazan `=`, saltos de línea, control y `<>&"'` |
| Contraseña | 8 a 64 bytes, sin caracteres de control |
| NTP | Vacío (sin NTP) o nombre de host válido |
| Intervalo | 60 a 3600 s (RF-21) |
| Temperatura | −20 a 60 °C (rango del DHT11 V1.3); mín < máx |
| HR | 5 a 95 % (rango del DHT11 V1.3); mín < máx |
| Retención | 1 a 30 días (RF-22) |

El error de "mín ≥ máx" se marca en el campo **máximo**. Por qué se rechazan `=` y saltos de línea: el archivo es `clave=valor` por línea; un nombre con `\n` permitiría inyectar otra clave. `<>&"'` se rechazan para no tener que escapar el nombre en HTML.

**Carga estricta.** `config_cargar` devuelve `OK`, `NO_EXISTE` (modo bring-up), `INVALIDA` o `ERROR_IO`. Es inválido un archivo que no termina en `\n`, al que le falta una clave, con una clave repetida o desconocida, o con un valor fuera de rango. Con `INVALIDA` el equipo entra en bring-up, **no borra el archivo** y deja el motivo en el log (decisión de Mateo: se conserva la evidencia).

**Hash (RF-32, PU-15).** `crypt()` de la libc con SHA-512: `$6$` + sal de 16 caracteres sacados de `getrandom` + resumen de 86 caracteres. La verificación recalcula `crypt(clave, hash)` y compara en tiempo constante (sin `strcmp`, que se detiene en la primera diferencia).

### 2.2 `alarm`

Recibe cada `ENV` aceptada por el parser y el tiempo monotónico en ms; no lee el reloj ni hace E/S.

- **Activación (RF-24):** cada variable tiene una racha. Una lectura fuera de rango (`v < mín` o `v > máx`; los límites cuentan como dentro) suma 1; con **5** se activa.
- **La racha se interrumpe** con una lectura en rango, una `ENV` inválida (estado 1, 2 o 3) o un hueco de `seq`. Un `seq` repetido no es una lectura nueva y se ignora. El paso 99999999 → 0 es consecutivo.
- **Desactivación con histéresis (RF-24):** la alarma solo se apaga cuando el valor vuelve a `[mín + 0,5 °C ; máx − 0,5 °C]` (o ±2 puntos de HR).

```c
if (v < min || v > max) { if (++racha >= 5) activa = true; }
else { racha = 0; if (activa && v >= min + h && v <= max - h) activa = false; }
```

- **Resolución del DHT11:** el DHT11 entrega grados y porcentajes **enteros**. Con máximo 22 °C, una lectura de 22 °C está en rango pero no supera la histéresis (22 > 21,5), así que la alarma sigue; con 21 °C se apaga. En la práctica los 0,5 °C se comportan como **un escalón entero de 1 °C**, y los 2 puntos de HR como 2 escalones de 1 %. Lo prueba `pu13_resolucion_dht11_escalon_entero`.
- **"Sin datos" (RF-25):** la medición es válida solo si la última `ENV` fue válida **y** llegó hace ≤ `ALARM_FRESCURA_MS` (5000 ms). Una `ENV` inválida da "sin datos" de inmediato. Durante "sin datos" la alarma **conserva** su estado: no se apaga por falta de información.
- **Consecuencia que hay que conocer:** con `ENV` cada 3 s, **una sola trama perdida** deja 6 s sin dato válido y la web muestra "sin datos" entre los 5 y los 6 s. Es lo que pide RF-25 tal como está en `main`; Mateo lo propondrá a Andrew (subir el umbral a ~7 s). Lo documenta la prueba `rf25_una_trama_perdida_da_sin_datos_momentaneo`.
- **Cambio de umbrales** (página de ajustes): las rachas y alarmas se reinician y se evalúan de nuevo con los umbrales nuevos.

### 2.3 `logger`

- **Agregación por tiempo, no por conteo (RF-21, PU-09):** el intervalo (60 s por defecto) se alinea a múltiplos del intervalo en UTC; con `ENV` cada 3 s caben unas 20 lecturas por minuto, pero si se pierden tramas el intervalo sigue durando 60 s.
- Las `ENV` con estado ≠ 0 **no** entran al promedio. Un intervalo sin lecturas válidas se escribe vacío (`n_validas = 0`).
- El promedio se redondea a centésimas con mitades lejos de cero, en enteros.

**Formato del CSV (interno; el contrato con la web será la API JSON de la Fase C):**

```
inicio_utc,hora_ok,n_validas,temp_prom,temp_min,temp_max,hr_prom,hr_min,hr_max
1795770000,1,4,24.55,24.41,24.70,53.15,53.00,53.30
1795770060,1,0,,,,,,
```

- **Rotación diaria (RF-22, PU-11):** el nombre del archivo es la fecha **local** del inicio del intervalo (`AAAA-MM-DD.csv`); al cambiar el día se escribe en otro archivo, sin renombrar nada.
- **Retención:** al arrancar y cada vez que cambia el día local se borran los CSV anteriores a los últimos N días (el día actual cuenta como uno). Con 7 quedan hoy y los 6 anteriores. No toca otros archivos ni fechas futuras.
- **Hora no válida:** si el reloj marca antes del 2026-01-01 (la Duo S no tiene batería de RTC), la fila lleva `hora_ok = 0`. Si el reloj retrocede, se cierra el intervalo en curso y se abre uno nuevo. Ojo: cuando NTP corrige la hora, el archivo `1970-01-01.csv` queda fuera de la retención y se borra el mismo día.
- **Escrituras (RNF-08):** una sola `write` + `fsync` por intervalo, es decir, como máximo una por minuto. Ante un corte se pierde como máximo el intervalo en curso; si quedó una fila a medias, la siguiente empieza en una línea nueva.

## 3. Flujo de una medición, de la UART a la web

1. El STM32 lee el DHT11 cada 3 s y envía `$ENV,00000042,+02450,05310,0*44`.
2. *(Fase B)* El lazo de `envmond` despierta en `poll()` porque hay bytes en `/dev/ttyS*`; los lee sin bloquear.
3. `parser_bloque` encuadra la línea, verifica el XOR, decodifica los campos y actualiza los contadores y las tramas perdidas por `seq`.
4. `alarm_env` actualiza la frescura y las rachas; `alarm_estado` dice si la medición es válida y si hay alarma.
5. `logger_muestra` suma la lectura al intervalo en curso; cuando el intervalo termina, `logger_tick` escribe la fila en el CSV del día.
6. *(Fase B)* Cada 1 s el lazo envía `$SYS,1` (o `2` si hay alarma) y el STM32 pone el LED correspondiente.
7. *(Fase C)* `GET /api/estado` devuelve el estado en RAM; `temp_c` y `hr` van en `null` si `valida = false`.

## 4. Guardado atómico de la configuración (RF-43, PU-07)

El problema: si se escribe `/etc/envmon/config` directamente y se va la luz a la mitad, queda un archivo truncado. La solución estándar en POSIX:

```c
fd = open("config.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0600);
write(fd, ...);  fsync(fd);  close(fd);   /* los datos ya están en la tarjeta */
rename("config.tmp", "config");           /* reemplazo atómico del nombre */
fsync(dir_fd);                            /* el nuevo nombre también es durable */
```

- `rename` dentro del mismo sistema de archivos es **atómico**: cualquier lector ve el archivo viejo completo o el nuevo completo, nunca una mezcla.
- Sin el `fsync` previo, el sistema podría persistir el `rename` antes que los datos y dejar un `config` vacío tras un corte. El `fsync` del directorio hace durable el nuevo nombre.
- `config.tmp` sobrante se ignora al cargar y se sobrescribe en el siguiente guardado. El permiso 0600 se usa porque el archivo contiene el hash.
- **Cómo se prueba:** las cuatro operaciones (`escribir`, `sincronizar`, `renombrar`, `sincronizar_dir`) se pasan como punteros a función. La prueba las sustituye para (a) devolver error en cada paso y (b) simular un corte: un proceso hijo (`fork`) ejecuta el guardado y llama a `_exit` justo en el paso elegido, sin limpiar nada. En todos los casos la carga devuelve la configuración vieja (antes del `rename`) o la nueva completa (después). Además, todos los prefijos posibles del archivo se rechazan como inválidos.
- **Límite:** la prueba unitaria demuestra el orden de las operaciones y la recuperación; la durabilidad real en la microSD la demuestra PB-04 (cortes físicos).

## 5. Factory reset ante cortes de energía (diseño; se implementa en la Fase B)

Según [interfaces.md §4](interfaces.md): al recibir `$BTN,RST`, `envmond` **primero** crea y sincroniza `/etc/envmon/reset.pending` y solo entonces responde `$ACK,RST`. Después borra el historial, borra `config`, sincroniza el directorio y retira el marcador. Al arrancar, si existe el marcador, termina el borrado **antes de servir la web**.

- Corte **antes** de que el marcador sea durable: no hubo `ACK`, el reset no se confirmó y se conserva la configuración. El STM32 reintenta y, tras 5 intentos, avisa con los LED.
- Corte **después**: al arrancar se ve el marcador y se completa el borrado; cada paso es idempotente (borrar algo que ya no está no es error).

Se detallará con la prueba PU-12 en la Fase B.

## 6. Decisiones de diseño y alternativas

| Decisión | Alternativa descartada | Motivo |
|---|---|---|
| Umbrales y valores en centésimas enteras | `double` | Comparaciones exactas en los límites y sin dependencia del *locale* |
| Validación que devuelve un motivo por campo | Devolver solo el primer error | RF-42: el error se muestra junto al campo; el usuario corrige todo de una vez |
| `config.tmp` + `fsync` + `rename` + `fsync(dir)` | Escribir en sitio; SQLite | Es el mecanismo mínimo que garantiza viejo-o-nuevo; SQLite agrega una dependencia |
| Operaciones de archivo inyectables | Probar solo con cortes físicos | Permite fallar en cada paso exacto y de forma repetible (PU-07) |
| `crypt()` SHA-512 con sal de `getrandom` | bcrypt/argon2 con biblioteca externa; SHA-256 sin sal | Viene con la libc (`libcrypt`), tiene sal y miles de rondas; una biblioteca sería otra dependencia en RISC-V |
| Alarma con reloj inyectado | Leer `time()` dentro del módulo | Pruebas deterministas de 5 s, 5 lecturas y minutos sin dormir |
| Agregación por tiempo | Cada 20 tramas | Con tramas perdidas un conteo fijo estiraría el intervalo |
| CSV por día con fecha local | Un archivo único; base de datos | Rotación y retención = borrar archivos; legible con cualquier editor |
| `fsync` por fila | Búfer en RAM de varias filas | Se pierde como máximo un intervalo y se cumple RNF-08 |

## 7. Preguntas probables del profesor

1. **¿Qué pasa si se va la luz mientras se guarda la configuración?** Queda la anterior o la nueva, completa, nunca una mezcla: se escribe un temporal, se sincroniza y se renombra de forma atómica. PU-07 lo prueba fallando en cada paso y PB-04 con cortes reales.
2. **¿Por qué la alarma necesita 5 lecturas y no una?** Para no alarmar por una lectura aislada o por ruido; con `ENV` cada 3 s son unos 12 a 15 s. La histéresis evita que la alarma parpadee cuando el valor queda justo en el límite.
3. **¿Cómo guardan la contraseña?** Nunca en claro: se guarda `crypt()` SHA-512 con una sal aleatoria de 16 caracteres; para verificar se recalcula el hash y se compara en tiempo constante. PU-15 comprueba con `grep` que la contraseña no aparece en el archivo.
4. **¿Por qué no escriben cada medición en la microSD?** Serían 28 800 escrituras al día; agregando por minuto son 1440 (20 veces menos), lo que reduce el desgaste (RNF-08) y basta para la gráfica de 24 h.
5. **¿Qué significa "sin datos" y cuándo aparece?** Que la web no muestra un valor viejo como si fuera actual: aparece si la última trama fue inválida o si pasaron más de 5 s sin una válida. Con `ENV` cada 3 s, una sola trama perdida lo produce un momento; está documentado y se propondrá ajustar el umbral.

## 8. Pendientes y limitaciones conocidas (Fase A)

- **"Sin datos" momentáneo** con una trama perdida (§2.2): pendiente de acordar con Andrew un umbral de ~7 s.
- **Rango más estrecho que la histéresis:** si `máx − mín` es menor que 1,0 °C (o que 4 puntos de HR), la banda de desactivación queda vacía y la alarma nunca se apaga por histéresis. La validación actual solo exige mín < máx; queda como decisión pendiente.
- La **verificación formal** de PU-07, PU-09, PU-13 y PU-14 corresponde a Andrew, y la de PU-11 y PU-15 a Juan Fernando (matriz de verificación). Las pruebas de esta fase son de desarrollo.
- Falta confirmar en la imagen de la Duo S que `libcrypt` admite `$6$`.
