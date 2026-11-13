@page rollback Rollback por software

Un OTA puede instalar correctamente un firmware que luego **no funciona** (por ejemplo, se
reinicia en bucle). El rollback es el mecanismo que devuelve el equipo a la versión anterior.

## Idea

Después de un OTA, el firmware nuevo está "a prueba". Se cuentan sus arranques; si llega a
**3 arranques sin validarse**, se marca como mala y se vuelve a la otra partición.

## Datos que se guardan (memoria NVS)

La memoria no volátil NVS conserva los datos aunque el equipo se reinicie. Se usa el espacio
de nombres `fw` con tres claves:

| Clave | Significado |
|---|---|
| `pending` | `true` si el firmware actual es nuevo y todavía no se validó. |
| `tries` | Cuántos arranques lleva sin validarse. |
| `bad` | Versión que fue rechazada por el rollback (para no volver a instalarla). |

## Qué hace el código

1. **Después del OTA** (`checkOTA`): `pending = true`, `tries = 0` y reinicio.
2. **En cada arranque** (`bootCheck`, dentro de `setup()`): si `pending` es `true`, suma 1 a
   `tries` y muestra `Arranque sin validar N/3`.
3. **Si `tries` llega a 3**: imprime `ROLLBACK`, guarda la versión en `bad`, pone
   `pending = false` y cambia la partición de arranque a la anterior (`esp_ota_set_boot_partition`),
   seguido de un reinicio.
4. **Si el firmware funciona** (`marcarValida`): cuando conecta al Wi-Fi y toma su primera
   muestra correcta, pone `pending = false` y `tries = 0`. El firmware queda validado.
5. **Después del rollback**, `checkOTA` ignora la versión guardada en `bad`, así el equipo no
   vuelve a descargar la versión mala una y otra vez.

## La versión 1.3.0: una versión mala a propósito

El entorno `v130` se compila con `-DSIMULAR_FALLO`. Con esa bandera:

- el código ejecuta `ESP.restart()` a los 8 segundos de arrancar, y
- **nunca** llama a `marcarValida()`.

Resultado: la versión 1.3.0 se instala bien (el hash es correcto), pero se reinicia sola tres
veces sin validarse.

## Secuencia esperada en el monitor

```
Actualizando a 1.3.0              (descarga e instalación)
FW 1.3.0
Arranque sin validar 1/3
FW 1.3.0
Arranque sin validar 2/3
FW 1.3.0
Arranque sin validar 3/3
ROLLBACK: se vuelve a la version anterior
FW 1.2.0
```

## Limitación

Este rollback es **por software**: solo cubre fallos que ocurren cuando el programa ya está
corriendo (desde `setup()`). Si el firmware nuevo se cuelga antes, en el bootloader, no se
detecta. Para eso haría falta activar `CONFIG_APP_ROLLBACK_ENABLE` en ESP-IDF.
