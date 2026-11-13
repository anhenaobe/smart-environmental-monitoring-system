# Servicio Linux (Milk-V Duo S)

Servicio en C que se ejecuta al arrancar el sistema.

## Funciones

- Recepción y validación de tramas UART ([docs/protocolo-uart.md](../docs/protocolo-uart.md)).
- Agregación por minuto y registro en CSV con rotación.
- Alarmas por umbral y envío del latido `SYS`.
- Configuración persistente con escritura atómica; bring-up y factory reset.
- Instrumentación: CPU, memoria, almacenamiento, red y puertos desde `/proc` y `/sys`; contadores de la UART.
- Servidor HTTP para la interfaz web (`../web/`).

## Estructura

```
src/     código del servicio
tests/   pruebas unitarias (CMocka o Unity), ejecutables en el host
```

## Compilación (prevista)

- `make` → binario para la Duo S con el toolchain RISC-V del SDK de Milk-V.
- `make host` → binario x86 para pruebas, Valgrind y puerto serie virtual con `socat`.
- `make test` → pruebas unitarias.
