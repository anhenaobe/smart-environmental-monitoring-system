# Documentación

## Orden de lectura (y de presentación)

| # | Documento | Pregunta que responde | Responsable |
|---|---|---|---|
| 1 | [Descripción del sistema](descripcion-sistema.md) | ¿Qué problema resuelve y para quién? | Andrew |
| 1a | [Criterios ambientales de conservación](criterios-conservacion.md) | ¿Qué números están sustentados por bibliografía y cuáles son decisiones de diseño? | Andrew (propuesta), revisión del equipo |
| 2 | [Requerimientos](requerimientos.md) | ¿Qué debe hacer, con qué criterio medible? | Mateo |
| 3 | [Arquitectura](arquitectura.md) | ¿Cómo está construido y por qué? | Mateo (redacción), Andrew (revisión técnica), Juan Fernando (UI/diagnóstico) |
| 4 | [Matriz de verificación](matriz-verificacion.md) | ¿Cómo se demuestra que cumple, quién lo implementa y quién lo prueba? | Mateo y Juan Fernando |

## Contratos entre partes

| Documento | Conecta | Cambios aprobados por |
|---|---|---|
| [Protocolo UART](protocolo-uart.md) | STM32 ↔ `envmond` | Andrew (STM32) + Mateo (Linux), cambios acordados |
| [Interfaces](interfaces.md) | `envmond` ↔ web, configuración y diagnóstico | Mateo (API) + Juan Fernando (web/diagnóstico) |

## Pruebas de desarrollo

- [Bring-up STM32 + Milk-V](bringup-v0.md): instalación, cableado, diagnóstico y ensayos físicos pendientes.
- [Registro de ejecuciones](../tests/registro.md): resultados offline y formato para evidencia de laboratorio.

## Producto y gestión

| Documento | Contenido | Responsable |
|---|---|---|
| [Manual de usuario](manual-usuario.md) | Bring-up, uso diario, LED y factory reset para un usuario no experto | Juan Fernando |
| [Roles](roles.md) | Roles, regla de verificación cruzada y registro de aportes | Todos |
| [Uso de IA](uso-ia.md) | Cuándo, cómo y para qué se usó IA, y cómo se verificó | Mateo |
| [Chats de IA](ia-chats/) | Transcripciones de los chats registrados | Quien usó la herramienta |

## Convenciones

- Cada documento lleva un número de versión al inicio.
- Los requerimientos (`RF-`, `RNF-`) y las pruebas (`PU-`, `PI-`, `PB-`, `PF-`, `PS-`) tienen IDs fijos: no se renumeran; si uno se elimina, se marca como retirado.
- Los cambios entran a `main` por pull request revisado por un integrante distinto del autor.
