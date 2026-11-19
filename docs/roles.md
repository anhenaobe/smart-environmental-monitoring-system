# Roles, actividades y aportes

Se actualiza a lo largo del proyecto. Cada integrante registra sus aportes con enlaces a commits, PR o documentos.

## Roles

El equipo tiene **tres integrantes**: Andrew, Mateo y Juan Fernando. No se unió un cuarto integrante, así que sus tareas se repartieron entre los tres (2026-11-19).

| Integrante | Rol | Responsabilidades |
|---|---|---|
| Andrew | **Firmware, adquisición e integración de hardware** | Firmware completo del STM32 (sensor I²C, validación, UART, LED, botón de reset, watchdog). Bring-up de la Milk-V (imagen, consola, Ethernet, UART). Servicio de adquisición en la Milk-V (`envmond`): ambos extremos del protocolo UART, latido `SYS`, evaluación de alarmas, ejecución del factory reset y publicación del estado actual. Montaje físico: cableado, alimentación, ubicación del sensor. |
| Mateo | **Datos e interfaz web** | Servidor web y páginas (inicio, historial, bring-up, ajustes). Escritura de la configuración (formulario del bring-up). Registro histórico: agregación por minuto y rotación. Coordinación de la matriz de verificación y de la declaración de uso de IA. |
| Juan Fernando (Fercho) | **Instrumentación, herramientas de verificación y producto** | Página de diagnóstico y lectura de métricas desde `/proc` y `/sys` (CPU, memoria, almacenamiento, red, puertos). Framework de pruebas unitarias, Makefile de host, Valgrind, SystemTap (o su alternativa) y el puerto serie virtual con `socat` para probar sin hardware. Carcasa, manual de usuario, video y pruebas de aceptación con usuarios externos. |

La frontera entre los roles está en [interfaces.md](interfaces.md): mientras esos contratos se respeten, cada uno puede avanzar sin esperar a los demás.

## Regla de verificación

Nadie verifica lo que implementó. Propuesta de verificación cruzada:

| Implementa | Verifica |
|---|---|
| Andrew (firmware y `envmond`) | Juan Fernando (pruebas con instrumentos: analizador lógico, ST-LINK, referencia, cortes de energía) y Mateo (pruebas funcionales) |
| Mateo (web, datos) | Andrew |
| Juan Fernando (instrumentación) | Mateo |
| Juan Fernando (carcasa, manual, video) | Andrew y Mateo |
| Andrew + Mateo (bring-up y factory reset de extremo a extremo) | Juan Fernando |

Carga aproximada según la matriz (implementa + verifica): Andrew 33 + 9, Mateo 16 + 14, Juan Fernando 7 + 30.

## Registro de actividades

| Fecha | Integrante | Actividad | Evidencia |
|---|---|---|---|
| 2026-11-13 | Andrew | Definición del alcance, protocolo UART, requerimientos, matriz de verificación y estructura del repositorio | Commit `803936e` |
| 2026-11-13 | Andrew | Definición de roles e interfaces entre subsistemas | Commit `a8aea09` |
| 2026-11-13 | Mateo | Propuesta v0.2 de requerimientos, matriz de verificación, descripción y protocolo | PR #1 |
| 2026-11-19 | Mateo | Documento de arquitectura, índice de documentación y reparto de roles para tres integrantes | Este commit |
