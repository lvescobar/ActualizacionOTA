/**
 * @file secrets.example.h
 * @brief Plantilla de datos privados (NO contiene claves reales).
 *
 * Copia este archivo como `secrets.h` y escribe tus datos:
 *   copy src\secrets.example.h src\secrets.h
 * El archivo `secrets.h` esta en .gitignore y nunca se sube a GitHub.
 */
#ifndef SECRETS_H
#define SECRETS_H

#define SECRET_WIFI_SSID "NOMBRE_DE_TU_HOTSPOT"
#define SECRET_WIFI_PASS "CLAVE_DE_TU_HOTSPOT"
/** IP del PC en la red del hotspot (cambia entre sesiones; mirala con ipconfig). */
#define SECRET_SERVER    "http://192.168.0.0:8000"

#endif  // SECRETS_H
