# Roles, actividades y aportes

Se actualiza a lo largo del proyecto. Cada integrante registra sus aportes con enlaces a commits, PR o documentos.

## Roles

| Integrante | Rol | Responsabilidades |
|---|---|---|
| Andrew | **Firmware, adquisición e integración de hardware** | Firmware completo del STM32 (sensor I²C, validación, UART, LED, botón de reset, watchdog). Bring-up de la Milk-V (imagen, consola, Ethernet, UART). Servicio de adquisición en la Milk-V (`envmond`): ambos extremos del protocolo UART, latido `SYS`, evaluación de alarmas, ejecución del factory reset y publicación del estado actual. Montaje físico: cableado, alimentación, ubicación del sensor. |
| _Integrante 2_ | **Datos e interfaz web** | Servidor web y páginas (inicio, historial, bring-up). Escritura de la configuración (formulario del bring-up). Registro histórico: agregación por minuto y rotación. |
| _Integrante 3_ | **Instrumentación y herramientas de verificación** | Página de diagnóstico y lectura de métricas desde `/proc` y `/sys` (CPU, memoria, almacenamiento, red, puertos). Framework de pruebas unitarias, Makefile de host, Valgrind, SystemTap (o su alternativa) y el puerto serie virtual con `socat` para probar sin hardware. |
| _Integrante 4_ | **Producto, documentación y validación** | Carcasa. Manual de usuario. Coordinación de la matriz de verificación. Declaración de uso de IA. Video. Pruebas de aceptación con usuarios externos. |

La frontera entre los roles está en [interfaces.md](interfaces.md): mientras esos contratos se respeten, cada uno puede avanzar sin esperar a los demás.

## Regla de verificación

Nadie verifica lo que implementó. Propuesta de verificación cruzada:

| Implementa | Verifica |
|---|---|
| Andrew (firmware y `envmond`) | Integrante 3 (herramientas) e Integrante 4 (pruebas funcionales) |
| Integrante 2 (web, datos) | Andrew |
| Integrante 3 (instrumentación) | Integrante 2 |
| Integrante 4 (carcasa, manual) | Andrew e Integrante 2 |

## Registro de actividades

| Fecha | Integrante | Actividad | Evidencia |
|---|---|---|---|
| 2026-11-13 | Andrew | Definición del alcance, protocolo UART, requerimientos, matriz de verificación y estructura del repositorio | Commit `803936e` |
| 2026-11-13 | Andrew | Definición de roles e interfaces entre subsistemas | Este commit |
