# ESP32 OTA con IA {#mainpage}

Proyecto universitario que combina tres ideas en un ESP32:

1. **Actualización OTA segura** (Over-The-Air, "por el aire"): el ESP32 descarga
   un firmware nuevo por Wi-Fi y lo instala **solo si** su huella SHA-256 coincide.
2. **Rollback por software**: si el firmware nuevo falla al arrancar 3 veces seguidas,
   el ESP32 vuelve solo a la versión anterior.
3. **Detección de anomalías con regresión logística**: el ESP32 mide su temperatura
   interna y un sensor táctil, y decide si cada medida es normal o anómala.

Todo se demuestra con dos LEDs (rojo y amarillo), un servidor FastAPI en el PC y
telemetría guardada en `telemetria.csv`.

## Páginas de esta documentación

- @subpage arquitectura : cómo se conectan el ESP32, el servidor y los LEDs.
- @subpage ota : el proceso OTA explicado paso a paso (manifiesto, descarga, SHA-256, instalación).
- @subpage rollback : cómo se protege el equipo si el firmware nuevo es malo.
- @subpage ia : el modelo de regresión logística, cómo se entrenó y cómo decide.
- @subpage evidencias : capturas y GIFs de cada prueba.

Además, el menú **Clases** y **Archivos** contiene la documentación del código
(`main.cpp`, `detector_logistico.h`, `main.py`, `publicar.py`, `entrenar_logistica.py`).

## Estructura del proyecto

| Archivo | Para qué sirve |
|---|---|
| `src/main.cpp` | Firmware: OTA, rollback, LEDs, sensores y telemetría. |
| `src/detector_logistico.h` | Detector de anomalías con regresión logística. |
| `src/secrets.example.h` | Plantilla de datos privados (se copia a `secrets.h`). |
| `entrenamiento/entrenar_logistica.py` | Entrena en el PC los pesos de la regresión logística. |
| `servidor/main.py` | Servidor FastAPI: `/version.json`, `/telemetry` y `/fw`. |
| `servidor/publicar.py` | Publica una versión nueva y calcula su SHA-256. |
| `platformio.ini` | Define las 4 versiones del firmware. |

## Versiones y LEDs

| Versión | Patrón de LED |
|---|---|
| 1.0.0 | Rojo fijo |
| 1.1.0 | Amarillo parpadeando lento |
| 1.2.0 | Rojo + amarillo fijos (normal), con IA |
| 1.2.0 con anomalía | Amarillo parpadeando rápido |
| 1.2.0 con sensor caído | Rojo parpadeando rápido |
| Descargando OTA | Rojo y amarillo alternándose rápido |
| 1.3.0 | Versión mala a propósito (prueba de rollback) |

## Cómo usarlo

1. `copy src\secrets.example.h src\secrets.h` y llena Wi-Fi e IP del PC (`ipconfig`).
2. Servidor (carpeta `servidor/`): `python -m uvicorn main:app --host 0.0.0.0 --port 8000`
3. Compilar (carpeta raíz): `pio run -e v120`
4. Publicar (carpeta `servidor/`): `python publicar.py 1.2.0 ..\.pio\build\v120\firmware.bin`

## Limitación conocida

El rollback por software solo cubre fallos que ocurren desde `setup()`. El rollback del
bootloader requeriría `CONFIG_APP_ROLLBACK_ENABLE` en ESP-IDF.
