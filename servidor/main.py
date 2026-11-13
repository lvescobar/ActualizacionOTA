"""
@file main.py
@brief Servidor FastAPI: entrega la version OTA y guarda la telemetria del ESP32.

Ejecutar desde la carpeta servidor/:
    python -m uvicorn main:app --host 0.0.0.0 --port 8000
"""
import json, csv, time
from pathlib import Path
from fastapi import FastAPI, Request, HTTPException
from fastapi.staticfiles import StaticFiles

app = FastAPI()
Path("fw").mkdir(exist_ok=True)

CSV = Path("telemetria.csv")   # archivo donde se guarda la telemetria
if not CSV.exists():
    CSV.write_text("timestamp,fw,temp,tacto,anomalia,rssi\n")


@app.get("/version.json")
def version():
    """
    @brief Devuelve el manifest con la ultima version publicada.
    @return Diccionario con "version", "url" del .bin y "sha256".
    @exception HTTPException 404 si todavia no se ha publicado ninguna version.
    """
    m = Path("fw/manifest.json")
    if not m.exists():
        raise HTTPException(404, "Aún no hay versión publicada")
    return json.loads(m.read_text())


@app.post("/telemetry")
async def telemetry(req: Request):
    """
    @brief Recibe una muestra del ESP32 y la guarda en telemetria.csv.
    @param req Peticion con JSON: fw, temp, luz (valor tactil), anomalia, rssi.
    @return {"ok": True}
    """
    d = await req.json()
    with open(CSV, "a", newline="") as f:
        # el ESP32 envía el valor táctil en el campo "luz"
        csv.writer(f).writerow([time.time(), d["fw"], d["temp"], d["luz"], d["anomalia"], d["rssi"]])
    print(d)
    return {"ok": True}


app.mount("/fw", StaticFiles(directory="fw"), name="fw")
