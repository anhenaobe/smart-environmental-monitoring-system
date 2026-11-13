# Análisis de requerimientos

Versión: 0.1 (borrador)

Cada requerimiento tiene un identificador único que se usa en la [matriz de verificación](matriz-verificacion.md). Los marcados como **(curso)** cubren los elementos mínimos que exige el enunciado.

## 1. Alcance

Incluido: medición de temperatura y humedad relativa, registro histórico, interfaz web local, indicadores LED, bring-up, factory reset, instrumentación del sistema y carcasa prototipo.

Fuera de alcance: CO₂, presión, calidad del aire, actuadores (ventiladores, relés), conectividad a la nube, aplicación móvil y alimentación por batería.

## 2. Requerimientos funcionales

### Sensado y firmware (STM32)

| ID | Requerimiento |
|---|---|
| RF-01 | El STM32 mide temperatura y humedad relativa con un sensor I²C (SHT31 o equivalente) una vez por segundo. **(curso)** |
| RF-02 | El STM32 verifica el CRC de cada lectura del sensor y descarta las lecturas inválidas. |
| RF-03 | El STM32 detecta que el sensor no responde, lo reporta (estado ≠ 0) y reintenta la comunicación sin necesidad de reiniciarse. |
| RF-04 | Al reconectar el sensor, el STM32 vuelve a entregar lecturas válidas sin intervención del usuario. |
| RF-05 | El STM32 tiene un watchdog independiente (IWDG) que lo reinicia si el lazo principal se bloquea, y reporta la causa del último reset. |
| RF-06 | El STM32 sigue midiendo y transmitiendo aunque no reciba latido de Linux. |

### Comunicación SBC–microcontrolador

| ID | Requerimiento |
|---|---|
| RF-10 | La Milk-V y el STM32 se comunican por UART según [protocolo-uart.md](protocolo-uart.md). **(curso)** |
| RF-11 | Ambos lados validan el checksum de cada trama y descartan las inválidas sin bloquearse. |
| RF-12 | Linux detecta tramas perdidas a partir del consecutivo `seq`. |
| RF-13 | Linux envía un latido (`SYS`) cada segundo con el estado del sistema. |

### Servicio Linux, registro y alarmas

| ID | Requerimiento |
|---|---|
| RF-20 | El servicio Linux arranca automáticamente con el sistema, sin intervención del usuario. |
| RF-21 | El servicio guarda en almacenamiento persistente el promedio, el mínimo y el máximo de cada minuto. |
| RF-22 | Los registros rotan y se conserva una cantidad configurable de días (7 por defecto). |
| RF-23 | El servicio compara cada medición con los umbrales configurados y señala alarma (web + LED). |

### Interfaz de usuario

| ID | Requerimiento |
|---|---|
| RF-30 | La interfaz web muestra la medición actual, el historial y el estado del dispositivo. **(curso)** |
| RF-31 | Los LED del STM32 indican los estados definidos en el protocolo (§5). **(curso)** |
| RF-32 | El acceso a la configuración por web requiere la contraseña de administrador definida en el bring-up. |

### Bring-up (curso)

| ID | Requerimiento |
|---|---|
| RF-40 | Si no existe configuración, el equipo arranca en modo bring-up y lo indica con el LED verde parpadeando a 1 Hz. |
| RF-41 | En modo bring-up, la interfaz web presenta una página de configuración inicial con: nombre del equipo, contraseña de administrador, hora/NTP, intervalo de registro y umbrales de alarma. |
| RF-42 | La página de configuración valida los datos antes de guardarlos (rangos, contraseña no vacía). |
| RF-43 | La configuración se guarda de forma atómica (escritura en un archivo temporal + `rename`). Un corte de energía durante el guardado no deja una configuración corrupta. |
| RF-44 | Al guardar la configuración, el equipo pasa a funcionamiento normal sin reinstalar nada. |
| RF-45 | El manual de usuario describe el bring-up paso a paso para un usuario no experto. |

### Factory reset (curso)

| ID | Requerimiento |
|---|---|
| RF-50 | El factory reset se activa **solo** con el botón físico conectado al STM32, mantenido presionado 10 s. No hay opción de reset en la web. |
| RF-51 | Mientras el botón está presionado, los LED parpadean a 5 Hz. Si se suelta antes de 10 s, no ocurre nada. |
| RF-52 | El reset borra la configuración y los registros, y el equipo vuelve al modo bring-up (RF-40). |
| RF-53 | El archivo de configuración se elimina como **último** paso. Un corte de energía a mitad del reset deja el equipo en un estado coherente: o configurado, o en bring-up. |
| RF-54 | Si Linux no confirma el reset (`ACK`), el STM32 lo indica con los LED. |

### Instrumentación soft (curso)

| ID | Requerimiento |
|---|---|
| RF-60 | La interfaz web tiene una página de diagnóstico con: uso de CPU, uso de memoria y espacio libre en el almacenamiento. |
| RF-61 | La página de diagnóstico muestra los bytes de red de entrada y salida por interfaz, y los puertos en escucha. |
| RF-62 | La página de diagnóstico muestra el acceso a periféricos: tramas UART recibidas, errores de checksum, tramas perdidas y tiempo desde la última trama. |
| RF-63 | La página de diagnóstico muestra los datos del STM32: uptime, errores I²C, causa del último reset y versión de firmware. |

## 3. Requerimientos no funcionales

| ID | Requerimiento |
|---|---|
| RNF-01 | El software de Linux se escribe en un lenguaje compilado (C propuesto) y lee las métricas directamente de `/proc` y `/sys`, sin intérpretes ni frameworks pesados. |
| RNF-02 | El firmware se escribe en C para el NUCLEO-L432KC. |
| RNF-03 | El servicio Linux no tiene fugas de memoria (verificado con Valgrind en el build de host). |
| RNF-04 | El sistema funciona sin intervención durante al menos 24 h continuas. |
| RNF-05 | El sistema se alimenta con USB de 5 V. |
| RNF-06 | El sensor se ubica separado de las placas o en una zona ventilada de la carcasa, para que el calor de las placas no sesgue la medición. |
| RNF-07 | La medición de temperatura difiere de un instrumento de referencia en menos de ±1 °C en condiciones de laboratorio. *(Tolerancia por confirmar con el instrumento disponible.)* |
| RNF-08 | Para reducir el desgaste de la microSD, no se escribe en el almacenamiento persistente más de una vez por minuto. |
| RNF-09 | La carcasa es un prototipo (PLA, madera, cartón u otro material) que deja accesibles el botón de reset, los LED, el puerto Ethernet y la alimentación. **(curso)** |

## 4. Decisiones abiertas

- Servidor HTTP: biblioteca embebible en C (mongoose / civetweb) o lighttpd + CGI en C.
- Red en el bring-up: DHCP con IP fija de respaldo, o solo IP fija. Se define después de probar la Ethernet de la Duo S.
- Hora: confirmar si la Duo S conserva la hora sin alimentación. Si no, NTP o hora fijada en el bring-up.
- Disponibilidad de Valgrind y SystemTap en la imagen de la Duo S. Si no están, se verifica en el build de host (x86) con un puerto serie virtual (`socat`).
- Pines exactos de la UART en el conector J3 de la Duo S y en el NUCLEO.
