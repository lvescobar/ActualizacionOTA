/**
 * @file main.cpp
 * @brief Firmware del ESP32: actualizacion OTA segura, rollback por software y
 *        deteccion de anomalias con regresion logistica.
 *
 * El mismo codigo genera 4 versiones (entornos de PlatformIO) que cambian con
 * las banderas -DFW_VERSION y -DLED_MODE (ver platformio.ini).
 *
 * | Version | Patron de LED                                  |
 * |---------|------------------------------------------------|
 * | 1.0.0   | Rojo fijo                                      |
 * | 1.1.0   | Amarillo parpadeando lento                     |
 * | 1.2.0   | Rojo + amarillo fijos (normal), con IA         |
 * | 1.3.0   | Version mala a proposito (prueba de rollback)  |
 *
 * Datos privados (clave Wi-Fi e IP del servidor): ver secrets.example.h.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <mbedtls/sha256.h>
#include <esp_ota_ops.h>

#include "detector_logistico.h"
#include "secrets.h"   // copia secrets.example.h -> secrets.h y llena tus datos

#ifndef FW_VERSION
/** Version del firmware (la define platformio.ini con -DFW_VERSION). */
#define FW_VERSION "1.0.0"
#endif
#ifndef LED_MODE
/** Patron de LED / funciones de la version (0, 1 o 2). */
#define LED_MODE 0
#endif

const char* WIFI_SSID = SECRET_WIFI_SSID;   ///< Nombre de la red Wi-Fi.
const char* WIFI_PASS = SECRET_WIFI_PASS;   ///< Clave de la red Wi-Fi.
const String SERVER   = SECRET_SERVER;      ///< URL del servidor FastAPI en el PC.

#define PIN_Y 5    ///< LED amarillo (con resistencia a GND).
#define PIN_R 19   ///< LED rojo (con resistencia a GND).
// Sensor tactil: T0 = GPIO 4 (cable suelto en ese pin).

Preferences prefs;   ///< Memoria no volatil (NVS): guarda "pending", "tries" y "bad".

/** @brief Estados del dispositivo que decide la IA. */
enum Estado { NORMAL, ANOMALIA, FALLO_SENSOR };
Estado estado = NORMAL;   ///< Estado actual.

/** Detector con regresion logistica: piso de desviacion 1.0 C (temperatura) y 4.0 (tacto). */
DetectorLogistico detector(1.0f, 4.0f);

// ---------------------------------------------------------------- LEDs ----

/**
 * @brief Enciende o apaga los dos LEDs.
 * @param y true = amarillo encendido.
 * @param r true = rojo encendido.
 */
void setLeds(bool y, bool r) {
  digitalWrite(PIN_Y, y); digitalWrite(PIN_R, r);
}

/**
 * @brief Dibuja el patron de LED de la version y del estado actual.
 *
 * Debe llamarse en cada vuelta de loop(). Usa millis() en lugar de delay()
 * para parpadear sin detener el resto del programa.
 */
void actualizarLED() {
  static unsigned long t = 0; static bool on = false;
#if LED_MODE == 0
  setLeds(0, 1);                                                          // v1.0.0: ROJO fijo
#elif LED_MODE == 1
  if (millis() - t >= 500) { t = millis(); on = !on; setLeds(on, 0); }    // v1.1.0: AMARILLO lento
#else                                                                      // v1.2.0
  if (estado == NORMAL) setLeds(1, 1);                                    // ambos fijos = todo bien
  else if (estado == ANOMALIA) {
    if (millis() - t >= 150) { t = millis(); on = !on; setLeds(on, 0); }  // amarillo rapido = anomalia
  } else {
    if (millis() - t >= 150) { t = millis(); on = !on; setLeds(0, on); }  // rojo rapido = sensor caido
  }
#endif
}

// ---------------------------------------------------- Rollback (software) ----

/**
 * @brief Cuenta los arranques de un firmware recien actualizado.
 *
 * Despues de un OTA la bandera "pending" queda en true. Si el firmware nuevo
 * reinicia 3 veces sin llamar a marcarValida(), se marca como mala ("bad") y se
 * vuelve a la particion anterior. Solo cubre fallos que ocurren en setup()/loop(),
 * no en el bootloader (eso requiere CONFIG_APP_ROLLBACK_ENABLE en ESP-IDF).
 */
void bootCheck() {
  prefs.begin("fw", false);
  if (prefs.getBool("pending", false)) {
    uint8_t t = prefs.getUChar("tries", 0) + 1;
    prefs.putUChar("tries", t);
    Serial.printf("Arranque sin validar %u/3\n", t);
    if (t >= 3) {                                    // 3 arranques sin validar
      Serial.println("ROLLBACK: se vuelve a la version anterior");
      prefs.putString("bad", FW_VERSION);
      prefs.putBool("pending", false);
      prefs.putUChar("tries", 0);
      const esp_partition_t* prev = esp_ota_get_next_update_partition(NULL);
      if (prev && esp_ota_set_boot_partition(prev) == ESP_OK) ESP.restart();
    }
  }
}

/** @brief Confirma que el firmware nuevo funciona (cancela el rollback). */
void marcarValida() {
  prefs.putBool("pending", false);
  prefs.putUChar("tries", 0);
}

// ------------------------------------------------- OTA con SHA-256 ----

/**
 * @brief Descarga un .bin, calcula su SHA-256 mientras llega y lo instala solo
 *        si el hash coincide.
 * @param url          Direccion del .bin en el servidor.
 * @param hashEsperado SHA-256 (hexadecimal) que publico el servidor.
 * @return true si se instalo; false si fallo (red, tamano o hash distinto).
 *
 * Mientras descarga, los LEDs rojo y amarillo se alternan rapido. La particion de
 * arranque solo cambia en Update.end(), despues de validar el hash.
 */
bool aplicarOTA(const String& url, String hashEsperado) {
  HTTPClient http;
  http.begin(url);
  if (http.GET() != 200) { http.end(); return false; }
  int total = http.getSize();
  if (total <= 0 || !Update.begin(total)) { http.end(); return false; }

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);

  WiFiClient* s = http.getStreamPtr();
  uint8_t buf[1024];
  int restante = total;
  unsigned long ultimo = millis();
  bool alt = false; unsigned long ta = 0;

  while (restante > 0 && http.connected()) {
    if (millis() - ta >= 150) { ta = millis(); alt = !alt; setLeds(alt, !alt); }  // descargando: se alternan
    size_t disp = s->available();
    if (disp) {
      int n = s->readBytes(buf, min(disp, sizeof(buf)));
      if (Update.write(buf, n) != (size_t)n) break;
      mbedtls_sha256_update(&ctx, buf, n);
      restante -= n;
      ultimo = millis();
    } else if (millis() - ultimo > 10000) break;     // corte de red
    delay(1);
  }
  http.end();

  if (restante != 0) { Update.abort(); mbedtls_sha256_free(&ctx); Serial.println("OTA incompleta"); return false; }

  uint8_t h[32];
  mbedtls_sha256_finish(&ctx, h);
  mbedtls_sha256_free(&ctx);
  String hex;
  for (int i = 0; i < 32; i++) { char c[3]; sprintf(c, "%02x", h[i]); hex += c; }
  hashEsperado.toLowerCase();

  if (hex != hashEsperado) { Update.abort(); Serial.println("HASH INVALIDO, se descarta"); return false; }
  return Update.end(true);                           // solo aqui se cambia la particion de arranque
}

/**
 * @brief Pregunta al servidor cual es la ultima version y la instala si es nueva.
 *
 * Lee /version.json. No actualiza si la version es la actual o si esa version ya
 * fue marcada como mala por el rollback. Tras un OTA exitoso reinicia el ESP32.
 */
void checkOTA() {
  HTTPClient http;
  http.begin(SERVER + "/version.json");
  int code = http.GET();
  Serial.printf("version.json -> HTTP %d\n", code);
  if (code != 200) { http.end(); return; }
  JsonDocument doc;
  if (deserializeJson(doc, http.getString())) { http.end(); return; }
  http.end();

  String nueva = doc["version"].as<String>();
  String bin   = doc["url"].as<String>();
  String hash  = doc["sha256"].as<String>();
  if (nueva == FW_VERSION || nueva == prefs.getString("bad", "")) return;

  Serial.println("Actualizando a " + nueva);
  if (aplicarOTA(bin, hash)) {
    prefs.putBool("pending", true);
    prefs.putUChar("tries", 0);
    delay(200);
    ESP.restart();                                   // el OTA reinicia solo, no hace falta pulsar EN
  }
}

// ------------------------------------------------------ Utilidades ----

/** @brief Conecta al Wi-Fi si no lo esta (espera maximo 15 s). */
void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) delay(200);
}

/**
 * @brief Envia una muestra al servidor (POST /telemetry, formato JSON).
 * @param t     Temperatura interna del chip (grados C).
 * @param tacto Valor del sensor tactil (el servidor lo guarda en el campo "luz").
 * @param anom  true si la IA la marco como anomalia.
 */
void enviar(float t, int tacto, bool anom) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(SERVER + "/telemetry");
  http.addHeader("Content-Type", "application/json");
  String body = String("{\"fw\":\"") + FW_VERSION + "\",\"temp\":" + String(t) +
                ",\"luz\":" + tacto + ",\"anomalia\":" + (anom ? "true" : "false") +
                ",\"rssi\":" + WiFi.RSSI() + "}";
  http.POST(body);
  http.end();
}

// ------------------------------------------------ Programa principal ----

/** @brief Se ejecuta una vez al encender: pines, rollback y Wi-Fi. */
void setup() {
  Serial.begin(115200);
  pinMode(PIN_Y, OUTPUT); pinMode(PIN_R, OUTPUT);
  bootCheck();
  Serial.println("FW " FW_VERSION);
  conectarWiFi();
  Serial.print("IP ESP32: "); Serial.println(WiFi.localIP());
  Serial.print("Gateway:  "); Serial.println(WiFi.gatewayIP());
  Serial.print("SERVER:   "); Serial.println(SERVER);
}

unsigned long tMuestra = 0;   ///< Instante de la ultima muestra (ms).
unsigned long tOTA = 0;       ///< Instante de la ultima consulta OTA (ms).
int contEnvio = 0;            ///< Contador para enviar 1 de cada 3 con senal debil.
int pegado = 0;               ///< Lecturas seguidas con tacto <= 2 (sensor caido).
bool validada = false;        ///< Ya se llamo a marcarValida() en este arranque.

/**
 * @brief Ciclo infinito: LEDs, muestreo con IA, telemetria y consulta OTA.
 *
 * Gestion del dispositivo segun la IA:
 *  - Anomalia: muestrea cada 1 s (normal: cada 5 s).
 *  - Sensor caido (tacto <= 2 seis veces seguidas): estado FALLO_SENSOR.
 *  - RSSI < -80 dBm: envia 1 de cada 3 muestras y consulta OTA cada 60 s.
 */
void loop() {
  actualizarLED();

#ifdef SIMULAR_FALLO
  if (millis() > 8000) ESP.restart();                // v1.3.0: se reinicia sin validarse nunca
#endif

  unsigned long intervalo = (estado == ANOMALIA) ? 1000 : 5000;

  if (millis() - tMuestra >= intervalo) {
    tMuestra = millis();
    float t = temperatureRead();       // grados C del chip
    int tacto = touchRead(T0);         // GPIO 4

    // Sensor caido: cable en corto a GND (lectura ~0) varias veces seguidas
    pegado = (tacto <= 2) ? pegado + 1 : 0;

    if (pegado >= 6) {
      estado = FALLO_SENSOR;
    } else {
      bool anom = false;
#if LED_MODE == 2
      anom = detector.evaluar(t, (float)tacto);      // regresion logistica (solo en v1.2.0)
#endif
      estado = anom ? ANOMALIA : NORMAL;

#ifndef SIMULAR_FALLO
      if (!validada && WiFi.status() == WL_CONNECTED) { marcarValida(); validada = true; }
#endif
      Serial.printf("T=%.1f tacto=%d anom=%d p=%.2f\n", t, tacto, anom, detector.probabilidad());
      // Senal debil -> reporta 1 de cada 3 muestras
      if (WiFi.RSSI() > -80 || ++contEnvio % 3 == 0) enviar(t, tacto, anom);
    }
  }

  // Consulta OTA: cada 30 s (60 s si la senal es debil)
  unsigned long perOTA = (WiFi.RSSI() < -80) ? 60000 : 30000;
  if (millis() - tOTA >= perOTA) { tOTA = millis(); conectarWiFi(); checkOTA(); }
}
