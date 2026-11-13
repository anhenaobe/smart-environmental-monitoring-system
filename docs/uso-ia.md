# Declaración de uso de inteligencia artificial

Este documento registra cada uso de herramientas de IA en el proyecto: cuándo se usó, con qué herramienta, para qué y **cómo se verificó el resultado**. Los chats completos se guardan en `docs/ia-chats/`.

## Registro

| # | Fecha | Herramienta | Propósito | Resultado | Verificación | Responsable |
|---|---|---|---|---|---|---|
| 1 | 2026-11 | Asistente conversacional (ChatGPT) | Elegir la temática del proyecto y proponer el hardware y la arquitectura | Propuesta inicial del monitor ambiental | Datos de la Milk-V Duo S (Ethernet, UART en J3, 3,3 V, 5 V/1 A) contrastados con la documentación oficial de Milk-V: _pendiente de registrar_ | Andrew |
| 2 | 2026-11-13 | Claude (Anthropic) | Revisar la propuesta frente a los requisitos del curso; definir el bring-up, el factory reset, el watchdog, el protocolo UART; redactar los requerimientos, la matriz de verificación y la estructura del repositorio | Archivos de `docs/` y estructura del repositorio | Revisión del equipo frente al enunciado; el protocolo y los requerimientos se validan con las pruebas de la matriz. _Pendiente: revisión por los demás integrantes_ | Andrew |
| 3 | 2026-11-13 | Claude (Anthropic) | Revisar el repositorio frente al enunciado y a los proyectos anteriores; preparar la v0.2 de requerimientos, matriz, descripción y protocolo | Prioridad, método de verificación y trazabilidad en los requerimientos; 14 requerimientos nuevos; 22 pruebas nuevas; inconsistencias corregidas entre manual, interfaces y requerimientos | Los checksums de todas las tramas de ejemplo y el vector CRC del SHT31 (`0xBEEF` → `0x92`) se recalcularon con un script; un script verificó que cada requerimiento tenga al menos una prueba y que las referencias sean recíprocas; los datos de la Duo S (J3 a 3,3 V, UART0 en pines 8/10, pin 9 según revisión) se contrastaron con la documentación de Milk-V. _Pendiente: revisión por Andrew y Juan Fernando_ | Mateo |

## Criterios de verificación

- **Datos técnicos** (hojas de datos, pines, especificaciones): siempre se contrastan con la fuente oficial; se anota la fuente.
- **Código generado:** no se integra sin compilar, pasar las pruebas unitarias de la matriz y ser revisado por un integrante distinto del que lo pidió.
- **Documentación:** la revisa al menos un integrante que no la generó.
