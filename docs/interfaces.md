# Interfaces entre subsistemas

Versión: 0.1 (borrador)

Estos contratos separan el trabajo de cada rol. Para cambiar uno se necesita el acuerdo de los dos roles que conecta.

```
 STM32 ──UART──► envmond ──► /run/envmon/estado.json ──► web / diagnóstico
                    ▲
                    └── /etc/envmon/config ◄── formulario de bring-up (web)
```

## 1. UART STM32 ↔ `envmond`

Definido en [protocolo-uart.md](protocolo-uart.md). Como Andrew implementa los dos extremos, este contrato no tiene dueños separados, pero sigue siendo la referencia para las pruebas.

## 2. Estado actual: `envmond` → web e instrumentación

`envmond` reescribe `/run/envmon/estado.json` una vez por segundo. `/run` está en RAM (tmpfs), así que no desgasta la microSD. La escritura es atómica: archivo temporal + `rename`.

```json
{
  "ts": 1763045400,
  "estado_sistema": 1,
  "medicion": { "valida": true, "temp_c": 24.50, "hr": 53.10, "estado_sensor": 0, "seq": 42 },
  "uart": { "tramas_ok": 1200, "err_checksum": 0, "err_trama": 0, "desconocidas": 0, "perdidas": 0, "seg_desde_ultima": 0 },
  "stm32": { "uptime_s": 3600, "err_i2c": 0, "causa_reset": 0, "fw": "0.1.0" }
}
```

- `estado_sistema`: mismos valores que `SYS` (0 bring-up, 1 normal, 2 alarma, 3 reset en curso).
- Cuando `medicion.valida = false`, `temp_c` y `hr` valen `null`.
- El registro histórico (Integrante 2) lee este archivo cada segundo y agrega por minuto.

## 3. Configuración: web → `envmond`

Archivo `/etc/envmon/config`, en formato `clave=valor`, una entrada por línea. Lo escribe **solo** el formulario de bring-up, de forma atómica.

```
nombre=Laboratorio 3
admin_hash=<hash de la contraseña>
ntp=pool.ntp.org
intervalo_registro_s=60
temp_min=15.0
temp_max=30.0
hr_min=30.0
hr_max=70.0
retencion_dias=7
```

- Si el archivo no existe, el equipo está en modo bring-up.
- Después de escribirlo, la web avisa a `envmond` con `SIGHUP` y este recarga la configuración y pasa a normal.

## 4. Factory reset (`envmond`)

Al recibir `$BTN,RST`:

1. Responder `$ACK,RST` y enviar `$SYS,3`.
2. Borrar los registros (`/var/lib/envmon/`).
3. Borrar `/etc/envmon/config` (**último paso**, RF-53).
4. Pasar a modo bring-up (`SYS,0`) sin reiniciar la placa.

El servicio web detecta el modo bring-up porque falta el archivo de configuración (o por `estado_sistema = 0`) y muestra el formulario.

## 5. Pendientes

- Hash de la contraseña: algoritmo y biblioteca (en C, sin dependencias pesadas).
- Mecanismo exacto de aviso web → `envmond` (`SIGHUP` con PID en `/run/envmon/envmond.pid`, o un socket).
