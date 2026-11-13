@page evidencias Evidencias de las pruebas

Capturas y videos de cada paso. En las capturas, el **servidor** está a la izquierda y el
**monitor serie del ESP32** a la derecha. El proceso completo se explica en las páginas
@ref ota, @ref rollback y @ref ia.

@section paso1 1. Punto de partida: versión 1.0.0 (LED rojo fijo)

Se borra la memoria flash del ESP32 (`pio run -e v100 -t erase`) y se sube la v1.0.0 por USB
(`pio run -e v100 -t upload`). Así se parte de un equipo limpio.

@image html 01_borrado_y_carga_v100.png "Borrado (erase) y carga por USB de la v1.0.0"

El monitor muestra `FW 1.0.0`, la IP del ESP32 en el hotspot y la dirección del servidor. El
servidor recibe la telemetría (`POST /telemetry 200 OK`). Como todavía no hay nada publicado,
la consulta OTA responde 404.

@image html 02_v100_monitor_y_servidor.png "FW 1.0.0 en el monitor y telemetría recibida en el servidor"

@section paso2 2. OTA a la versión 1.1.0 (amarillo parpadeando lento)

Se publica la v1.1.0 con `publicar.py`. El script copia el `.bin`, calcula su SHA-256 y crea el
manifiesto. En la captura se ve el `404 Not Found` anterior y luego la publicación. En el
siguiente ciclo de consulta (máximo 30 s), el ESP32 descarga el firmware, verifica el hash y se
reinicia solo. Mientras descarga, los LEDs rojo y amarillo se alternan rápido.

@image html 03_publicar_v110.png "Antes: version.json -> 404. Después: Publicada 1.1.0 con su SHA-256"

@htmlonly
<video controls loop muted style="max-width:100%;width:800px">
  <source src="05_ota_v110.mp4" type="video/mp4">
</video>
<p><b>Video: actualización OTA a la v1.1.0</b></p>
@endhtmlonly

@section paso3 3. OTA a la versión 1.2.0 (con IA)

Se publica la v1.2.0. El servidor registra la descarga (`GET /fw/firmware_1.2.0.bin`) y el ESP32
queda con rojo y amarillo fijos. A partir de aquí la telemetría llega con `fw: 1.2.0`.

@image html 04_publicar_v120.png "Publicación de la v1.2.0 y descarga del .bin por el ESP32"

@htmlonly
<video controls loop muted style="max-width:100%;width:800px">
  <source src="06_ota_v120.mp4" type="video/mp4">
</video>
<p><b>Video: actualización OTA a la v1.2.0</b></p>
@endhtmlonly

@section paso4 4. Detección de anomalías con regresión logística

Con la v1.2.0 corriendo, se esperan 2.5 minutos sin tocar el cable (calibración de 30 muestras).
Luego se toca el cable: el tacto baja, la probabilidad `p` sube a 1.00, el LED amarillo parpadea
rápido, el equipo muestrea cada 1 s y el servidor registra `anomalia: True`. Al soltarlo, vuelve a
`anom=0` y a los LEDs fijos.

@htmlonly
<video controls loop muted style="max-width:100%;width:800px">
  <source src="07_anomalia.mp4" type="video/mp4">
</video>
<p><b>Video: anomalía al tocar el cable (anom=1, p=1.00)</b></p>
@endhtmlonly

@section paso5 5. Pruebas adicionales

Estas pruebas se describen en @ref ota y @ref rollback. Para documentarlas, copia la captura a
la carpeta `imagenes/` y agrega una línea como esta al final de este archivo:

@verbatim
@image html 08_rollback.png "ROLLBACK y regreso a FW 1.2.0"
@endverbatim

- **Sensor caído:** cable a GND durante más de 6 s, rojo parpadeando rápido.
- **Hash inválido:** monitor muestra `HASH INVALIDO` y sigue en la versión actual.
- **Corte de red:** a los 10 s sin datos, `OTA incompleta`.
- **Rollback:** `Arranque sin validar 1/3, 2/3, 3/3`, `ROLLBACK` y luego `FW 1.2.0`.
