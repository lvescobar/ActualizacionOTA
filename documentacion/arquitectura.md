@page arquitectura Arquitectura general

## Componentes

El sistema tiene tres partes que se comunican por HTTP dentro del mismo hotspot Wi-Fi.

```
          Hotspot Wi-Fi (celular)
   ┌──────────────┐                 ┌─────────────────────────┐
   │    ESP32     │  POST /telemetry│  PC: servidor FastAPI   │
   │  (firmware)  │ ───────────────►│  - guarda telemetria.csv│
   │  LED rojo    │                 │                         │
   │  LED amarillo│  GET /version.json                        │
   │  sensor tacto│ ───────────────►│  - entrega el manifiesto│
   │  temp. chip  │                 │                         │
   │              │  GET /fw/firmware_X.bin                   │
   │              │ ───────────────►│  - entrega el .bin      │
   └──────────────┘                 └─────────────────────────┘
                                          ▲
                                          │ publicar.py copia el .bin a fw/
                                          │ y escribe fw/manifest.json (con SHA-256)
                                    Compilación con PlatformIO (pio run -e vXXX)
```

## Qué hace cada pieza

- **ESP32 (`main.cpp`)**: cada 5 s lee la temperatura del chip y el sensor táctil (GPIO 4),
  decide con la IA si es una anomalía y envía la muestra al servidor. Cada 30 s pregunta si
  hay una versión nueva.
- **Servidor (`servidor/main.py`)**: usa FastAPI y tiene tres funciones:
  - `GET /version.json`: devuelve el manifiesto de la última versión publicada. Responde 404 si
    todavía no se ha publicado ninguna.
  - `POST /telemetry`: recibe una muestra en JSON y la agrega a `telemetria.csv`.
  - `/fw`: carpeta de archivos estáticos desde donde se descarga el `.bin`.
- **`publicar.py`**: prepara una versión nueva (copia el `.bin` y crea el manifiesto).
- **PlatformIO (`platformio.ini`)**: compila el mismo código en cuatro versiones cambiando solo
  dos banderas de compilación.

## Una sola base de código, cuatro versiones

El archivo `platformio.ini` define cuatro entornos. Cada uno pasa al compilador dos
banderas con `-D`:

| Entorno | `FW_VERSION` | `LED_MODE` | Comportamiento |
|---|---|---|---|
| `v100` | 1.0.0 | 0 | Rojo fijo |
| `v110` | 1.1.0 | 1 | Amarillo parpadeando lento |
| `v120` | 1.2.0 | 2 | Rojo + amarillo fijos y detector de IA activo |
| `v130` | 1.3.0 | 1 | Igual que v110, más `SIMULAR_FALLO`: se reinicia a los 8 s (prueba de rollback) |

Así se ve claramente qué versión corre cada vez que el LED cambia, sin necesitar pantalla.

## Datos privados

La clave del Wi-Fi y la IP del servidor están en `src/secrets.h`, que **no se sube a
GitHub** (está en `.gitignore`). El repositorio solo trae `secrets.example.h` como plantilla.
La IP del PC puede cambiar cuando se reconecta el hotspot; debe ser igual en `secrets.h`
y en `servidor/publicar.py`, y al cambiarla hay que recompilar las versiones.

## Formato de la telemetría

Cada muestra enviada por el ESP32 es un JSON así:

```
{"fw":"1.2.0","temp":64.44,"luz":18,"anomalia":false,"rssi":-31}
```

El campo `luz` contiene el valor del sensor táctil (el nombre se conservó por compatibilidad).
El servidor lo guarda en `telemetria.csv` con las columnas
`timestamp, fw, temp, tacto, anomalia, rssi`.
