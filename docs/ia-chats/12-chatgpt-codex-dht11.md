# Registro de asistencia IA #12 — Sustitución de sensor por DHT11

Fecha: 2026-11-26. Responsable: Andrew. Herramienta: ChatGPT/Codex con agentes de código.

## Solicitud de Andrew

> Bro tenemos un problema, resulta que nuestro sensor no es el que teníamos planeado, podrias corregir el firmware con codex directamente sobre github y actualizar rl repo para que el sensor de temp y humedad sea el DTH1?

Se confirmó que el componente disponible es el DHT11 (sensor azul), no el SHT31.

## Trabajo asistido

- Agente STM32: contrastar manual ST UM1956 y documentación Aosong, migrar el driver I²C a GPIO open-drain DATA en PB5/D11, temporizar con DWT a 16 MHz, decodificar y verificar el checksum aditivo, mantener watchdog, LED, botón y tramas; preparar pruebas host y objetivos de compilación para las dos placas.
- Agente de revisión Linux: contrastar la hoja de datos y el parser de Mateo, revisar los impactos de cadencia y precisión. No editar `linux/`.
- Asistente principal: revisar cambios y pruebas, sincronizar con main actual, adaptar documentación de hardware, requisitos, protocolo, criterios ambientales y matriz. Proponer 3 s entre lecturas reales en lugar de 1 s para DHT11 V1.3; mantener 1 s en modo sintético y SYS; no alterar el formato UART ni los umbrales del gabinete. Registrar la menor exactitud nominal y la necesidad de ensayar la variante disponible.
- Decisiones pendientes de revisión cruzada: tiempos de detección/recuperación, objetivo provisional RNF-07, histéresis, modo de representar las fallas de sensor en el diagnóstico y compatibilidad del módulo físico con 3,3 V/3 s. Coordinar la redacción final con el PR de protocolo v0.3 de Mateo.
- Las pruebas de software no demuestran funcionamiento físico. La matriz sigue pendiente hasta revisión y verificación de compañeros. Ver [registro de pruebas](../../tests/registro.md), [cableado](../../firmware/README.md) y [fuentes](../criterios-conservacion.md). No integrar directamente a main.

Este registro resume el trabajo asistido; no es una exportación completa del chat.
