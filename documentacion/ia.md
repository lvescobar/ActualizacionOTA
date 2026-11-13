@page ia Modelo de IA: regresión logística

Esta página explica qué hace la inteligencia artificial del ESP32 (versión 1.2.0), cómo se
construyó y por qué decide lo que decide. El código está en `src/detector_logistico.h` (clase
`DetectorLogistico`) y el entrenamiento en `entrenamiento/entrenar_logistica.py`.

## 1. El problema

El ESP32 toma dos medidas cada 5 s:

- **Temperatura interna del chip** (`temperatureRead()`, en °C).
- **Valor del sensor táctil** (`touchRead(T0)`, GPIO 4, con un cable suelto como antena).
  Sin tocar da valores de unos 69-71; al tocar el cable baja mucho (por ejemplo 0 a 18).

Se quiere decidir, para **cada muestra**, si es **normal (0)** o **anomalía (1)**. Es un
problema de **clasificación binaria**, y para eso se usa regresión logística.

## 2. Por qué regresión logística

- Es un modelo **supervisado**: aprende de ejemplos con etiqueta (0 = normal, 1 = anomalía).
- Entrega una **probabilidad** (de 0 a 1) en lugar de solo "sí o no". Esa probabilidad se ve en el
  monitor como `p=...`.
- Es muy liviano: para predecir solo hace sumas, multiplicaciones y una exponencial. Cabe sin
  problema en un ESP32 y no necesita librerías de machine learning.

## 3. Cómo funciona, paso a paso

### Paso 1: Calibración (primeras 30 muestras)

Cada ESP32 y cada entorno son distintos (otra temperatura, otro cable), así que el equipo
**aprende lo normal por sí mismo** al arrancar. Con las primeras 30 muestras (unos 2.5 minutos)
calcula, para cada variable, la **media** y la **desviación estándar** con el algoritmo de
Welford (calcula ambas sin guardar las 30 muestras).

Durante la calibración el detector siempre responde "normal" y `p=0.00`.

> **Importante:** durante esos 2.5 minutos no hay que tocar el cable. Si se toca, el modelo
> "aprende" que ese valor es normal y deja de detectarlo. Si ocurre, basta reiniciar con el botón EN.

La desviación estándar tiene un **piso** (`stdMin`): 1.0 °C para la temperatura y 4.0 para el
tacto. Evita dividir por casi cero cuando el sensor casi no varía.

### Paso 2: Normalizar (puntaje z)

Cada medida nueva se convierte a "cuántas desviaciones estándar se aleja de lo normal":

```
z = (x - media) / desviación
```

Así la temperatura (en °C) y el tacto (en unidades del sensor) quedan en la misma escala y
pesan lo mismo.

### Paso 3: Características

Las entradas del modelo son los valores absolutos `|z|` de cada variable, limitados a 10 para
que un valor extremo no sature el modelo:

```
f1 = min(|z_temperatura|, 10)
f2 = min(|z_tacto|,       10)
```

Se usa el valor absoluto porque da igual que la medida suba o baje: lo que importa es qué tan
lejos está de lo normal.

### Paso 4: Regresión logística

```
s = SESGO + W_TEMP * f1 + W_TACTO * f2
p = 1 / (1 + e^(-s))          (función sigmoide)
anomalía = (p > 0.5)
```

La sigmoide convierte cualquier número `s` en una probabilidad entre 0 y 1. Con los pesos
entrenados:

| Parámetro | Valor | Significado |
|---|---|---|
| `W_TEMP` | 1.6997 | Cuánto aumenta `s` por cada desviación de temperatura. |
| `W_TACTO` | 1.6760 | Cuánto aumenta `s` por cada desviación de tacto. |
| `SESGO` | -7.3083 | Valor base: hace que lo normal dé probabilidad casi cero. |

### Paso 5: Qué probabilidad se obtiene

Para una sola variable alejada `z` desviaciones (la otra normal):

| Distancia en desviaciones (z) | Probabilidad p | Decisión |
|---|---|---|
| 0 | 0.001 | normal |
| 2 | 0.019 | normal |
| 3 | 0.093 | normal |
| 4 | 0.353 | normal |
| **4.36** | **0.500** | **frontera** |
| 5 | 0.745 | anomalía |
| 6 | 0.940 | anomalía |
| 10 o más | 1.000 | anomalía |

La frontera cae en unas **4.3 desviaciones**. Con la calibración típica del proyecto
(tacto cercano a 69 y piso de desviación 4.0), eso equivale a:

- **Tacto:** anomalía cuando baja de unos **52** (o sube de unos 86).
- **Temperatura:** anomalía cuando se aleja unos **4.3 °C** de su media (piso de 1.0 °C).

Esto coincide con lo observado en las pruebas: tacto 57-69 da `p` entre 0.00 y 0.08 (normal) y tacto
0-18 da `p=1.00` (anomalía).

### Paso 6: Adaptación a la deriva lenta

La temperatura del chip sube al calentarse. Para no dar falsas alarmas, cada muestra
**normal** mueve la media un 5 % hacia el valor nuevo. Las muestras **anómalas no actualizan la
media**: así, tocar el cable muchas veces no hace que lo anormal pase a "normal".

## 4. Cómo se entrenó

La regresión logística necesita ejemplos con etiqueta, y el ESP32 no los tiene. Por eso el
entrenamiento se hace **una vez en el PC** con `entrenamiento/entrenar_logistica.py`:

1. **Genera los datos en espacio z** (4000 ejemplos de cada clase):
   - *Normales (etiqueta 0):* valores `|z|` de una distribución normal estándar, para ambas variables.
   - *Anomalías (etiqueta 1):* una de las dos variables se aleja entre 3 y 40 desviaciones y la otra
     queda normal (representa un toque del cable o un calentamiento fuerte).
2. **Limita** las características a 10, igual que el ESP32.
3. **Entrena** con descenso de gradiente (tasa 0.1, 5000 iteraciones) y regularización L2
   (1e-3, evita pesos exagerados). El descenso de gradiente ajusta los pesos poco a poco para
   reducir el error entre `p` y la etiqueta real.
4. **Evalúa**: 99 % de exactitud en los datos de entrenamiento. El 1 % de error está en la zona
   de solapamiento (entre 3 y 4.3 desviaciones), donde una medida "muy rara" y una "anomalía suave"
   se parecen.
5. **Imprime los pesos** (`W_TEMP`, `W_TACTO`, `SESGO`), que se copian a `detector_logistico.h`.

Para reentrenar: `python entrenar_logistica.py` (requiere `pip install numpy`) y pegar los
nuevos valores en las constantes de `DetectorLogistico`.

### Qué datos se usaron: aclaración

El entrenamiento usa **datos simulados** en espacio z, no mediciones reales del ESP32. Esto es
válido porque el ESP32 normaliza sus lecturas con su propia calibración: el modelo solo ve "qué tan
lejos de lo normal", y eso se puede simular. La validación con datos reales se hizo después, en las
pruebas con el cable (sección 6).

## 5. Qué hace el equipo con la decisión

| Resultado | LED | Comportamiento |
|---|---|---|
| Normal | Rojo + amarillo fijos | Muestrea cada 5 s. |
| Anomalía | Amarillo parpadeando rápido | Muestrea cada **1 s** y envía `anomalia: true`. |
| Sensor caído | Rojo parpadeando rápido | Ver abajo. |

**Sensor caído:** no lo decide el modelo, sino una regla simple. Si el tacto vale 2 o menos
**seis veces seguidas** (cable a GND), se declara fallo del sensor y no se evalúa la IA. Es
distinto de un toque: al tocar, el valor baja pero rara vez queda pegado en cero tanto tiempo.

## 6. Validación

- **En el PC (simulación con `g++`):** con la calibración hecha, el detector no dio falsos positivos
  en 2200 muestras normales y detectó 200 de 200 anomalías simuladas.
- **En el ESP32 (datos reales):** el monitor muestra `anom=1 p=1.00` al tocar el cable, y
  `anom=0` con `p` de 0.00 a 0.08 al soltarlo. El servidor registra `anomalia: True` y `False`
  en esos momentos (ver @ref evidencias).

## 7. Limitaciones

- La **calibración debe ser limpia**: si se toca el cable en los primeros 2.5 minutos, el modelo
  toma ese comportamiento como normal. La calibración no se guarda, así que se repite en cada reinicio.
- Los pesos se entrenaron con **datos simulados**, no con anomalías reales etiquetadas.
- Con dos variables y una frontera simple (suma de desviaciones) el modelo es muy sencillo.
  Detecta bien valores extremos, pero no detectaría un patrón raro formado por valores
  individualmente normales.
- Como la temperatura del chip depende de la carga del Wi-Fi, un calentamiento brusco
  (más de unos 4 °C) se marcaría como anomalía.
