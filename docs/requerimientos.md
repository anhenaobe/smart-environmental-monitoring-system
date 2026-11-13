# Análisis de requerimientos

Versión: 0.2 (borrador para revisión en clase)

Cada requerimiento tiene un identificador único que se usa en la [matriz de verificación](matriz-verificacion.md). Los marcados como **(curso)** cubren los elementos mínimos que exige el enunciado.

**Prioridad:** **Debe** = sin esto el producto no se entrega · **Debería** = se implementa si el cronograma lo permite; si se recorta, se documenta.
**Verificación:** **P** = prueba (unitaria o de integración, con criterio numérico) · **D** = demostración (se observa el comportamiento) · **I** = inspección (revisión de código, archivos o del montaje) · **A** = análisis (cálculo o medición indirecta).

## 1. Alcance

Incluido: medición de temperatura y humedad relativa, registro histórico, interfaz web local, indicadores LED, bring-up, factory reset, instrumentación del sistema y carcasa prototipo.

Fuera de alcance: CO₂, presión, calidad del aire, actuadores (ventiladores, relés), conectividad a la nube, aplicación móvil, notificaciones (correo, mensajería) y alimentación por batería.

## 2. Actores y casos de uso

| Actor | Descripción |
|---|---|
| Usuario final | Persona no experta en embebidos que consulta el ambiente de un espacio (laboratorio, bodega, cuarto de servidores). |
| Administrador | Quien instala el equipo: hace el bring-up y ajusta los umbrales. Conoce la contraseña. |
| Soporte técnico | Revisa la página de diagnóstico cuando algo falla. |

| ID | Caso de uso | Actor | Resultado | Requerimientos |
|---|---|---|---|---|
| UC-01 | Configurar un equipo nuevo (bring-up) | Administrador | El equipo queda configurado y en funcionamiento normal, siguiendo solo el manual | RF-40 a RF-46 |
| UC-02 | Consultar la medición actual | Usuario final | Ve temperatura, humedad y estado, en la web o en los LED | RF-30, RF-31 |
| UC-03 | Consultar el historial | Usuario final | Ve la gráfica de las últimas horas y días | RF-21, RF-22, RF-30 |
| UC-04 | Enterarse de una alarma | Usuario final | El LED y la web indican que una variable está fuera de rango | RF-23, RF-24, RF-31 |
| UC-05 | Cambiar los umbrales | Administrador | Los nuevos umbrales se aplican sin repetir el bring-up | RF-32 |
| UC-06 | Diagnosticar una falla | Soporte técnico | Identifica si el problema es el sensor, la UART, el STM32 o Linux | RF-60 a RF-64 |
| UC-07 | Restablecer de fábrica | Administrador | El equipo vuelve al bring-up sin configuración ni historial | RF-50 a RF-55 |

## 3. Requerimientos funcionales

### Sensado y firmware (STM32)

| ID | Requerimiento | Prioridad | Verif. | Pruebas |
|---|---|---|---|---|
| RF-01 | El STM32 mide temperatura y humedad relativa con un sensor I²C (SHT31 o equivalente) una vez por segundo. **(curso)** | Debe | P | PU-02, PI-02 |
| RF-02 | El STM32 verifica el CRC de cada lectura del sensor y descarta las lecturas inválidas. | Debe | P | PU-01 |
| RF-03 | El STM32 detecta en ≤ 2 s que el sensor no responde, lo reporta (estado ≠ 0) y reintenta la comunicación sin reiniciarse. | Debe | P | PI-05 |
| RF-04 | Al reconectar el sensor, el STM32 vuelve a entregar lecturas válidas en ≤ 2 s, sin intervención del usuario. | Debe | P | PI-05 |
| RF-05 | El STM32 tiene un watchdog independiente (IWDG) que lo reinicia si el lazo principal se bloquea, y reporta la causa del último reset. | Debe | P | PI-06 |
| RF-06 | El STM32 sigue midiendo y transmitiendo aunque no reciba latido de Linux. | Debe | P | PI-12 |
| RF-07 | El STM32 reporta en `STA` su propia instrumentación: tiempo máximo de una iteración del lazo principal y pila libre mínima observada. | Debería | P | PI-16 |

### Comunicación SBC–microcontrolador

| ID | Requerimiento | Prioridad | Verif. | Pruebas |
|---|---|---|---|---|
| RF-10 | La Milk-V y el STM32 se comunican por una UART dedicada según [protocolo-uart.md](protocolo-uart.md). **(curso)** | Debe | P | PU-03, PI-03 |
| RF-11 | Ambos lados validan el checksum de cada trama y descartan las inválidas sin bloquearse. | Debe | P | PU-04, PU-05 |
| RF-12 | Linux detecta tramas perdidas a partir del consecutivo `seq`. | Debe | P | PU-06 |
| RF-13 | Linux envía un latido (`SYS`) cada segundo con el estado del sistema. | Debe | P | PI-04 |

### Servicio Linux, registro y alarmas

| ID | Requerimiento | Prioridad | Verif. | Pruebas |
|---|---|---|---|---|
| RF-20 | El servicio Linux (`envmond`) arranca automáticamente con el sistema y se reinicia solo si termina de forma inesperada. | Debe | P | PI-07 |
| RF-21 | El servicio guarda en almacenamiento persistente el promedio, el mínimo y el máximo de cada intervalo de registro (60 s por defecto, configurable entre 60 y 3600 s). | Debe | P | PU-09 |
| RF-22 | Los registros rotan a diario y se conserva una cantidad configurable de días (7 por defecto, de 1 a 30). | Debería | P | PU-11 |
| RF-23 | El servicio compara cada medición válida con los umbrales configurados y señala alarma en la web y en los LED. | Debe | P | PI-09 |
| RF-24 | La alarma se activa cuando la variable lleva 5 lecturas consecutivas fuera de rango y se desactiva cuando vuelve al rango con una histéresis de 0,5 °C o 2 % HR, para evitar que parpadee en el límite. | Debería | P | PU-13 |
| RF-25 | Si no hay una medición válida en los últimos 5 s, la web muestra "sin datos" en lugar del último valor. | Debe | P | PI-05 |

### Interfaz de usuario

| ID | Requerimiento | Prioridad | Verif. | Pruebas |
|---|---|---|---|---|
| RF-30 | La interfaz web muestra la medición actual, el historial (gráfica de al menos 24 h) y el estado del dispositivo. **(curso)** | Debe | P | PI-08 |
| RF-31 | Los LED del STM32 indican los estados definidos en el protocolo (§5), con las frecuencias y la prioridad allí definidas. **(curso)** | Debe | P | PI-13 |
| RF-32 | Después del bring-up, el nombre y los umbrales se pueden cambiar en una página de ajustes que pide la contraseña de administrador. Las demás páginas son de solo lectura y no la piden. | Debería | P | PU-15, PI-14 |

### Bring-up (curso)

| ID | Requerimiento | Prioridad | Verif. | Pruebas |
|---|---|---|---|---|
| RF-40 | Si no existe configuración, el equipo arranca en modo bring-up y lo indica con el LED verde parpadeando a 1 Hz. | Debe | P | PB-01 |
| RF-41 | En modo bring-up, la web presenta una página de configuración inicial con: nombre del equipo, contraseña de administrador, hora/NTP, intervalo de registro y umbrales de alarma. Cualquier otra página redirige a esta. | Debe | D | PB-01, PB-03 |
| RF-42 | La configuración se valida antes de guardarla: rangos de cada campo, mínimo < máximo y contraseña de al menos 8 caracteres. Un error se muestra junto al campo que lo causó. | Debe | P | PU-14, PB-02 |
| RF-43 | La configuración se guarda de forma atómica (archivo temporal + `fsync` + `rename`). Un corte de energía durante el guardado no deja una configuración corrupta. | Debe | P | PU-07, PB-04 |
| RF-44 | Al guardar la configuración, el equipo pasa a funcionamiento normal en ≤ 5 s, sin reiniciarse ni reinstalar nada. | Debe | P | PB-03 |
| RF-45 | El manual de usuario describe el bring-up paso a paso para un usuario no experto. | Debe | D | PB-05 |
| RF-46 | La dirección para entrar al bring-up es fija y conocida de antemano: aparece en el manual y en una etiqueta en la carcasa. El usuario no tiene que averiguar la IP del equipo. | Debe | P | PB-06 |

### Factory reset (curso)

| ID | Requerimiento | Prioridad | Verif. | Pruebas |
|---|---|---|---|---|
| RF-50 | El factory reset se activa **solo** con el botón físico conectado al STM32, mantenido presionado 10 s. No hay opción de reset en la web. | Debe | P / I | PF-02 |
| RF-51 | Mientras el botón está presionado, los LED parpadean a 5 Hz. Si se suelta antes de 10 s, no ocurre nada. | Debe | P | PU-10, PF-01 |
| RF-52 | El reset borra la configuración y los registros, y el equipo vuelve al modo bring-up (RF-40). | Debe | P | PF-02, PF-05 |
| RF-53 | El archivo de configuración se elimina como **último** paso. Un corte de energía a mitad del reset deja el equipo en un estado coherente: configurado o en bring-up, nunca intermedio. | Debe | P | PU-12, PF-03 |
| RF-54 | Si Linux no confirma el reset (`ACK`) tras 5 intentos, el STM32 lo indica con los LED. | Debe | P | PF-04 |
| RF-55 | Desde que se cumplen los 10 s del botón hasta que el equipo está en bring-up pasan ≤ 30 s. | Debería | P | PF-02 |

### Instrumentación soft (curso)

| ID | Requerimiento | Prioridad | Verif. | Pruebas |
|---|---|---|---|---|
| RF-60 | La página de diagnóstico muestra el uso de CPU, el uso de memoria y el espacio libre en el almacenamiento, y se actualiza cada ≤ 2 s. | Debe | P | PU-08, PI-10 |
| RF-61 | La página de diagnóstico muestra los bytes de red de entrada y salida por interfaz, y los puertos TCP/UDP en escucha con el proceso que los usa. | Debe | P | PU-08, PI-10 |
| RF-62 | La página de diagnóstico muestra el acceso a periféricos: bytes y tramas UART recibidas y enviadas, errores de checksum y de trama, tramas perdidas y tiempo desde la última trama. | Debe | P | PU-05, PI-10 |
| RF-63 | La página de diagnóstico muestra los datos del STM32: uptime, errores I²C, causa del último reset, versión de firmware y la instrumentación de RF-07. | Debe | P | PI-10, PI-16 |
| RF-64 | La página de diagnóstico muestra el consumo del propio `envmond`: memoria residente (RSS), CPU y descriptores abiertos. | Debería | P | PI-15 |

## 4. Requerimientos no funcionales

| ID | Requerimiento | Prioridad | Verif. | Pruebas |
|---|---|---|---|---|
| RNF-01 | El software de Linux se escribe en C y lee las métricas directamente de `/proc` y `/sys`, sin intérpretes (Python, Node) ni frameworks pesados. | Debe | I | PS-05 |
| RNF-02 | El firmware se escribe en C para el NUCLEO-L432KC. | Debe | I | PS-05 |
| RNF-03 | El servicio Linux no tiene fugas de memoria (verificado con Valgrind en el build de host). | Debe | P | PI-11 |
| RNF-04 | El sistema funciona sin intervención durante al menos 24 h continuas. | Debe | P | PS-01 |
| RNF-05 | El sistema se alimenta con una sola fuente USB de 5 V. *(Confirmar si una fuente alimenta ambas placas o si se usan dos.)* | Debe | A | PS-06 |
| RNF-06 | El calor de las placas no sesga la medición más de 0,5 °C (sensor separado o en zona ventilada). | Debe | P | PS-03 |
| RNF-07 | La temperatura difiere de un instrumento de referencia en menos de ±1 °C y la humedad en menos de ±5 % HR, en condiciones de laboratorio. *(Tolerancia por confirmar con el instrumento disponible.)* | Debe | P | PS-02 |
| RNF-08 | Para reducir el desgaste de la microSD, no se escribe en el almacenamiento persistente más de una vez por minuto en funcionamiento normal. | Debe | A | PS-04 |
| RNF-09 | La carcasa es un prototipo (PLA, madera, cartón u otro material) que deja accesibles el botón de reset, los LED, el puerto Ethernet y la alimentación, y protege el botón de pulsaciones accidentales. **(curso)** | Debe | I | PS-07 |
| RNF-10 | Desde que se conecta la alimentación hasta que la web responde y los LED muestran el estado pasan ≤ 90 s. | Debería | P | PS-08 |
| RNF-11 | En funcionamiento normal, `envmond` usa ≤ 8 MB de memoria residente y ≤ 5 % de CPU en promedio. | Debería | P | PS-09 |
| RNF-12 | La web funciona sin conexión a internet (sin CDN ni recursos externos) y se ve correctamente en el navegador de un celular. | Debe | P | PS-10 |
| RNF-13 | El firmware ocupa ≤ 50 % de la flash (256 KB) y ≤ 50 % de la RAM (64 KB) del STM32L432KC. | Debería | A | PS-11 |

## 5. Trazabilidad con los elementos mínimos del curso

| Elemento del enunciado | Requerimientos |
|---|---|
| Comunicación SBC–microcontrolador | RF-10 a RF-13 |
| Sensado de una variable física | RF-01 a RF-04 |
| Bring-up con manual de usuario | RF-40 a RF-46 |
| Factory reset | RF-50 a RF-55 |
| Interfaz: LED + seguimiento web | RF-30, RF-31 |
| Carcasa prototipo | RNF-09 |
| Instrumentación: memoria, periféricos, CPU | RF-07, RF-60, RF-62 a RF-64 |
| Instrumentación de red: datos de entrada/salida, puertos | RF-61 |
| Lenguajes compilados, pocas capas, bajo consumo de recursos | RNF-01, RNF-02, RNF-11, RNF-13 |

## 6. Decisiones abiertas

| Tema | Opciones | Propuesta | Se decide con |
|---|---|---|---|
| Servidor HTTP | Biblioteca embebible en C (mongoose / civetweb) o lighttpd + CGI en C | mongoose: un solo archivo `.c`, sin procesos adicionales; revisar su licencia (GPLv2 / comercial) | Mateo |
| Acceso en el bring-up (RF-46) | a) Red USB (RNDIS) de la Duo S con IP fija; b) Ethernet directa al PC con IP fija y servidor DHCP en el equipo; c) DHCP del router + mDNS | Probar a) primero, porque la imagen oficial ya trae la red USB con IP fija (192.168.42.1). Si la alimentación final no usa ese USB, pasar a b) | PI-01 |
| Hora | RTC de la Duo S (sin batería, se pierde al apagar), NTP o hora del navegador en el bring-up | NTP si hay red; si no, tomar la hora del navegador al guardar el formulario. Los registros sin hora válida se marcan | PI-01 |
| Hash de la contraseña | `crypt()` de la libc con SHA-512 (`$6$`) o una biblioteca | `crypt()`: no agrega dependencias; confirmar que la libc de la imagen lo soporta | Mateo |
| Aviso web → `envmond` | `SIGHUP` con PID en `/run/envmon/envmond.pid`, o socket Unix | Si la web corre dentro de `envmond` (mongoose), no hace falta aviso: es una llamada directa | Mateo + Andrew |
| Valgrind y SystemTap en la Duo S | Disponibles en la imagen, o solo en el host | Verificar en el host x86 con puerto serie virtual (`socat`); en la placa usar `/proc/<pid>/status`, `strace` y `perf` si están | Juan Fernando |
| Pines de la UART | UART1–UART3 del J3 de la Duo S (por pinmux) y USART1 del NUCLEO | No usar UART0 (pines 8 y 10 del J3): es la consola de Linux | PI-01 |
| Alimentación | Una fuente para las dos placas o dos fuentes | Medir el consumo (PS-06) antes de decidir | PS-06 |
