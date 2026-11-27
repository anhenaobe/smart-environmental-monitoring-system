# Matriz de verificación

Versión: 0.5 (adaptación DHT11, sin cambios en estados de prueba)

**Tipos:** PU = unitaria · PI = integración · PB = bring-up · PF = factory reset · PS = sistema / aceptación.
**Estado:** Pendiente · En curso · Aprobada · Falló · Bloqueada. Un resultado negativo se conserva: no se borra ni se reescribe, se agrega una nueva ejecución.

Regla del equipo: **quien implementa no verifica formalmente su propia funcionalidad.** El equipo tiene tres integrantes (Andrew, Mateo y Juan Fernando). En pruebas integrales se identifican los implementadores del subsistema evaluado y se asigna otro verificador.

Las pruebas unitarias corren en el host (x86) sobre los módulos de lógica pura, sin HAL ni hardware. Las de integración y sistema corren sobre el hardware real salvo que la herramienta diga "host".

**Caso de uso para aceptación:** una sonda en un gabinete documental pequeño. Los ensayos demuestran el funcionamiento del sistema, no la preservación material ni uniformidad espacial. Ver [criterios-conservacion.md](criterios-conservacion.md).
## 1. Pruebas unitarias

| ID | Req. | Prueba | Criterio de aceptación | Implementa | Verifica | Herramienta | Estado |
|---|---|---|---|---|---|---|---|
| PU-01 | RF-02 | Suma módulo 256 del DHT11 con vectores válidos y alterados | Bytes 53, 0, 24, 4 → 81; alterar un byte sin ajustar la suma se rechaza | Andrew | Juan Fernando | Unity (host) | Pendiente |
| PU-02 | RF-01 | Decodificación DHT11 a centésimas de °C y % HR | Vectores de signo, fracción, límites de V1.3 y formato inválido; 24,4 °C/53 % HR → 2440/5300 | Andrew | Juan Fernando | Unity (host) | Pendiente |
| PU-03 | RF-10 | Generación de las tramas `ENV` y `STA` en el firmware | Coinciden byte a byte con los vectores de [protocolo-uart.md](protocolo-uart.md), incluidos el checksum, los valores negativos y los campos vacíos | Andrew | Juan Fernando | Unity (host) | Pendiente |
| PU-04 | RF-11 | Parser de Linux: tramas válidas de todos los tipos | Todos los campos se decodifican correctamente | Mateo | Juan Fernando | CMocka/Unity (host) | Pendiente |
| PU-05 | RF-11, RF-62 | Parser de Linux: checksum malo, trama truncada, > 64 bytes, basura antes de `$`, tipo desconocido, 10 000 tramas aleatorias | Se rechazan, el contador correcto aumenta y no hay fallos ni accesos inválidos | Mateo | Juan Fernando | CMocka/Unity + Valgrind (host) | Pendiente |
| PU-06 | RF-12 | Detección de tramas perdidas por `seq`, incluido el desbordamiento 99999999 → 0 y el reinicio del STM32 (`seq` vuelve a 0 con un `STA` nuevo) | El número de tramas perdidas es correcto; el desbordamiento y el reinicio no cuentan como pérdida | Mateo | Juan Fernando | CMocka/Unity (host) | Pendiente |
| PU-07 | RF-43 | Guardado atómico de la configuración con inyección de fallas: abortar antes de `fsync`, antes de `rename` y después | En todos los casos queda la configuración anterior o la nueva, completa; nunca un archivo truncado | Mateo | Andrew | CMocka/Unity (host) | Pendiente |
| PU-08 | RF-60, RF-61 | Lectura de métricas desde copias de `/proc/stat`, `/proc/meminfo`, `/proc/net/dev`, `/proc/net/tcp` tomadas de la Duo S | Los valores calculados coinciden con los esperados para cada archivo de prueba | Juan Fernando | Mateo | CMocka/Unity (host) | Pendiente |
| PU-09 | RF-21 | Agregación por intervalo (promedio, mínimo, máximo) | Coincide con un cálculo de referencia en hoja de cálculo; ignora las lecturas con estado ≠ 0; un intervalo sin lecturas válidas se registra vacío | Mateo | Andrew | CMocka/Unity (host) | Pendiente |
| PU-10 | RF-51 | Máquina de estados del botón: rebote, pulsación corta, sostenido 9 s, sostenido 10 s | `BTN,RST` solo a los 10 s; los rebotes < 50 ms no cuentan; soltar a los 9 s no lo genera | Andrew | Juan Fernando | Unity (host), reloj simulado | Pendiente |
| PU-11 | RF-22 | Rotación diaria y retención con reloj simulado (8 días, cambio de mes) | Se crea un archivo por día; con retención 7 queda exactamente el día actual y los 6 anteriores | Mateo | Juan Fernando | CMocka/Unity (host) | Pendiente |
| PU-12 | RF-53 | Factory reset con inyección de falla antes y después del marcador persistente y de cada borrado | Antes del marcador conserva la configuración; después, al reiniciar, completa el borrado y entra a bring-up sin datos previos | Mateo | Juan Fernando | CMocka/Unity (host) | Pendiente |
| PU-13 | RF-24 | Histéresis y persistencia de la alarma con secuencias sintéticas | 4 lecturas fuera no activan; 5 sí; la alarma no se apaga hasta superar la histéresis | Mateo | Andrew | CMocka/Unity (host) | Pendiente |
| PU-14 | RF-42 | Validación de la configuración: cada campo vacío, fuera de rango, mín ≥ máx, contraseña de 7 caracteres, caracteres especiales en el nombre | Todos se rechazan con el código de error del campo correcto; una configuración válida se acepta | Mateo | Andrew | CMocka/Unity (host) | Pendiente |
| PU-15 | RF-32 | Verificación de la contraseña con hash (`crypt` SHA-512) | La correcta se acepta; una distinta, vacía o con un carácter cambiado se rechaza; el archivo no contiene la contraseña en claro | Mateo | Juan Fernando | CMocka/Unity (host), `grep` | Pendiente |

## 2. Pruebas de integración

| ID | Req. | Prueba | Criterio de aceptación | Implementa | Verifica | Herramienta | Estado |
|---|---|---|---|---|---|---|---|
| PI-01 | — | Bring-up del hardware de la Milk-V: arranque desde la microSD, consola, Ethernet, red USB y UART dedicada (lazo TX→RX) | Shell accesible; `ping` responde; un byte enviado por la UART dedicada vuelve por el lazo | Andrew | Mateo | Terminal serie, `ping`, `stty`, cable de lazo | Pendiente |
| PI-02 | RF-01 | STM32 + sensor: lectura y salida por la consola de depuración | Una trama real cada 3 s; valores plausibles y dentro de la tolerancia RNF-07 frente al instrumento de referencia | Andrew | Juan Fernando | ST-LINK, terminal serie, instrumento de referencia | Pendiente |
| PI-03 | RF-10 | STM32 → Linux con datos ficticios durante 10 min | Linux recibe el 100 % de las tramas (600 `ENV`, 0 errores); el analizador lógico confirma 115200 baudios ±2 % | Andrew + Mateo | Juan Fernando | `cat`/`stty` en Linux, analizador lógico | Pendiente |
| PI-04 | RF-13 | Linux → STM32: latido `SYS` | El LED cambia según `SYS`; detener el latido produce el patrón "sin Linux" en ≤ 6 s | Andrew + Mateo | Juan Fernando | Terminal serie, cronómetro | Pendiente |
| PI-05 | RF-03, RF-04, RF-25 | Desconexión y reconexión del sensor en caliente | Estado 1 en ≤ 4 s; `medicion.valida=false` al recibir trama inválida o a los 5 s desde la última válida; lecturas válidas en ≤ 7 s tras reconectar; `uptime_s` del STM32 no se reinicia | Andrew + Mateo | Juan Fernando | Terminal serie, navegador | Pendiente |
| PI-06 | RF-05 | Watchdog: forzar un bloqueo del lazo principal (firmware de prueba con comando de bloqueo) | El STM32 se reinicia y `STA` reporta `causa_reset = 2` | Andrew | Juan Fernando | ST-LINK, terminal serie | Pendiente |
| PI-07 | RF-20 | Arranque automático y reinicio supervisado del servicio | Tras reiniciar la placa, recibe tramas sin intervención; un supervisor compatible con la imagen lo reinicia tras `kill -9` en ≤ 5 s | Mateo | Andrew | `ps`, `kill`, registros del servicio | Pendiente |
| PI-08 | RF-30 | Interfaz web: valores actuales e historial | Los valores de la web coinciden con las tramas en ≤ 2 s; la gráfica muestra 24 h de datos de prueba | Mateo + Juan Fernando | Andrew | Navegador, `curl` | Pendiente |
| PI-09 | RF-23 | Alarma por umbral para gabinete documental | Medir temperatura ambiente T y configurar **solo para la prueba** el máximo T+1 °C; elevar la temperatura medida hasta **>T+1 °C** mediante perturbación controlada sin exponer documentos. Confirmar alarma en `/api/estado` y LED tras 5 lecturas válidas si RF-24 se implementa (perturbación suficiente para superar la cuantización), y desaparición al recuperar el rango. Registrar umbral, lecturas, tiempos y referencia; RF-24 tiene prueba de histéresis independiente (PU-13). | Andrew + Mateo | Juan Fernando | Terminal, navegador y termohigrómetro | Pendiente |
| PI-10 | RF-60 a RF-63 | Página de diagnóstico frente a herramientas del sistema | CPU y memoria dentro de ±5 puntos de `top`/`free`; bytes de red iguales a `/proc/net/dev`; puertos iguales a `ss -tulpn` (o `netstat`); contadores UART iguales a los inyectados | Juan Fernando | Mateo | `top`, `free`, `ss`, navegador | Pendiente |
| PI-11 | RNF-03 | Fugas de memoria del servicio con tramas reales y erróneas | Valgrind sin "definitely lost" ni "invalid read/write" tras 30 min con un puerto serie virtual | Mateo | Juan Fernando | Valgrind + `socat` (host) | Pendiente |
| PI-12 | RF-06 | STM32 sin Linux: detener `envmond` durante 2 min | El STM32 sigue enviando `ENV` cada 3 s en modo normal (sin saltos de `seq` al leer con `cat`) y muestra el patrón "sin Linux" | Andrew | Mateo | `kill` / script de inicio, terminal serie | Pendiente |
| PI-13 | RF-31 | Patrones de los LED: inyectar cada `SYS` y cada falla desde una terminal serie | Cada estado muestra el patrón de §5 del protocolo; frecuencias medidas dentro de ±10 % (1 Hz, 5 Hz); con dos estados a la vez gana el de mayor prioridad | Andrew | Juan Fernando | Terminal serie, analizador lógico en los pines de los LED | Pendiente |
| PI-14 | RF-32 | Página de ajustes | Sin contraseña o con una incorrecta no se guarda nada; con la correcta los umbrales nuevos se aplican en ≤ 5 s | Mateo | Andrew | Navegador, `curl` | Pendiente |
| PI-15 | RF-64 | Consumo propio de `envmond` en la página de diagnóstico | RSS, CPU y descriptores coinciden con `/proc/<pid>/status`, `top` y `ls /proc/<pid>/fd` | Juan Fernando | Mateo | `/proc`, `top` | Pendiente |
| PI-16 | RF-07, RF-63 | Instrumentación del STM32 | `lazo_max_us` coincide (±10 %) con el ancho de un pin de prueba que sube y baja en cada iteración; `pila_libre_b` baja al llamar una función de prueba con un arreglo local grande | Andrew | Juan Fernando | Analizador lógico, terminal serie | Pendiente |

## 3. Pruebas de bring-up

| ID | Req. | Prueba | Criterio de aceptación | Implementa | Verifica | Herramienta | Estado |
|---|---|---|---|---|---|---|---|
| PB-01 | RF-40, RF-41 | Arranque sin configuración | LED verde a 1 Hz; toda URL de la web redirige al formulario; los umbrales **18–22 °C y 35–50 % HR** están sugeridos pero se pueden editar | Andrew + Mateo | Juan Fernando | Observación, navegador | Pendiente |
| PB-02 | RF-42 | Datos inválidos en el formulario (los mismos casos de PU-14, desde el navegador) | Se rechazan con un mensaje junto al campo; no se crea `/etc/envmon/config` | Mateo | Andrew | Navegador, `ls` | Pendiente |
| PB-03 | RF-41, RF-44 | Configuración completa | Pasa a funcionamiento normal (LED verde fijo) en ≤ 5 s sin reiniciar; la web muestra la medición actual | Andrew + Mateo | Juan Fernando | Navegador, cronómetro | Pendiente |
| PB-04 | RF-43 | Corte de energía durante el guardado (10 repeticiones). Complementa a PU-07, que cubre los puntos exactos de falla | El equipo siempre arranca en un estado coherente (bring-up o configurado) | Mateo | Juan Fernando | Desconexión física | Pendiente |
| PB-05 | RF-45 | Bring-up hecho por una persona ajena al equipo usando solo el manual impreso | Lo completa sin ayuda en ≤ 10 min; se anotan los puntos confusos y se corrige el manual | Juan Fernando | Persona externa | Manual de usuario, cronómetro | Pendiente |
| PB-06 | RF-46 | Acceso a la dirección del bring-up desde un computador que nunca se conectó al equipo | La dirección de la etiqueta abre el formulario sin configurar nada en el computador (ni IP manual ni buscar en el router) | Andrew + Mateo | Juan Fernando | Navegador en Windows y Linux | Pendiente |

## 4. Pruebas de factory reset

| ID | Req. | Prueba | Criterio de aceptación | Implementa | Verifica | Herramienta | Estado |
|---|---|---|---|---|---|---|---|
| PF-01 | RF-51 | Botón presionado 3 s y 9 s | No hay reset; los LED vuelven a su estado previo; no se envía `BTN` | Andrew | Mateo | Observación, terminal serie | Pendiente |
| PF-02 | RF-50, RF-52, RF-55 | Botón presionado 10 s | `BTN,RST` → persistir marcador → `ACK,RST` → bring-up en ≤ 30 s; sin configuración, historial ni marcador; la web no tiene opción de reset | Andrew + Mateo | Juan Fernando | Terminal serie, navegador, `ls`, `grep` | Pendiente |
| PF-03 | RF-53 | Corte de energía en medio del reset (10 repeticiones). Complementa a PU-12 | Si el marcador ya es durable, al iniciar completa el reset antes de habilitar la web; si no, conserva la configuración | Mateo | Juan Fernando | Desconexión física | Pendiente |
| PF-04 | RF-54 | Reset con el servicio Linux detenido | 5 intentos de `BTN` a 1 s; después, el patrón "sin ACK" | Andrew | Mateo | Observación, terminal serie | Pendiente |
| PF-05 | RF-40, RF-52 | Ciclo completo: bring-up → uso 1 h → reset → bring-up, 3 veces | Cada ciclo funciona igual que el primer arranque | Andrew + Mateo | Juan Fernando | Navegador, observación | Pendiente |

## 5. Pruebas de sistema

| ID | Req. | Prueba | Criterio de aceptación | Implementa | Verifica | Herramienta | Estado |
|---|---|---|---|---|---|---|---|
| PS-01 | RNF-04 | Funcionamiento continuo durante 24 h | Sin reinicios inesperados (`uptime` y `causa_reset`); tramas perdidas < 0,1 %; RSS de `envmond` sin crecimiento sostenido | Andrew + Mateo | Juan Fernando | Página de diagnóstico, registros | Pendiente |
| PS-02 | RNF-07 | Exactitud frente al instrumento de referencia | Diferencia absoluta ≤2 °C y ≤5 puntos HR en 3 condiciones de ensayo (ambiente, calor, humedad); documentar referencia, variante, alimentación e incertidumbre | Andrew | Mateo | Instrumento de referencia | Pendiente |
| PS-03 | RNF-06 | Sesgo térmico por la carcasa | Temperatura con carcasa vs. sin carcasa tras 1 h: diferencia ≤ 0,5 °C medida con referencia adecuada (DHT11 por sí solo no demuestra este límite) | Juan Fernando | Andrew | Instrumento de referencia | Pendiente |
| PS-04 | RNF-08 | Escrituras en el almacenamiento durante 1 h | ≤ 60 operaciones de escritura sobre la partición de datos en 1 h (`/proc/diskstats`, campo de escrituras completadas) | Mateo | Juan Fernando | `/proc/diskstats`, `strace -e trace=write` en el host | Pendiente |
| PS-05 | RNF-01, RNF-02 | Inspección de lenguajes y dependencias | `file` muestra binarios ELF RISC-V y ARM; `ldd envmond` solo lista la libc; `ps` no muestra intérpretes del proyecto | Andrew + Mateo | Juan Fernando | `file`, `ldd`, `ps`, revisión del repositorio | Pendiente |
| PS-06 | RNF-05 | Consumo eléctrico en reposo y en carga | Corriente medida y documentada; queda dentro de lo que entrega la fuente con al menos 20 % de margen | Juan Fernando | Andrew | Medidor USB de corriente | Pendiente |
| PS-07 | RNF-09 | Inspección de la carcasa con lista de chequeo | Botón, LED, Ethernet y alimentación accesibles; el botón no se presiona al apoyar la caja; etiqueta con la dirección del bring-up | Juan Fernando | Andrew + Mateo | Lista de chequeo, fotos | Pendiente |
| PS-08 | RNF-10 | Tiempo de arranque (10 repeticiones) | Desde la conexión hasta que la web responde y los LED muestran estado: ≤ 90 s en las 10 | Andrew + Mateo | Juan Fernando | Cronómetro, `curl` en lazo desde el PC | Pendiente |
| PS-09 | RNF-11 | Presupuesto de recursos de `envmond` durante 1 h | RSS ≤ 8 MB; CPU promedio ≤ 5 % | Mateo | Juan Fernando | `/proc/<pid>/status`, `top -b` | Pendiente |
| PS-10 | RNF-12 | Web sin internet y en celular | Con el equipo y el celular aislados de internet, todas las páginas cargan completas; `grep` no encuentra URLs externas en `web/` | Juan Fernando | Mateo | Celular, navegador, `grep` | Pendiente |
| PS-11 | RNF-13 | Tamaño del firmware | `arm-none-eabi-size`: flash ≤ 128 KiB y RAM estática ≤ 32 KiB para L432KC; flash ≤ 64 KiB y RAM estática ≤ 20 KiB para L412KB | Andrew | Juan Fernando | `arm-none-eabi-size`, archivo `.map` | Pendiente |

## 6. Herramientas de inspección

| Herramienta | Dónde corre | Para qué | Pruebas |
|---|---|---|---|
| Unity / CMocka | Host x86 | Pruebas unitarias en C de los módulos sin hardware (firmware y servicio) | PU-01 a PU-15 |
| Valgrind (memcheck) | Host x86 | Fugas y accesos inválidos del servicio | PU-05, PI-11 |
| `socat` | Host x86 | Par de puertos serie virtuales para alimentar el servicio con tramas sin hardware | PI-11 |
| `gcov` | Host x86 | Cobertura de las pruebas unitarias (meta: ≥ 80 % de líneas en parser, configuración y reset) | PU-* |
| SystemTap | Host x86 (en la Duo S, solo si la imagen y el kernel lo permiten) | Trazar llamadas a `write`/`fsync` y accesos al puerto serie | PS-04 |
| `strace` | Host / Duo S | Alternativa ligera a SystemTap para contar llamadas al sistema | PS-04 |
| `/proc`, `top`, `free`, `ss` | Duo S | Referencia para comparar la página de diagnóstico | PI-10, PI-15, PS-09 |
| Analizador lógico | Banco | Velocidad de la UART, frecuencia de los LED, tiempo del lazo del STM32 | PI-03, PI-13, PI-16 |
| ST-LINK + `arm-none-eabi-size` | Banco / PC | Programación, depuración y tamaño del firmware | PI-02, PI-06, PS-11 |
| Instrumento de referencia (termohigrómetro) | Banco | Exactitud de la medición | PI-02, PS-02, PS-03 |

## 7. Registro de ejecuciones

Cada ejecución se anota en `tests/registro.md` (o como comentario en el PR) con: ID de la prueba, fecha, commit del firmware y del servicio, quién la ejecutó, resultado (Aprobada / Falló / Bloqueada) y enlace a la evidencia (log, captura, foto o video). La columna **Estado** de esta matriz solo cambia cuando hay evidencia enlazada.
