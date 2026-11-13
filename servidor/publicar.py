"""
@file publicar.py
@brief Publica un firmware: copia el .bin a fw/ y escribe fw/manifest.json con su SHA-256.

Uso (desde la carpeta servidor/):
    python publicar.py <version> <ruta_al_bin>
Ejemplo:
    python publicar.py 1.2.0 ..\\.pio\\build\\v120\\firmware.bin

@note Cambia IP cada vez que cambie la IP del PC (debe ser igual a SECRET_SERVER).
"""
import sys, shutil, hashlib, json

IP = "192.168.53.86"   # IP del PC en el hotspot (la misma de SECRET_SERVER en secrets.h)

if len(sys.argv) != 3:
    sys.exit("Uso: python publicar.py <version> <ruta_al_bin>")

ver, src = sys.argv[1], sys.argv[2]
dst = f"fw/firmware_{ver}.bin"
shutil.copy(src, dst)
h = hashlib.sha256(open(dst, "rb").read()).hexdigest()   # huella digital del archivo
json.dump({"version": ver, "url": f"http://{IP}:8000/{dst}", "sha256": h},
          open("fw/manifest.json", "w"))
print("Publicada", ver, h)
