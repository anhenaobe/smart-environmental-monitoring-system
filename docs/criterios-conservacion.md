# Fundamentación para conservación documental

Versión 0.1 · 2026-11-20 · propuesta pendiente de revisión del equipo.

## 1. Problema y alcance

Las inspecciones ocasionales de temperatura y humedad de un gabinete documental dejan sin documentar fluctuaciones y extremos que ocurren fuera de horario. Un programa de monitoreo continuo permite caracterizar las condiciones que experimentan las colecciones y aportar evidencia para decidir mejoras de almacenamiento. El **NEDCC 2.2** recomienda registro continuo frente a mediciones puntuales, ubicación representativa de sensores y revisión periódica de su calibración.

**Caso de aplicación:** un armario, vitrina o compartimento pequeño que guarda cartas, carpetas o libros de papel corriente; **un solo sensor SHT31** observa el aire de ese punto. El dispositivo no mide el contenido de humedad del papel, no detecta directamente moho ni daño, no regula la temperatura ni la humedad y **no representa todos los puntos de una bodega o edificio**.

## 2. Cifras documentadas por fuentes

| Fuente / contexto | Temperatura | Humedad relativa | Interpretación correcta |
|---|---|---|---|
| NEDCC 2.1: papel, libros y textiles en espacio mixto de almacenamiento y trabajo | **68 °F ± 4 °F** = 64–72 °F ≈ **17,8–22,2 °C** | **30–60 % HR** | Guía para el contexto indicado. NEDCC plantea temperaturas menores para almacenamiento exclusivo |
| Library of Congress: cartas y documentos en papel | Ambiente fresco, a temperatura de sala o inferior, **sin un valor fijo en esa página** | Aproximadamente **35–50 % HR** | Guía de almacenamiento de documentos, no especificación del sensor |
| Canadian Conservation Institute: conservación general de obras de papel | **21 °C máximo** en el contexto citado | Preferentemente **<50 % HR**; **>60 % HR** acelera deterioro químico y biológico | Orientación más estricta que el intervalo general NEDCC |
| NEDCC 2.2: programa de monitoreo | **Sin consigna universal** | **Sin consigna universal** | Sustenta registro, análisis, colocación, mantenimiento y calibración |

**No se deben fusionar estas fuentes en una supuesta norma única.** Las necesidades difieren para películas, fotografías, pergaminos y colecciones particulares. El NEDCC 2.2 indica explícitamente que no existe un único punto ideal para todas las colecciones.

## 3. Perfil inicial sugerido del prototipo

El formulario de configuración ofrecerá como **valores iniciales editables**:

| Campo | Sugerido | Razón |
|---|---:|---|
| Temperatura mínima | **18,0 °C** | Redondeo del límite 17,8 °C de NEDCC 2.1 para espacio mixto |
| Temperatura máxima | **22,0 °C** | Redondeo del límite 22,2 °C del NEDCC; el criterio CCI para su contexto es más estricto: 21 °C |
| HR mínima | **35 %** | Dentro de NEDCC 30–60 %, coincide con orientación de la Library of Congress |
| HR máxima | **50 %** | Coincide con el extremo superior de Library of Congress y es más estricta que NEDCC 60 % |

**Son ajustes operativos propuestos, no límites universales de seguridad.** Al superarlos, la alarma significa **desviación respecto del perfil seleccionado**, no presencia demostrada de hongos o daño. La persona responsable debe configurar el perfil según su colección; si existe un especialista en conservación, prevalece su evaluación. El instrumento tampoco puede garantizar condiciones reales dentro de sus márgenes de error.

## 4. Qué números pertenecen al diseño, no a la bibliografía

| Parámetro | Valor existente | Motivo del equipo |
|---|---:|---|
| SHT31 y trama `ENV` (RF-01) | **1 muestra/s** | Seguimiento frecuente y detección de fallas; NEDCC 2.2 **no exige** 1 Hz |
| Resumen de historial (RF-21) | **60 s**, configurable 60–3600 s | Reducir escritura en microSD: 86 400 muestras/día a 1 Hz frente a 1 440 intervalos/día (factor **60**) |
| Gráfica (RF-30) | **≥24 h** | Criterio de presentación y demostración, no estudio estacional |
| Debounce e histéresis opcional (RF-24) | **5 muestras**, **0,5 °C** o **2 puntos % HR** | Evitar transiciones repetidas en torno al umbral; verificar en PU-13, no se atribuye a NEDCC |
| Exactitud aceptada (RNF-07) | **<±1 °C** y **<±5 puntos % HR** contra referencia | Criterio de aceptación provisional sujeto al instrumento y montaje |
| Operación continua (RNF-04) | **24 h** | Prueba académica de funcionamiento, no de clima anual |

**Limitación histórica:** con registros agregados por minuto se conocen promedios y extremos; no se puede reconstruir la duración exacta de una desviación de pocos segundos si no se guarda además un conteo o evento con sello temporal. Esa funcionalidad sería una ampliación y **no se declara implementada**.

La tolerancia de ±5 puntos porcentuales HR implica que ver 50 % HR no demuestra que el valor verdadero sea ≤50 %. No usar el resultado para prometer preservación garantizada.

## 5. Instalación y validación

- Colocar el **elemento sensible** del SHT31 en el volumen del aire que rodea los documentos, a altura representativa, por encima del suelo, sin tocar los objetos; alejado de puertas, ventanas, aire acondicionado, calefacción o condensación.
- Mantener Milk-V y STM32 fuera del gabinete o térmicamente aisladas de la sonda, pues podrían introducir calentamiento local (RNF-06, PS-03).
- Anotar gabinete, posición del sensor, hora de instalación y fecha de última comparación con termohigrómetro (PS-02). Revisar calibración conforme al fabricante.
- Una sola sonda **no garantiza uniformidad espacial**: para afirmar representación de todo el gabinete o de varios compartimentos se necesitarían medidas adicionales.
- NEDCC 2.2 recomienda alrededor de **3 meses por ubicación** cuando se utiliza un monitor limitado para construir un perfil, y **un año** de datos antes de cambios sustanciales en edificios. La demostración del curso y las 24 h de PS-01 **no sustituyen** esas escalas.

## 6. Demostración reproducible en clase

1. Disponer una caja o gabinete con documentos de prueba; colocar el sensor correctamente y separar placas de la zona de medición.
2. Comprobar el intercambio UART STM32–Milk-V, mediciones válidas y contraste con un instrumento de referencia.
3. Configurar el perfil 18–22 °C / 35–50 % HR, o mover **temporalmente y de forma explícita** un umbral para poder demostrar una alarma en el ambiente real.
4. Abrir la puerta u ocasionar otra perturbación controlada sin dañar los documentos; registrar valores observados, tiempos de alarma y recuperación, y el historial en Linux.
5. Verificar que el dashboard comunica una **desviación ambiental** y no un diagnóstico de deterioro.

**Estado:** hipótesis de aplicación documentada; la validación del gabinete, las placas, la calibración y la ejecución de las 53 pruebas del proyecto siguen pendientes.

## 7. Referencias (consultadas el 20 de noviembre de 2026)

- Northeast Document Conservation Center. (2022). *2.2 Monitoring temperature and relative humidity*. https://www.nedcc.org/free-resources/preservation-leaflets/2.-the-environment/2.2-monitoring-temperature-and-relative-humidity
- Northeast Document Conservation Center. (2020). *2.1 Temperature, relative humidity, light, and air quality: Basic guidelines for preservation*. https://www.nedcc.org/free-resources/preservation-leaflets/2.-the-environment/2.1-temperature,-relative-humidity,-light,-and-air-quality-basic-guidelines-for-preservation
- Library of Congress. (2026, August 4). *How do I preserve my letters or paper documents?* https://ask.loc.gov/preservation/faq/445765
- Canadian Conservation Institute. (s. f.). *Storing works on paper – CCI Notes 11/2*. Government of Canada. https://www.canada.ca/en/conservation-institute/services/conservation-preservation-publications/canadian-conservation-institute-notes/storing-works-paper.html
