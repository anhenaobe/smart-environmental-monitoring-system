# Manual de usuario

Versión: 0.1 (borrador; se completa cuando la interfaz web esté implementada)

## 1. Contenido de la caja

- Monitor ambiental
- Cable USB y fuente de 5 V
- Cable Ethernet

## 2. Indicadores

| LED verde | LED rojo | Qué significa | Qué hacer |
|---|---|---|---|
| Fijo | Apagado | Todo funciona | Nada |
| Parpadea despacio | Apagado | El equipo no está configurado | Seguir la sección 3 |
| Fijo | Parpadea despacio | Temperatura o humedad fuera de los límites configurados | Revisar el ambiente |
| Apagado | Fijo | Falla del sensor | Revisar que el sensor esté conectado; si sigue, contactar soporte |
| Apagado | Doble destello | El equipo está iniciando o el sistema interno no responde | Esperar 2 minutos; si sigue, desconectar y volver a conectar la alimentación |
| Parpadeo rápido | Parpadeo rápido | Se está restableciendo de fábrica | Esperar |

## 3. Configuración inicial (bring-up)

1. Conecte el cable Ethernet al equipo y a su router o computador.
2. Conecte la alimentación USB. Espere a que el LED verde parpadee despacio (aprox. 1 minuto).
3. En un navegador, abra la dirección `http://<por definir>`.
4. Complete el formulario:
   - **Nombre del equipo:** por ejemplo, "Laboratorio 3".
   - **Contraseña de administrador:** la necesitará para cambiar la configuración más adelante.
   - **Hora / servidor NTP.**
   - **Intervalo de registro.**
   - **Umbrales de alarma:** temperatura y humedad mínimas y máximas.
5. Pulse **Guardar**. El LED verde queda fijo: el equipo está listo.

## 4. Uso diario

- **Inicio:** temperatura y humedad actuales.
- **Historial:** gráfica de las últimas horas y días.
- **Diagnóstico:** estado interno del equipo (para soporte técnico).

## 5. Restablecer de fábrica

Borra **toda** la configuración y el historial.

1. Mantenga presionado el botón de reset durante **10 segundos**. Los dos LED parpadean rápido.
2. Suelte el botón. El equipo se reinicia y el LED verde vuelve a parpadear despacio.
3. Configure el equipo de nuevo (sección 3).

Si suelta el botón antes de 10 segundos, no se borra nada.

## 6. Ubicación recomendada

- Lejos de fuentes de calor directo (sol, computadores, calentadores).
- No obstruya las ranuras de ventilación de la carcasa.
- No exponga el equipo al agua.
