<a id="menu"></a>

# Generar mapa de wardriving

Esta utilidad convierte el archivo `WARD_DRIVE.csv` creado por **CYBERDECK MINI ESP32 V2** en un mapa HTML interactivo. Dibuja la ruta GPS, agrupa las redes por posición y muestra SSID, BSSID, RSSI, canal y autenticación en cada punto.

## Menú

- [Requisitos](#requisitos)
- [Uso rápido](#uso-rápido)
- [Opciones](#opciones)
- [Formato del CSV](#formato-del-csv)
- [Privacidad](#privacidad)
- [Solución de problemas](#solución-de-problemas)

## Requisitos

- Python 3.10 o posterior.
- Navegador moderno.
- Conexión a Internet al abrir el HTML para cargar Leaflet y el mapa base de Esri.

No hace falta instalar paquetes con `pip`: el script utiliza únicamente la biblioteca estándar de Python.

[⬆️ Regresar al menú](#menu)

## Uso rápido

1. Retira correctamente la microSD del cyberdeck.
2. Abre `/wardriving` en la microSD y copia el CSV de la sesión a tu computadora.
3. Desde la raíz del repositorio ejecuta:

```powershell
python ".\generar mapa wardriving script\wardrive_map.py" "D:\WD_23-09-2026_05-08-45.csv"
```

Se creará `WARD_DRIVE_MAP.html` en la misma carpeta del CSV y se abrirá automáticamente.

[⬆️ Regresar al menú](#menu)

## Opciones

Elegir otro archivo de salida:

```powershell
python wardrive_map.py WARD_DRIVE.csv -o mi_ruta.html
```

Generar el mapa sin abrir el navegador:

```powershell
python wardrive_map.py WARD_DRIVE.csv --no-open
```

[⬆️ Regresar al menú](#menu)

## Formato del CSV

El firmware escribe estas columnas:

```text
UTC,Millis,Lat,Lng,Sat,HDOP,SSID,BSSID,RSSI,Channel,Auth
```

Las filas sin coordenadas GPS válidas se contabilizan, pero no se dibujan. Las redes repetidas se conservan en los puntos donde fueron observadas y se deduplican en el resumen general mediante BSSID.

[⬆️ Regresar al menú](#menu)

## Privacidad

`WARD_DRIVE.csv` puede revelar tu recorrido y datos de redes próximas. No publiques el CSV ni el HTML generado sin revisarlos y anonimizar la información sensible. Este repositorio no incluye capturas reales.

[⬆️ Regresar al menú](#menu)

## Solución de problemas

- **El mapa está vacío:** confirma que `Lat` y `Lng` contienen coordenadas válidas distintas de cero.
- **No aparece el mapa base:** revisa la conexión a Internet o bloqueadores del navegador.
- **Caracteres extraños en SSID:** el script intenta UTF-8 y después Windows-1252; algunos nombres pueden contener bytes no imprimibles.
- **Python no se reconoce:** instala Python 3 y activa la opción para agregarlo al `PATH`.

[⬆️ Regresar al menú](#menu)
