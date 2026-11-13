"""
@file entrenar_logistica.py
@brief Entrena (en el PC) la regresion logistica que usa el ESP32 y escribe sus pesos.

La regresion logistica es un metodo SUPERVISADO: necesita ejemplos con etiqueta
(0 = normal, 1 = anomalia). El ESP32 no tiene etiquetas, asi que:
  1. Aqui se generan ejemplos etiquetados en "desviaciones estandar" (z).
  2. Se entrena el modelo con descenso de gradiente.
  3. Los pesos se copian a src/detector_logistico.h.
En el ESP32 solo se calcula la prediccion; la calibracion (media y desviacion)
se hace con las primeras 30 muestras, como antes.

Uso:  python entrenar_logistica.py
Requiere: pip install numpy
"""
import numpy as np

rng = np.random.default_rng(42)
TOPE = 10.0  # las caracteristicas se limitan a 10 desviaciones (igual que el ESP32)


def generar(n=4000):
    """
    @brief Genera ejemplos etiquetados en espacio z.
    @param n Cantidad de ejemplos de cada clase.
    @return (X, y): X con columnas |z_temp| y |z_tacto|; y con 0 normal / 1 anomalia.
    """
    # Normal: ruido normal estandar en ambas variables.
    normal = np.abs(rng.normal(0, 1, (n, 2)))
    # Anomalia: al menos una variable se aleja mucho (toque del cable, calor, etc.).
    anom = np.abs(rng.normal(0, 1, (n, 2)))
    eje = rng.integers(0, 2, n)
    anom[np.arange(n), eje] = rng.uniform(3.0, 40.0, n)
    X = np.vstack([normal, anom])
    y = np.hstack([np.zeros(n), np.ones(n)])
    return np.minimum(X, TOPE), y


def sigmoide(z):
    """@brief Funcion sigmoide: convierte cualquier numero en una probabilidad (0 a 1)."""
    return 1.0 / (1.0 + np.exp(-z))


def entrenar(X, y, tasa=0.1, epocas=5000, l2=1e-3):
    """
    @brief Descenso de gradiente para regresion logistica con regularizacion L2.
    @return (w, b): pesos y sesgo.
    """
    w = np.zeros(X.shape[1]); b = 0.0
    for _ in range(epocas):
        p = sigmoide(X @ w + b)
        w -= tasa * (X.T @ (p - y) / len(y) + l2 * w)
        b -= tasa * np.mean(p - y)
    return w, b


if __name__ == "__main__":
    X, y = generar()
    w, b = entrenar(X, y)
    pred = sigmoide(X @ w + b) > 0.5
    print(f"Exactitud en entrenamiento: {np.mean(pred == y) * 100:.2f} %")
    print("Pegar en src/detector_logistico.h:")
    print(f"  W_TEMP   = {w[0]:.4f}f")
    print(f"  W_TACTO  = {w[1]:.4f}f")
    print(f"  SESGO    = {b:.4f}f")
