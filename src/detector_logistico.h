/**
 * @file detector_logistico.h
 * @brief Detector de anomalias con regresion logistica para el ESP32.
 *
 * No depende de Arduino: solo usa <math.h>. Por eso se puede probar en un PC
 * normal con g++ antes de subirlo al ESP32.
 *
 * Idea general:
 *  1. Calibracion: con las primeras 30 muestras se calcula la media y la
 *     desviacion estandar de cada variable (temperatura y valor tactil).
 *  2. Cada muestra se convierte a "desviaciones estandar": z = (x - media) / desviacion.
 *  3. Caracteristicas: |z| de cada variable (limitado a 10).
 *  4. Regresion logistica: p = sigmoide(SESGO + W_TEMP*|z_temp| + W_TACTO*|z_tacto|).
 *     p es la probabilidad de anomalia. Si p > 0.5 es anomalia.
 *
 * @note La regresion logistica es SUPERVISADA: sus pesos se entrenan en el PC
 *       con entrenamiento/entrenar_logistica.py. El ESP32 solo calcula la prediccion.
 */
#ifndef DETECTOR_LOGISTICO_H
#define DETECTOR_LOGISTICO_H

#include <math.h>

/**
 * @class DetectorLogistico
 * @brief Clasifica cada muestra (temperatura, tacto) como normal o anomalia.
 */
class DetectorLogistico {
public:
  /** Cantidad de muestras que se usan para aprender lo "normal". */
  static constexpr int MUESTRAS_CALIBRACION = 30;
  /** Peso de |z| de la temperatura (resultado de entrenar_logistica.py). */
  static constexpr float W_TEMP  = 1.6997f;
  /** Peso de |z| del tacto (resultado de entrenar_logistica.py). */
  static constexpr float W_TACTO = 1.6760f;
  /** Sesgo (termino independiente) del modelo. */
  static constexpr float SESGO   = -7.3083f;
  /** Probabilidad minima para declarar anomalia. */
  static constexpr float UMBRAL_P = 0.5f;
  /** Tope de |z| para que un valor extremo no sature el modelo. */
  static constexpr float TOPE_Z  = 10.0f;

  /**
   * @brief Constructor.
   * @param stdMinTemp  Piso de la desviacion estandar de la temperatura (grados C).
   * @param stdMinTacto Piso de la desviacion estandar del valor tactil.
   *
   * El piso evita dividir por casi cero cuando el sensor casi no varia.
   */
  DetectorLogistico(float stdMinTemp, float stdMinTacto) {
    stdMin_[0] = stdMinTemp;
    stdMin_[1] = stdMinTacto;
  }

  /**
   * @brief Evalua una muestra.
   * @param temp  Temperatura interna del chip (grados C).
   * @param tacto Valor del sensor tactil.
   * @return true si la muestra es una anomalia, false si es normal.
   *
   * Durante la calibracion siempre devuelve false. Las muestras anomalas
   * no actualizan la media (para no "acostumbrarse" a las anomalias).
   */
  bool evaluar(float temp, float tacto) {
    const float x[2] = {temp, tacto};

    // ---- Fase 1: calibracion (algoritmo de Welford: media y varianza) ----
    if (n_ < MUESTRAS_CALIBRACION) {
      n_++;
      for (int i = 0; i < 2; i++) {
        float d = x[i] - media_[i];
        media_[i] += d / n_;
        m2_[i] += d * (x[i] - media_[i]);
      }
      if (n_ == MUESTRAS_CALIBRACION) {
        for (int i = 0; i < 2; i++) {
          float s = sqrtf(m2_[i] / (MUESTRAS_CALIBRACION - 1));
          desv_[i] = (s > stdMin_[i]) ? s : stdMin_[i];
        }
      }
      ultimaProb_ = 0.0f;
      return false;
    }

    // ---- Fase 2: caracteristicas |z| (limitadas a TOPE_Z) ----
    float f[2];
    for (int i = 0; i < 2; i++) {
      f[i] = fabsf(x[i] - media_[i]) / desv_[i];
      if (f[i] > TOPE_Z) f[i] = TOPE_Z;
    }

    // ---- Fase 3: regresion logistica ----
    float s = SESGO + W_TEMP * f[0] + W_TACTO * f[1];
    ultimaProb_ = 1.0f / (1.0f + expf(-s));
    bool anom = ultimaProb_ > UMBRAL_P;

    // ---- Fase 4: solo los datos normales ajustan la media (deriva lenta) ----
    if (!anom) {
      for (int i = 0; i < 2; i++) media_[i] += TASA_DERIVA * (x[i] - media_[i]);
    } else {
      nAnomalias_++;
    }
    return anom;
  }

  /** @return true cuando ya termino la calibracion. */
  bool calibrado() const { return n_ >= MUESTRAS_CALIBRACION; }
  /** @return Probabilidad de anomalia de la ultima muestra (0 a 1). */
  float probabilidad() const { return ultimaProb_; }
  /** @return Cantidad de muestras clasificadas como anomalia. */
  int totalAnomalias() const { return nAnomalias_; }

private:
  /** Que tanto se mueve la media con cada muestra normal (0 a 1). */
  static constexpr float TASA_DERIVA = 0.05f;

  float stdMin_[2];                 ///< Pisos de desviacion estandar.
  float media_[2] = {0, 0};         ///< Media de cada variable.
  float m2_[2]    = {0, 0};         ///< Acumulador de Welford.
  float desv_[2]  = {1, 1};         ///< Desviacion estandar final de cada variable.
  float ultimaProb_ = 0.0f;         ///< Probabilidad de anomalia de la ultima muestra.
  int   n_ = 0;                     ///< Muestras de calibracion recibidas.
  int   nAnomalias_ = 0;            ///< Total de anomalias detectadas.
};

#endif  // DETECTOR_LOGISTICO_H
