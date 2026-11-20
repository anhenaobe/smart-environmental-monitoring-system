# Roles, actividades y aportes

Se actualiza a lo largo del proyecto. Cada integrante registra sus aportes con enlaces a commits, PR o documentos.

## Roles

El equipo tiene **tres integrantes**: Andrew, Mateo y Juan Fernando. No se unió un cuarto integrante, así que sus tareas se repartieron entre los tres (2026-11-19).

| Integrante | Rol | Responsabilidades |
|---|---|---|
| Andrew | **Firmware, hardware e integración física** | Firmware del STM32 (sensor I²C, UART, validación, LED, botón y watchdog). Bring-up físico de la Milk-V (imagen, consola, red y UART), cableado, alimentación, montaje y pruebas experimentales. Implementa el extremo STM32 del protocolo; no desarrolla `envmond`. |
| Mateo | **Servicio Linux y datos** | Implementa `envmond` en C: UART Linux, parser, latido, alarmas, configuración, reset y recuperación, registros, API HTTP y supervisión del proceso; construye las páginas de bring-up y ajustes. Coordina la matriz de verificación y la declaración de IA. |
| Juan Fernando (Fercho) | **Interfaz, instrumentación y producto** | Implementa el dashboard de mediciones e historial y la página de diagnóstico usando la API de Mateo; métricas desde `/proc` y `/sys`; soporte de pruebas en host con `socat`, `make test` y Valgrind cuando proceda. Diseña la carcasa junto con Andrew y prepara manual, video y pruebas de aceptación. |

La frontera entre los roles está en [interfaces.md](interfaces.md): mientras esos contratos se respeten, cada uno puede avanzar sin esperar a los demás.

## Regla de verificación

Nadie verifica lo que implementó. Propuesta de verificación cruzada:

| Implementa | Verifica |
|---|---|
| Andrew (firmware y hardware) | Juan Fernando (instrumentos) o Mateo (integración funcional) |
| Mateo (`envmond`, configuración y bring-up web) | Andrew o Juan Fernando |
| Juan Fernando (dashboard, diagnóstico y carcasa) | Andrew o Mateo |
| Andrew + Mateo (integración UART y reset) | Juan Fernando |
| Mateo + Juan Fernando (API y visualización) | Andrew |

Las pruebas de desarrollo propias son válidas, pero la **verificación formal** la realiza alguien que no figure entre sus implementadores.

Participaciones en la matriz (una prueba compartida cuenta para cada implementador): Andrew 26 implementaciones y 11 verificaciones; Mateo 31 y 10; Juan Fernando 9 y 32. **Estos conteos no equivalen a horas de trabajo.**

## Registro de actividades

| Fecha | Integrante | Actividad | Evidencia |
|---|---|---|---|
| 2026-11-13 | Andrew | Definición del alcance, protocolo UART, requerimientos, matriz de verificación y estructura del repositorio | Commit `803936e` |
| 2026-11-13 | Andrew | Definición de roles e interfaces entre subsistemas | Commit `a8aea09` |
| 2026-11-13 | Mateo | Propuesta v0.2 de requerimientos, matriz de verificación, descripción y protocolo | PR #1 |
| 2026-11-19 | Mateo | Arquitectura, índice de documentación y propuesta inicial de reparto | PR #2, primer commit |
| 2026-11-19 | Andrew (asistencia de ChatGPT) | Ajuste de roles, verificación y arquitectura tras auditoría de PR #2 | PR #2, commit de seguimiento; revisión humana pendiente |
