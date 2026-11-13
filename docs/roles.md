# Roles, actividades y aportes

Se actualiza a lo largo del proyecto. Cada integrante registra sus aportes con enlaces a commits, PR o documentos.

## Roles

Integrantes confirmados: Andrew, Mateo y Juan Fernando. El rol del Integrante 4 queda abierto mientras se define si se une una cuarta persona; si no, sus tareas se reparten entre los tres. Mateo toma el rol de datos e interfaz web y Juan Fernando el de instrumentación y herramientas de verificación, además de la carcasa, el manual y el video (acordado el 2026-11-13).

| Integrante | Rol | Responsabilidades |
|---|---|---|
| Andrew | **Firmware, adquisición e integración de hardware** | Firmware completo del STM32 (sensor I²C, validación, UART, LED, botón de reset, watchdog). Bring-up de la Milk-V (imagen, consola, Ethernet, UART). Servicio de adquisición en la Milk-V (`envmond`): ambos extremos del protocolo UART, latido `SYS`, evaluación de alarmas, ejecución del factory reset y publicación del estado actual. Montaje físico: cableado, alimentación, ubicación del sensor. |
| Mateo | **Datos e interfaz web** | Servidor web y páginas (inicio, historial, bring-up, ajustes). Escritura de la configuración (formulario del bring-up). Registro histórico: agregación por minuto y rotación. |
| Juan Fernando (Fercho) | **Instrumentación, herramientas de verificación y producto** | Página de diagnóstico y lectura de métricas desde `/proc` y `/sys` (CPU, memoria, almacenamiento, red, puertos). Framework de pruebas unitarias, Makefile de host, Valgrind, SystemTap (o su alternativa) y el puerto serie virtual con `socat` para probar sin hardware. Carcasa, manual de usuario y video. |
| _Integrante 4_ | **Documentación y validación** | Coordinación de la matriz de verificación. Declaración de uso de IA. Pruebas de aceptación con usuarios externos. |

La frontera entre los roles está en [interfaces.md](interfaces.md): mientras esos contratos se respeten, cada uno puede avanzar sin esperar a los demás.

## Regla de verificación

Nadie verifica lo que implementó. Propuesta de verificación cruzada:

| Implementa | Verifica |
|---|---|
| Andrew (firmware y `envmond`) | Juan Fernando (herramientas) e Integrante 4 (pruebas funcionales) |
| Mateo (web, datos) | Andrew |
| Juan Fernando (instrumentación) | Mateo |
| Juan Fernando (carcasa, manual, video) | Andrew y Mateo |

## Registro de actividades

| Fecha | Integrante | Actividad | Evidencia |
|---|---|---|---|
| 2026-11-13 | Andrew | Definición del alcance, protocolo UART, requerimientos, matriz de verificación y estructura del repositorio | Commit `803936e` |
| 2026-11-13 | Andrew | Definición de roles e interfaces entre subsistemas | Commit `a8aea09` |
| 2026-11-13 | Mateo | Propuesta v0.2 de requerimientos, matriz de verificación, descripción y protocolo (pendiente de revisión por Andrew) | Este commit |
