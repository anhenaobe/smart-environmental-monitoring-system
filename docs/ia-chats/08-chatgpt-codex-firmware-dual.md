# Registro de asistencia IA #8 — Firmware de laboratorio

Fecha: 2026-11-26. Responsable: Andrew. Herramienta: ChatGPT/Codex; implementación delegada a dos agentes de código y revisión de integración por el asistente principal.

## Solicitud de Andrew (transcripción)

> Bien, BROnskiano, vamos a trabajar en el firmware, podriamos hacerlo de modo que a mi me toque comprobar el funcionamiento? luego me encargare de estudiarlo. la idea es que el firmware trabaje tanto para la Milk como para la stm, suponiendo que encuentro una stm32l432kc o stm32l412kb.
> Podemos delegarle la tarea a claude code o codex, lo que prefieras. Recuerda que la version de la Milk puede ser consultada en el anterior proyecto, el de ucupviewer

## Resumen del trabajo asistido

- Recuperar bring-up v0.1 sobre documentación actual, sin alterar trabajo de otros subsistemas. Una rama de ensayo contiene los cambios.
- Agente STM32: contrastar manual UM1956 y hojas de datos; crear un firmware común para las dos Nucleo, targets normal, sintético y de watchdog, máquina de botón y LED, recuperación de fallas I²C, métricas STM32 y configuración de compilación.
- Agente Milk-V: consultar uCuPViewer (registro de la sesión real de 5/11/2026), documentación oficial del pinout y SDK; implementar utilidad C temporal de UART y pruebas PTY, sin instalar servicios ni realizar factory reset.
- Asistente principal: refuerzo de parser y pruebas de CRC/conversión, coordinación y revisión de cambios, guía de ensayo, compilación y registro de resultados. Umbrales ambientales siguen responsabilidad de envmond: no se duplican en STM32.
- No se afirmó que el hardware estuviera probado. Las variantes ficticias y el ACK simulado se usan exclusivamente en banco; jamás representan medidas ambientales ni el marcador durable de reset.

Ver [guía](../bringup-v0.md), [registro de pruebas](../../tests/registro.md), y los cambios de la rama para evidencia técnica. Este archivo reproduce la petición y resume los aportes; no es una exportación completa de la conversación.
