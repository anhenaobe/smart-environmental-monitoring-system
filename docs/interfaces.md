# Interfaces entre subsistemas

Versión: 0.3 (borrador)

Estos contratos separan el trabajo de cada rol. Para cambiar uno se necesita el acuerdo de los dos roles que conecta.

```
 STM32 ──UART──► envmond (estado en RAM) ──HTTP/JSON──► web / diagnóstico
                    ▲
                    └── /etc/envmon/config ◄── formulario de bring-up (web)
```

## 1. UART STM32 ↔ `envmond`

Definido en [protocolo-uart.md](protocolo-uart.md). **Andrew implementa el extremo STM32 y Mateo el extremo Linux**. Ambos acuerdan las modificaciones al contrato antes de cambiar código; Juan Fernando utiliza los vectores para verificar la integración.

## 2. Estado actual: `envmond` → web e instrumentación

`envmond` conserva el último estado en RAM y lo publica con `GET /api/estado`. No se requiere archivo JSON intermedio. Mateo mantiene el formato y los códigos de error de esta API; Juan Fernando construye las vistas que la consultan. Ejemplo de respuesta:

```json
{
  "ts": 1763045400,
  "estado_sistema": 1,
  "medicion": { "valida": true, "temp_c": 24.50, "hr": 53.10, "estado_sensor": 0, "seq": 42 },
  "uart": { "tramas_ok": 1200, "err_checksum": 0, "err_trama": 0, "desconocidas": 0, "perdidas": 0, "seg_desde_ultima": 0 },
  "stm32": { "uptime_s": 3600, "err_i2c": 0, "causa_reset": 0, "fw": "0.1.0", "lazo_max_us": 850, "pila_libre_b": 2048 }
}
```

- `estado_sistema`: mismos valores que `SYS` (0 bring-up, 1 normal, 2 alarma, 3 reset en curso).
- `stm32.err_i2c` conserva el nombre legado del contrato y cuenta fallas de comunicación del sensor DHT11 (estados 1 y 2); mostrar «Errores del sensor» en diagnóstico.
- La frecuencia normal de ENV es 3 s; el resumen histórico se agrega por tiempo, no por conteo fijo de tramas. SYS conserva 1 Hz.
- Cuando `medicion.valida = false`, `temp_c` y `hr` valen `null`.
- El registro histórico del propio `envmond` (Mateo) consume las muestras validadas en memoria y las agrega por minuto, sin leer un archivo JSON.

## 3. Configuración: web → `envmond`

Archivo `/etc/envmon/config`, en formato `clave=valor`, una entrada por línea. **Solo `envmond`** lo escribe después de validar los datos enviados por las páginas de bring-up y ajustes (RF-32, con contraseña). Debe usar archivo temporal, `fsync`, `rename` y sincronización del directorio cuando el sistema de archivos lo soporte.

```
nombre=Gabinete documental A
admin_hash=<hash de la contraseña>
ntp=pool.ntp.org
intervalo_registro_s=60
temp_min=18.0
temp_max=22.0
hr_min=35.0
hr_max=50.0
retencion_dias=7
```

- Los umbrales mostrados son un **perfil sugerido editable** para el caso documental, no una condición universal. No cambia el formato ni los endpoints; ver [criterios-conservacion.md](criterios-conservacion.md).
- Si el archivo no existe, el equipo está en modo bring-up.
- Con el HTTP integrado en `envmond`, el manejador aplica la configuración tras guardarla; no necesita `SIGHUP` ni un segundo proceso.

## 4. Factory reset (`envmond`)

Al recibir `$BTN,RST`:

1. Crear y sincronizar `/etc/envmon/reset.pending`. Solo después responder `$ACK,RST` y enviar `$SYS,3`.
2. Borrar registros (`/var/lib/envmon/`) de manera idempotente.
3. Borrar `/etc/envmon/config` y sincronizar el directorio.
4. Retirar el marcador al terminar y pasar a bring-up (`SYS,0`), sin reiniciar la placa.

Al arrancar con `reset.pending`, `envmond` reanuda los pasos 2 a 4 **antes de servir la web**. Sin marcador durable, el reset todavía no se confirma y puede conservarse la configuración previa. Esta propiedad se debe probar sobre la microSD real; no implica protección contra corrupción de la tarjeta.

El servicio web detecta el modo bring-up porque falta el archivo de configuración (o por `estado_sistema = 0`) y muestra el formulario.

## 5. Pendientes

Ver [requerimientos.md §6](requerimientos.md#6-decisiones-abiertas): aviso web → `envmond` (innecesario si el servidor HTTP corre dentro de `envmond`).

**Hash de la contraseña (decidido, 2026-11-27):** `admin_hash` guarda el resultado de `crypt()` con SHA-512: `$6$`, una sal de 16 caracteres del alfabeto `./0-9A-Za-z` obtenida con `getrandom` y el resumen de 86 caracteres. El archivo nunca contiene la contraseña en claro.
