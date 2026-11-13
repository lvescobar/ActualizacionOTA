@page ota Actualización OTA paso a paso

OTA significa *Over-The-Air*: actualizar el programa del ESP32 por Wi-Fi, sin cable USB.
Esta página explica cada etapa del proceso tal como está implementado en `main.cpp`,
`servidor/main.py` y `servidor/publicar.py`.

## Visión general

```
1. Compilar         pio run -e v120                  -> firmware.bin
2. Publicar         python publicar.py 1.2.0 ...     -> fw/firmware_1.2.0.bin + fw/manifest.json
3. Consultar        ESP32: GET /version.json         (cada 30 s)
4. Comparar         ¿la versión del manifiesto es distinta de la mía?
5. Descargar        ESP32: GET /fw/firmware_1.2.0.bin (calculando el SHA-256 mientras llega)
6. Verificar        ¿el hash calculado = el hash del manifiesto?
7. Instalar         Update.end(): cambia la partición de arranque
8. Reiniciar        ESP.restart() arranca la versión nueva
9. Validar          si funciona, marcarValida(); si no, rollback (ver @ref rollback)
```

## 1. Preparar el equipo: dos particiones

El ESP32 tiene la memoria flash dividida en particiones. Con `board_build.partitions =
min_spiffs.csv` (en `platformio.ini`) hay **dos ranuras de aplicación** (`app0` y `app1`).
El firmware corre desde una de ellas y la actualización se escribe en la **otra**. Mientras
se descarga, la versión que está funcionando no se toca: si algo sale mal, el equipo sigue
con la versión vieja.

## 2. Publicar una versión (`publicar.py`)

Se ejecuta en el PC, en la carpeta `servidor/`:

```
python publicar.py 1.2.0 ..\.pio\build\v120\firmware.bin
```

El script hace tres cosas:

1. Copia el `.bin` a `fw/firmware_1.2.0.bin`.
2. Calcula su **SHA-256** (una huella digital de 64 caracteres hexadecimales: si cambia un solo
   bit del archivo, la huella cambia por completo).
3. Escribe `fw/manifest.json` con la versión, la dirección de descarga y la huella:

```
{"version": "1.2.0",
 "url": "http://192.168.53.86:8000/fw/firmware_1.2.0.bin",
 "sha256": "fc71a755ce90f7db41cef1fb4c222c7c5564d593fc57f3e890532e837592028c"}
```

Mientras no se publique nada, `GET /version.json` responde **404**. Eso es lo que se ve en el
monitor con la v1.0.0 (`version.json -> HTTP 404`).

## 3. Consultar si hay versión nueva (`checkOTA`)

Cada **30 segundos** (60 s si la señal Wi-Fi es débil, RSSI < -80 dBm) el ESP32 llama a
`checkOTA()`:

1. Hace `GET /version.json` y muestra `version.json -> HTTP 200` en el monitor.
2. Lee el JSON con la librería ArduinoJson y extrae `version`, `url` y `sha256`.
3. **No actualiza** si la versión del manifiesto es igual a la que ya corre (`FW_VERSION`) o si
   esa versión fue marcada como mala por el rollback (se guarda en la memoria NVS con la clave `bad`).
4. Si es distinta, imprime `Actualizando a X.Y.Z` y llama a `aplicarOTA()`.

> La comparación es de **igualdad**, no de "mayor que". Cualquier versión distinta publicada
> en el servidor se instalaría, incluso una anterior.

## 4. Descargar e instalar (`aplicarOTA`)

Esta es la parte central. La función recibe la URL y el hash esperado y sigue estos pasos:

1. **Abre la descarga**: `GET` a la URL del `.bin`. Si no responde 200, termina sin cambios.
2. **Reserva espacio**: `Update.begin(total)` prepara la partición libre con el tamaño del archivo.
3. **Inicia el cálculo del SHA-256** con la librería mbedTLS.
4. **Descarga en bloques de 1 KB**. Cada bloque se hacen dos cosas a la vez: se escribe en la
   partición libre (`Update.write`) y se agrega al cálculo del hash (`mbedtls_sha256_update`).
   Así no hace falta guardar el archivo completo en RAM.
5. **LEDs durante la descarga**: rojo y amarillo se **alternan rápido** (cada 150 ms).
6. **Protección contra corte de red**: si pasan **10 s sin recibir datos**, se aborta con el
   mensaje `OTA incompleta` y no se cambia nada.
7. **Verificación**: al terminar, se compara el hash calculado con el del manifiesto. Si son
   distintos, `Update.abort()` y el monitor muestra `HASH INVALIDO, se descarta`.
8. **Instalación**: solo si el hash coincide se llama a `Update.end(true)`, que **recién entonces**
   marca la partición nueva como la de arranque.

Si el hash no coincide, el firmware descargado se descarta y el equipo continúa con la
versión actual, sin reiniciarse.

## 5. Reinicio y validación

Cuando `aplicarOTA` devuelve `true`, `checkOTA`:

1. Guarda en la memoria NVS `pending = true` y `tries = 0` ("el firmware nuevo aún no se validó").
2. Espera 200 ms y ejecuta `ESP.restart()`.

El reinicio es **automático**: tras un OTA no hace falta pulsar el botón EN. (El botón EN solo
se necesita después de grabar por USB.) Al arrancar, el firmware nuevo debe validarse solo; ver
la página @ref rollback.

## 6. Qué protege el SHA-256 y qué no

| Protege contra | No protege contra |
|---|---|
| Descarga corrupta o incompleta. | Alguien que controle el servidor y publique un `.bin` malicioso **junto con su hash**. |
| Archivo modificado en tránsito sin recalcular el hash. | Ataques en la red, porque se usa HTTP y no HTTPS. |

Para un producto real se agregaría **HTTPS** y **firma digital** del firmware. En este proyecto
el objetivo es demostrar la integridad de la descarga.

## 7. Comportamiento con señal débil

Si el RSSI es menor que -80 dBm, el ESP32:

- envía **1 de cada 3** muestras de telemetría, y
- consulta el OTA cada **60 s** en lugar de cada 30 s.

## 8. Pruebas realizadas

| Prueba | Cómo se hizo | Resultado esperado |
|---|---|---|
| OTA exitoso 1.0.0 -> 1.1.0 | `publicar.py 1.1.0 ...` | En máximo 30 s: LEDs alternándose y luego amarillo lento. |
| OTA exitoso 1.1.0 -> 1.2.0 | `publicar.py 1.2.0 ...` | LEDs alternándose y luego rojo + amarillo fijos. |
| Hash inválido | Publicar y cambiar una letra del `sha256` en `fw/manifest.json` | Monitor: `HASH INVALIDO`; sigue en la versión anterior. |
| Corte de red | Apagar el hotspot durante la descarga | A los 10 s: `OTA incompleta`; no se instala nada. |
| Señal débil | Tapar el ESP32 con aluminio hasta RSSI < -80 | El servidor recibe 1 de cada 3 muestras. |

Las capturas y GIFs están en la página @ref evidencias.
