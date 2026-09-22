<a id="inicio"></a>

# CYBERDECK MINI ESP32 V2

<p align="center">
  <strong>Cyberdeck portátil para ESP32-S3 con interfaz gráfica renovada, WiFi, Bluetooth LE, radio 2.4 GHz, GPS y microSD.</strong>
</p>

<p align="center">
  <a href="https://pepeangell5.github.io/CYBERDECK-MINI-ESP32-V2/"><img alt="Web Flasher" src="https://img.shields.io/badge/WEB_FLASHER-INSTALAR-1dff8f?style=for-the-badge&logo=espressif&logoColor=001008"></a>
  <a href="https://github.com/pepeangell5/CYBERDECK-MINI-ESP32-V2"><img alt="Repositorio V2" src="https://img.shields.io/badge/GITHUB-V2-181717?style=for-the-badge&logo=github&logoColor=white"></a>
  <a href="https://instagram.com/esp32_tools"><img alt="Instagram" src="https://img.shields.io/badge/INSTAGRAM-ESP32__TOOLS-E4405F?style=for-the-badge&logo=instagram&logoColor=white"></a>
</p>

![Menú principal de CYBERDECK MINI ESP32 V2](img/menu_principal.JPG)

> [!IMPORTANT]
> Utiliza este firmware únicamente con redes, dispositivos y laboratorios propios o con autorización expresa. Las herramientas inalámbricas están destinadas a aprendizaje, diagnóstico, investigación y auditorías autorizadas.

<a id="menu"></a>

## Menú

- [Descripción](#descripción)
- [Novedades de la V2](#novedades-de-la-v2)
- [Galería](#galería)
- [Hardware objetivo](#hardware-objetivo)
- [Pinout](#pinout)
- [Controles y navegación](#controles-y-navegación)
- [Funciones del firmware](#funciones-del-firmware)
- [GPS, wardriving y mapa](#gps-wardriving-y-mapa)
- [Archivos generados en microSD](#archivos-generados-en-microsd)
- [Instalación con Web Flasher](#instalación-con-web-flasher)
- [Flasheo manual](#flasheo-manual)
- [Compilar desde el código](#compilar-desde-el-código)
- [Estructura del proyecto](#estructura-del-proyecto)
- [Uso responsable](#uso-responsable)
- [Créditos y redes](#créditos-y-redes)
- [Licencia](#licencia)

## Descripción

CYBERDECK MINI ESP32 V2 convierte un ESP32-S3 en una consola portátil de análisis inalámbrico. Integra pantalla TFT ST7789 de 240 × 320, dos módulos nRF24L01, GPS NEO-6M, microSD, encoder, botones físicos, buzzer y medición de batería.

La V2 conserva las funciones del firmware original y añade una identidad visual propia: splash con ajolote, menú principal tipo carrusel, colores independientes por categoría, tarjetas más legibles y redibujado optimizado para reducir parpadeos.

[⬆️ Regresar al menú](#menu)

## Novedades de la V2

- Interfaz inspirada y prototipada en Lopaka, adaptada al hardware real.
- Menú principal tipo carrusel con iconos gráficos independientes.
- Submenús de tres tarjetas grandes, color específico por categoría e icono por función.
- Splash renovado con el ajolote de ESP32 Tools.
- Mejoras de fluidez mediante actualización parcial de regiones dinámicas.
- Pantallas completas y consistentes en WiFi, Radio/RF, Bluetooth, Monitor y System.
- Flujos GPS y microSD ampliados, incluido registro de wardriving en CSV.
- Script incluido para convertir `WARD_DRIVE.csv` en un mapa interactivo.
- Web Flasher y binarios completos específicos para la V2.

[⬆️ Regresar al menú](#menu)

## Galería

| Menú principal | WiFi |
| --- | --- |
| ![Menú principal](img/menu_principal.JPG) | ![Herramientas WiFi](img/wifi_tool.JPG) |

| WiFi: segunda página | Radio / RF |
| --- | --- |
| ![Segunda página WiFi](img/wifi_tool_2.JPG) | ![Herramientas de radio](img/radio_tool.JPG) |

| Bluetooth | Monitor de paquetes |
| --- | --- |
| ![Herramientas Bluetooth](img/bluetooth.JPG) | ![Monitor de paquetes](img/pkt_monitor.JPG) |

| System | Herramientas de sistema |
| --- | --- |
| ![Menú System](img/system.JPG) | ![Herramientas de sistema](img/system2.JPG) |

| Threat Monitor |
| --- |
| ![Threat Monitor](img/threat_mon.JPG) |

[⬆️ Regresar al menú](#menu)

## Hardware objetivo

- ESP32-S3 DevKitC-1 N8 o compatible, con 8 MB de flash.
- Pantalla TFT ST7789 SPI de 240 × 320.
- Dos módulos nRF24L01.
- Receptor GPS NEO-6M conectado a UART1.
- Lector microSD en un bus SPI dedicado.
- Encoder rotativo con pulsador.
- Cuatro botones: UP, DOWN, ENTER y BACK.
- Buzzer.
- Lectura ADC de batería mediante divisor resistivo de 2.2 kΩ / 1 kΩ.

[⬆️ Regresar al menú](#menu)

## Pinout

| Periférico | Función | GPIO |
| --- | --- | ---: |
| TFT ST7789 | SCK | 12 |
| TFT ST7789 | MOSI | 11 |
| TFT ST7789 | MISO | 13 |
| TFT ST7789 | CS | 10 |
| TFT ST7789 | DC | 21 |
| TFT ST7789 | RST | 14 |
| nRF24 #1 | CE / CSN | 4 / 5 |
| nRF24 #2 | CE / CSN | 6 / 7 |
| microSD | SCK / MOSI / MISO / CS | 36 / 35 / 37 / 16 |
| GPS NEO-6M | RX / TX | 18 / 17 |
| Botones | UP / DOWN / ENTER / BACK | 1 / 2 / 42 / 41 |
| Encoder | CLK / DT / SW | 40 / 39 / 38 |
| Buzzer | Señal | 15 |
| Batería | ADC | 9 |

[⬆️ Regresar al menú](#menu)

## Controles y navegación

| Control | Acción |
| --- | --- |
| UP / DOWN | Mover la selección o cambiar un valor |
| Encoder | Navegar en menús y herramientas compatibles |
| ENTER / OK | Abrir, confirmar o ejecutar |
| BACK | Regresar o cancelar |
| Pulsación larga | Salida rápida o acción secundaria cuando la pantalla lo indica |

La interfaz evita repintar toda la pantalla en cada cambio siempre que es posible, lo que reduce parpadeos y mejora la respuesta visual.

[⬆️ Regresar al menú](#menu)

## Funciones del firmware

### WiFi

- WiFi Scanner con SSID, BSSID, canal, RSSI, seguridad y vista de detalles.
- Threat Monitor para observar eventos y actividad anómala.
- WiFi Audit con análisis de redes visibles.
- Beacon Spam para pruebas controladas.
- Deauther para laboratorios autorizados.
- Evil Portal con captura local para demostraciones controladas.
- Probe Sniffer para observación pasiva de solicitudes probe.
- KARMA Attack para entornos de investigación autorizados.

### Radio / RF

- Jammer de canal para pruebas de laboratorio con doble nRF24L01.
- Spectrum con vistas Spectrum, Waterfall y WiFi Channels.
- RF Baseline para comparar actividad actual contra una línea base.
- Diagnóstico de módulos nRF24L01.

### Bluetooth

- BLE Defense con tarjetas de dispositivos y detalles.
- BLE Scanner.
- BLE Spam con múltiples perfiles publicitarios de prueba.
- BT Disruptor para laboratorio.
- BT Jammer con animación desacoplada del procesamiento de radio.

### Monitor

- Packet Monitor por canales WiFi.
- Indicadores visuales de actividad.
- Aviso acústico mediante buzzer cuando se detecta actividad elevada.

### System

- Mission Dashboard y Audit Reports.
- Settings y System Info.
- Herramientas GPS y consola NMEA.
- MicroSD Manager y MicroSD Info.
- Battery ADC.
- Clock & Weather con teclado en pantalla.
- About con identidad de ESP32 Tools.

[⬆️ Regresar al menú](#menu)

## GPS, wardriving y mapa

GPS Tools incluye panel de estado, logger de trayectoria, waypoints, brújula por movimiento, snapshot, consola NMEA y modo wardriving. Este último registra en `/WARD_DRIVE.csv` las coordenadas y redes detectadas.

El repositorio incluye la carpeta [`generar mapa wardriving script`](generar%20mapa%20wardriving%20script/README.md), con un programa en Python que genera un mapa HTML interactivo:

```powershell
python ".\generar mapa wardriving script\wardrive_map.py" "D:\WARD_DRIVE.csv"
```

El resultado se guarda junto al CSV como `WARD_DRIVE_MAP.html` y se abre automáticamente en el navegador. No requiere paquetes de Python adicionales; para cargar el mapa base sí necesita conexión a Internet.

> [!CAUTION]
> Un archivo de wardriving puede contener ubicación y datos de redes cercanas. Revisa y anonimiza el CSV antes de compartirlo. Ningún registro personal de prueba está incluido en este repositorio.

[⬆️ Regresar al menú](#menu)

## Archivos generados en microSD

| Archivo | Origen |
| --- | --- |
| `/WARD_DRIVE.csv` | GPS Wardrive |
| `/GPS_TRACK.csv` | GPS Track Logger |
| `/GPS_MARKS.csv` | Dashboard / Waypoint Mark |
| `/GPS_SNAPSHOT.txt` | Export Snapshot |
| `/THREAT_REPORT.txt` | Threat Monitor |
| `/WIFI_DEFENSE.txt` | WiFi Audit |
| `/WIFI_AUDIT.csv` | Audit Reports |
| `/RF_BASELINE.txt` | RF Baseline |
| `/BLE_AUDIT.txt` | BLE Defense |
| `/BATTERY_STATUS.txt` | Battery report |
| `/SD_INDEX.txt` | MicroSD Manager |

[⬆️ Regresar al menú](#menu)

## Instalación con Web Flasher

La forma más rápida de instalar la V2 es el Web Flasher:

### [Abrir CYBERDECK MINI ESP32 V2 Web Flasher](https://pepeangell5.github.io/CYBERDECK-MINI-ESP32-V2/)

1. Abre la página en Chrome, Edge u Opera de escritorio.
2. Conecta el ESP32-S3 mediante un cable USB de datos.
3. Pulsa **INSTALAR FIRMWARE** y selecciona el puerto serial correcto.
4. Si no inicia, mantén presionado BOOT al comenzar la instalación.
5. Reinicia la placa cuando termine.

El instalador usa el binario fusionado y escribe la imagen completa desde `0x0`.

[⬆️ Regresar al menú](#menu)

## Flasheo manual

Los archivos finales están en [`archivos bin`](archivos%20bin/):

| Modo | Archivo | Offset |
| --- | --- | ---: |
| Imagen completa | `CYBERDECK-MINI-ESP32-V2-firmware-merged.bin` | `0x0` |
| Bootloader | `bootloader.bin` | `0x0` |
| Tabla de particiones | `partitions.bin` | `0x8000` |
| Boot app | `boot_app0.bin` | `0xE000` |
| Aplicación | `firmware.bin` | `0x10000` |

Consulta también [`FLASH_OFFSETS.txt`](archivos%20bin/FLASH_OFFSETS.txt).

[⬆️ Regresar al menú](#menu)

## Compilar desde el código

Requisitos: Visual Studio Code con PlatformIO o PlatformIO Core. El proyecto descarga sus dependencias desde `platformio.ini`.

```powershell
pio run
```

Para compilar y cargar directamente:

```powershell
pio run -t upload
```

Configuración principal: `esp32-s3-devkitc-1`, framework Arduino, 8 MB de flash y partición `huge_app.csv`.

[⬆️ Regresar al menú](#menu)

## Estructura del proyecto

```text
.github/workflows/               Publicación automática del Web Flasher
archivos bin/                    Binarios finales y tabla de offsets
assets/                          Recursos gráficos y copia de firmware
firmware/                        Imagen fusionada compatible
generar mapa wardriving script/  Generador de mapa interactivo
img/                             Fotografías del dispositivo
include/                         Cabeceras y recursos compilados
src/                             Código fuente del firmware
tools/                           Generadores de recursos gráficos
index.html                       Web Flasher
manifest.json                    Manifiesto de ESP Web Tools
platformio.ini                   Configuración de compilación
```

[⬆️ Regresar al menú](#menu)

## Uso responsable

Este proyecto se publica con fines educativos y defensivos. El usuario es responsable de cumplir la legislación local y de contar con autorización antes de transmitir, interferir, capturar o auditar señales y dispositivos. No lo uses contra redes, equipos o personas sin permiso.

[⬆️ Regresar al menú](#menu)

## Créditos y redes

- Proyecto y adaptación: [PepeAngell](https://github.com/pepeangell5)
- Instagram: [@esp32_tools](https://instagram.com/esp32_tools)
- Facebook: [ESP32 Tools](https://www.facebook.com/esp32tools/)
- Repositorio: [CYBERDECK-MINI-ESP32-V2](https://github.com/pepeangell5/CYBERDECK-MINI-ESP32-V2)

[⬆️ Regresar al menú](#menu)

## Licencia

Consulta el archivo [`LICENSE`](LICENSE) antes de redistribuir o modificar el proyecto.

[⬆️ Regresar al menú](#menu)
