# Guía de estudio de `envmond`

Versión: Fase B (servicio: `uart`, `reset`, `servicio`, `envmond`, systemd) sobre la Fase A (lógica: `config`, `alarm`, `logger`) · 2026-11-27 · crece en la Fase C (web).

Esta guía explica **qué hace** el servicio Linux `envmond`, **por qué** está hecho así y **qué alternativas** se descartaron, para poder defenderlo en la sustentación. Las fuentes de verdad siguen siendo [requerimientos.md](requerimientos.md), [arquitectura.md](arquitectura.md), [interfaces.md](interfaces.md) y [protocolo-uart.md](protocolo-uart.md); aquí no se define nada nuevo del contrato.

## 1. Qué es `envmond` en una frase

Un único proceso en C que corre en la Milk-V Duo S, con un hilo de servicio (más los hilos HTTP de CivetWeb en la Fase C, §6.1): lee las tramas del STM32 por UART, valida cada medición, decide si hay alarma, guarda el historial agregado en la microSD, guarda la configuración del bring-up y la sirve por web.

## 2. Módulos

| Módulo | Estado | Qué hace | Por qué así |
|---|---|---|---|
| `parser` | Integrado (PR #5) | Convierte bytes de la UART en mensajes `ENV`/`STA`/`BTN`, cuenta errores y tramas perdidas por `seq` | No hace E/S ni usa memoria dinámica: se prueba con vectores, sin hardware |
| `metrics` | Integrado (Juan Fernando) | Lee CPU, memoria, red y puertos de `/proc` y `/sys` | RNF-01: sin intérpretes; rutas inyectables para PU-08 |
| `config` | **Fase A** | Valida el formulario, guarda y carga `/etc/envmon/config`, calcula y verifica el hash de la contraseña | El archivo es la única fuente del modo (bring-up o normal); el guardado no puede dejarlo truncado (RF-43) |
| `alarm` | **Fase A** | Decide si la medición es válida y fresca ("sin datos") y si hay alarma de temperatura o de HR | Lógica pura con reloj inyectado: se prueban segundos y minutos en microsegundos |
| `logger` | **Fase A** | Agrega las lecturas por intervalo (promedio, mínimo, máximo) y escribe una fila por intervalo en un CSV diario; aplica la retención | Escribir cada lectura gastaría la microSD (RNF-08) |
| `uart` | **Fase B** | Abre el puerto a 115200 8N1 en modo crudo y no bloqueante; arma `$<carga>*CS\r\n` y encola la salida | `poll` necesita un descriptor que nunca bloquee; la salida se vacía con `POLLOUT` |
| `reset` | **Fase B** | Marcador `reset.pending` durable y borrado idempotente (RF-52, RF-53) | Que un corte en cualquier punto deje un estado coherente (PU-12) |
| `servicio` | **Fase B** | Núcleo: modos bring-up, normal y reset; `SYS` cada 1 s; `BTN`/`ACK`; instantánea para `/api/estado` | Sin E/S directa ni relojes propios: se prueba entero con relojes simulados |
| `envmond.c` | **Fase B** | `main`: opciones, `signalfd`, lazo `poll`, reapertura de la UART | Solo E/S; toda la lógica está en `servicio` |
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
| Temperatura | −20 a 60 °C (rango del DHT11 V1.3); mín < máx y máx − mín ≥ 1,0 °C |
| HR | 5 a 95 % (rango del DHT11 V1.3); mín < máx y máx − mín ≥ 4 puntos |
| Retención | 1 a 30 días (RF-22) |

El error de "mín ≥ máx" (`ORDEN`) y el de rango demasiado estrecho (`ESTRECHO`) se marcan en el campo **máximo**.

**Rango mínimo (decisión de validación de Mateo).** Se exige `máx − mín ≥ 2 × histéresis` de RF-24 (1,0 °C y 4 puntos de HR). Con un rango menor, la banda de desactivación `[mín + h ; máx − h]` queda vacía y la alarma no se apagaría nunca. Ejemplo: con 20,0–20,5 °C haría falta una lectura ≥ 20,5 y ≤ 20,0 a la vez. No es una cifra de conservación; se deriva de la histéresis. Por qué se rechazan `=` y saltos de línea: el archivo es `clave=valor` por línea; un nombre con `\n` permitiría inyectar otra clave. `<>&"'` se rechazan para no tener que escapar el nombre en HTML.

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
- **Hora no válida:** si el reloj marca antes del 2026-01-01 (la Duo S no tiene batería de RTC), la fila lleva `hora_ok = 0` y va a un archivo con esa fecha, por ejemplo `1970-01-01.csv`. **La retención no borra los archivos con fecha anterior a 2026-01-01** (decisión de Mateo): se conservan como evidencia y los elimina el factory reset. Si el reloj retrocede, se cierra el intervalo en curso y se abre uno nuevo.
- **Escrituras (RNF-08):** una sola `write` + `fsync` por intervalo, es decir, como máximo una por minuto. Ante un corte se pierde como máximo el intervalo en curso; si quedó una fila a medias, la siguiente empieza en una línea nueva.

## 3. Flujo de una medición, de la UART a la web

1. El STM32 lee el DHT11 cada 3 s y envía `$ENV,00000042,+02450,05310,0*44`.
2. El lazo de `envmond` despierta en `poll()` porque hay bytes en el puerto (por defecto `/dev/ttyS2`) y los lee sin bloquear.
3. `parser_bloque` encuadra la línea, verifica el XOR, decodifica los campos y actualiza los contadores y las tramas perdidas por `seq`.
4. `servicio_bytes` recibe el mensaje: `alarm_env` actualiza la frescura y las rachas; en modo normal, `logger_muestra` suma la lectura al intervalo en curso. En bring-up solo se actualizan los contadores y la frescura (sin historial ni alarmas).
5. `servicio_tick`, en cada vuelta del lazo: cuando el intervalo termina, `logger_tick` escribe la fila en el CSV del día.
6. Cada 1 s `servicio_tick` envía `SYS,<estado>` (0 bring-up, 1 normal, 2 alarma, 3 reset) y el STM32 pone el LED correspondiente. El plazo de `poll` es justo el tiempo hasta el siguiente `SYS`, así el lazo no gira en vano.
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

## 5. Factory reset ante cortes de energía (RF-52, RF-53, PU-12)

Según [interfaces.md §4](interfaces.md), al recibir `$BTN,RST`:

```c
if (reset_marcar(dir_config, ops) != 0) return;   /* sin marcador durable: sin ACK */
enviar("ACK,RST");  enviar("SYS,3");
reset_completar(dir_config, dir_datos, ops);       /* historial -> config -> marcador */
```

1. `reset_marcar` crea `/etc/envmon/reset.pending` y sincroniza el archivo y el directorio. Si algo falla, lo retira y **no** se envía `ACK`; el STM32 reintenta y, tras 5 intentos, avisa con los LED (RF-54).
2. Con el marcador durable se envían `ACK,RST` y `SYS,3`. `SYS,3` se mantiene **2 s** (decisión de Mateo) para que se vea el LED a 5 Hz; después llega `SYS,0`.
3. `reset_completar` borra los archivos de `/var/lib/envmon/` (no el directorio, que crea systemd), sincroniza, borra `config` y `config.tmp`, sincroniza, retira el marcador y sincroniza. Si un paso falla, el marcador sigue y se reintenta cada segundo.
4. **Al arrancar**, `servicio_init` mira primero si existe el marcador y, si existe, completa el borrado **antes** de cargar la configuración o servir la web.

- **Corte antes** de que el marcador sea durable: no hubo `ACK` y se conserva todo.
- **Corte después:** el siguiente arranque ve el marcador y termina. Cada paso es idempotente (borrar lo que ya no está no es error), así que repetirlo desde el principio es seguro.
- **`BTN` repetido** (el `ACK` se perdió): vuelve a marcar, responde `ACK` y completa; no causa daño (decisión de Mateo).
- **Orden:** primero el historial y después `config`. Con cualquier corte, nunca queda "configuración borrada con historial viejo".

**Cómo se prueba (PU-12):** las cuatro operaciones (`crear`, `sincronizar`, `sincronizar_dir`, `borrar`) son inyectables. Un proceso hijo ejecuta marcar → `ACK` → completar y hace `_exit` **antes de la operación número N**; el padre simula el arranque siguiente. Se prueba N = 1 … 13, o sea, un corte antes de cada operación. En todos los casos el resultado es "todo intacto" o "bring-up limpio", y si hubo `ACK` siempre es bring-up limpio. Como en PU-07, la durabilidad física en la microSD queda para PF-03.

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

### 6.1 Decisiones de la Fase B

| Decisión | Alternativa descartada | Motivo |
|---|---|---|
| **Un hilo de servicio + hilos HTTP de CivetWeb con mutex** (decisión de Mateo) | Servidor HTTP propio dentro de `poll`; Mongoose | CivetWeb (MIT) crea sus propios hilos y no se integra en un `poll` ajeno. El hilo del lazo es el único dueño de UART, alarma, historial y reset; los manejadores HTTP solo copian una instantánea o dejan una petición de configuración bajo un mutex corto, con `num_threads` mínimo. El servidor propio agregaba código HTTP a probar y defender, y Mongoose (GPLv2/comercial) plantea dudas de licencia con GPL-3.0 |
| Núcleo `servicio` sin E/S, con `tx` y relojes inyectados | Lógica dentro de `main` | Las transiciones (bring-up → normal → reset → bring-up) se prueban en milisegundos |
| `signalfd` + señales bloqueadas | Manejador de señal con variable global | La señal llega como un descriptor más del `poll`; bloquearla antes de crear hilos evita que la reciba un hilo HTTP |
| Reabrir la UART cada 5 s si falla | Terminar el proceso | Un cable flojo no debe tumbar la web; systemd sigue cubriendo las caídas reales |
| systemd con `Restart=always`, `RestartSec=1`, `StartLimitIntervalSec=0` | Script de inicio; BusyBox `init` | La imagen Debian usa systemd; un script no reinicia tras `kill -9`. Sin `StartLimitIntervalSec=0`, 5 caídas en 10 s detendrían el servicio para siempre |
| Usuario `envmon` (grupo `dialout`), `NoNewPrivileges`, `ProtectSystem=strict` | Correr como root | Un fallo en la web (Fase C) no puede escribir fuera de `/etc/envmon` y `/var/lib/envmon` |

### 6.2 Cómo se comprobó que el servicio puede escribir bajo `ProtectSystem=strict`

En el manual `systemd.exec(5)` de systemd 257 (el instalado en el equipo de desarrollo):

- `ProtectSystem=strict` monta **todo** el sistema de archivos en solo lectura, salvo `/dev`, `/proc` y `/sys`. Lo excluyen las rutas de `ReadWritePaths=` y los directorios de `StateDirectory=`, `ConfigurationDirectory=` y afines.
- `StateDirectory=envmon` crea `/var/lib/envmon` **con dueño `envmon`**.
- `ConfigurationDirectory=envmon` crea `/etc/envmon` y lo deja escribible, pero el manual aclara: *"Except in case of `ConfigurationDirectory=`, the innermost specified directories will be owned by the user and group specified in `User=`"*. Es decir, `/etc/envmon` quedaría de root y `envmon` no podría escribir. Por eso:
  - `envmond.tmpfiles` (`d /etc/envmon 0750 envmon envmon -`) pone el dueño; `tmpfiles.d(5)` dice que `d` ajusta modo y dueño;
  - `ReadWritePaths=/etc/envmon /var/lib/envmon` lo deja explícito.
- `systemd-analyze verify` sobre la unidad no reporta directivas desconocidas.
- **Pendiente en la Duo S** (sin `sudo` en el equipo de desarrollo no se pudo instalar): `systemctl start envmond`, comprobar con `ls -l` el dueño de ambos directorios, guardar una configuración, ejecutar un factory reset y hacer `kill -9` (PI-07).

## 7. Preguntas probables del profesor

1. **¿Qué pasa si se va la luz mientras se guarda la configuración?** Queda la anterior o la nueva, completa, nunca una mezcla: se escribe un temporal, se sincroniza y se renombra de forma atómica. PU-07 lo prueba fallando en cada paso y PB-04 con cortes reales.
2. **¿Por qué la alarma necesita 5 lecturas y no una?** Para no alarmar por una lectura aislada o por ruido; con `ENV` cada 3 s son unos 12 a 15 s. La histéresis evita que la alarma parpadee cuando el valor queda justo en el límite.
3. **¿Cómo guardan la contraseña?** Nunca en claro: se guarda `crypt()` SHA-512 con una sal aleatoria de 16 caracteres; para verificar se recalcula el hash y se compara en tiempo constante. PU-15 comprueba con `grep` que la contraseña no aparece en el archivo.
4. **¿Por qué no escriben cada medición en la microSD?** Serían 28 800 escrituras al día; agregando por minuto son 1440 (20 veces menos), lo que reduce el desgaste (RNF-08) y basta para la gráfica de 24 h.
5. **¿Qué significa "sin datos" y cuándo aparece?** Que la web no muestra un valor viejo como si fuera actual: aparece si la última trama fue inválida o si pasaron más de 5 s sin una válida. Con `ENV` cada 3 s, una sola trama perdida lo produce un momento; está documentado y se propondrá ajustar el umbral.

**Fase B:**

6. **¿Qué pasa si se va la luz durante un factory reset?** Si el corte ocurre antes de que `reset.pending` sea durable, no hubo `ACK` y todo se conserva. Si ocurre después, el siguiente arranque ve el marcador y termina el borrado antes de servir la web. PU-12 corta antes de cada una de las 13 operaciones de archivo.
7. **¿Por qué no se envía el `ACK` apenas llega el `BTN`?** Porque el `ACK` le dice al STM32 que el reset quedó garantizado; si se enviara antes de persistir el marcador y se fuera la luz, el usuario creería que el equipo se restableció sin que fuera así.
8. **¿Cómo atiende el servicio la UART, el latido y la web sin bloquearse?** Con un lazo `poll()`: la UART es no bloqueante y el plazo de `poll` es el tiempo hasta el siguiente `SYS`. La web corre en hilos de CivetWeb que solo leen una copia del estado bajo un mutex corto.
9. **¿Qué pasa si `envmond` se cae?** systemd lo reinicia en 1 s (`Restart=always`, sin límite de reinicios). Mientras tanto, el STM32 sigue midiendo y, a los 5 s sin `SYS`, muestra "sin Linux" (RF-06). PI-07 lo verifica con `kill -9`.
10. **¿Cómo probaron todo sin la placa?** `socat` crea un par de pseudoterminales; `envmond` abre uno y un STM32 simulado en C abre el otro. Con el reloj real se comprueban bring-up, `SYS,1`, la alarma tras 5 lecturas, los contadores, el reset con `ACK` y `SYS,3` durante 2 s, y el arranque con `reset.pending`, también bajo Valgrind.

## 8. Pendientes y limitaciones conocidas

- **"Sin datos" momentáneo** con una trama perdida (§2.2): pendiente de acordar con Andrew un umbral de ~7 s.
- La **verificación formal** de PU-07, PU-09, PU-11, PU-13, PU-14 y PU-15 corresponde a Juan Fernando (la de PU-07, PU-09, PU-13 y PU-14 se le reasignó el 2026-11-27 por disponibilidad de Andrew). Las pruebas de esta fase son de desarrollo.
- Los archivos sin hora válida no tienen retención: si el equipo funciona mucho tiempo sin NTP ni hora del navegador, crecen sin límite (unos 86 kB por día con intervalo de 60 s).
- Falta confirmar en la imagen de la Duo S que `libcrypt` admite `$6$`.
- **Fase B:** la UART por defecto (`/dev/ttyS2`) es solo candidata hasta PI-01; se cambia con `systemctl edit envmond` (`Environment=ENVMOND_UART=…`).
- **Fase B:** la instalación con systemd, los permisos bajo `ProtectSystem=strict` y el reinicio tras `kill -9` (PI-07) no se ejecutaron en este equipo por falta de `sudo`; se prueban en la Duo S.
- La verificación formal de PU-12 corresponde a Juan Fernando; la integración en host (`make integracion`) es una prueba de desarrollo y no reemplaza PI-03, PI-04, PF-02 ni PF-03 sobre hardware.
