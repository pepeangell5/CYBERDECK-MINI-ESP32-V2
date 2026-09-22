#!/usr/bin/env python3
"""Convierte WARD_DRIVE.csv de CYBERDECK MINI ESP32 V2 en un mapa HTML."""

from __future__ import annotations

import argparse
import csv
import html
import json
import math
import sys
import webbrowser
from collections import defaultdict
from pathlib import Path


REQUIRED_COLUMNS = {"Lat", "Lng", "SSID", "BSSID", "RSSI", "Channel", "Auth"}


def read_csv(path: Path) -> list[dict[str, str]]:
    last_error: Exception | None = None
    for encoding in ("utf-8-sig", "cp1252", "latin-1"):
        try:
            with path.open("r", encoding=encoding, newline="") as handle:
                reader = csv.DictReader(handle)
                columns = set(reader.fieldnames or [])
                missing = REQUIRED_COLUMNS - columns
                if missing:
                    raise ValueError("Faltan columnas: " + ", ".join(sorted(missing)))
                return [
                    {key: (value or "").strip() for key, value in row.items() if key}
                    for row in reader
                ]
        except UnicodeDecodeError as exc:
            last_error = exc
    if last_error:
        raise last_error
    return []


def as_float(value: str) -> float | None:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) else None


def valid_position(row: dict[str, str]) -> tuple[float, float] | None:
    lat = as_float(row.get("Lat", ""))
    lng = as_float(row.get("Lng", ""))
    if lat is None or lng is None or not (-90 <= lat <= 90) or not (-180 <= lng <= 180):
        return None
    if lat == 0 and lng == 0:
        return None
    return lat, lng


def escape(value: str) -> str:
    return html.escape(value or "—", quote=True)


def build_map(rows: list[dict[str, str]], source: Path) -> str:
    located = [(row, pos) for row in rows if (pos := valid_position(row))]
    if not located:
        raise ValueError("El CSV no contiene coordenadas GPS válidas.")

    groups: dict[tuple[float, float], list[dict[str, str]]] = defaultdict(list)
    route: list[list[float]] = []
    previous: tuple[float, float] | None = None
    for row, pos in located:
        groups[pos].append(row)
        if pos != previous:
            route.append([pos[0], pos[1]])
            previous = pos

    unique_bssids = {row.get("BSSID", "").upper() for row, _ in located if row.get("BSSID")}
    open_aps = {
        row.get("BSSID", "").upper()
        for row, _ in located
        if row.get("BSSID") and row.get("Auth", "").upper() in {"OPEN", "NONE", "0"}
    }

    markers = []
    for (lat, lng), observations in groups.items():
        sorted_rows = sorted(
            observations,
            key=lambda row: as_float(row.get("RSSI", "")) or -999,
            reverse=True,
        )
        cards = []
        seen: set[tuple[str, str]] = set()
        for row in sorted_rows:
            identity = (row.get("BSSID", "").upper(), row.get("SSID", ""))
            if identity in seen:
                continue
            seen.add(identity)
            cards.append(
                "<div class='ap'>"
                f"<strong>{escape(row.get('SSID', '') or '[SSID oculto]')}</strong>"
                f"<span>BSSID: {escape(row.get('BSSID', ''))}</span>"
                f"<span>RSSI: {escape(row.get('RSSI', ''))} dBm · Canal: {escape(row.get('Channel', ''))}</span>"
                f"<span>Seguridad: {escape(row.get('Auth', ''))}</span>"
                "</div>"
            )
        markers.append({
            "lat": lat,
            "lng": lng,
            "count": len(seen),
            "popup": f"<div class='popup'><h3>{len(seen)} red(es)</h3>" + "".join(cards) + "</div>",
        })

    center_lat = sum(point[0] for point in route) / len(route)
    center_lng = sum(point[1] for point in route) / len(route)
    title = f"Ruta de wardriving · {source.name}"
    payload = {
        "center": [center_lat, center_lng],
        "route": route,
        "markers": markers,
        "stats": {"records": len(rows), "gps": len(route), "aps": len(unique_bssids), "open": len(open_aps)},
    }

    return f"""<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{escape(title)}</title>
  <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/leaflet@1.9.4/dist/leaflet.css">
  <style>
    html, body, #map {{ height: 100%; margin: 0; }}
    body {{ background: #060914; color: #f5f7ff; font-family: Inter, Segoe UI, sans-serif; }}
    #map {{ background: #0a1020; }}
    .panel {{ position: fixed; z-index: 1000; top: 18px; right: 18px; width: 230px; padding: 16px;
      border: 1px solid #28d7ec; border-radius: 14px; background: rgba(7, 12, 27, .92);
      box-shadow: 0 16px 45px rgba(0, 0, 0, .42); backdrop-filter: blur(10px); }}
    .panel h1 {{ color: #28d7ec; font-size: 17px; margin: 0 0 10px; }}
    .panel p {{ display: flex; justify-content: space-between; gap: 12px; margin: 6px 0; font-size: 13px; }}
    .panel strong {{ color: #ff70c7; }}
    .panel small {{ color: #9da8c2; display: block; margin-top: 10px; overflow-wrap: anywhere; }}
    .popup {{ max-height: 310px; min-width: 230px; overflow-y: auto; }}
    .popup h3 {{ margin: 0 0 8px; color: #18223a; }}
    .ap {{ border-top: 1px solid #d8ddea; padding: 7px 0; }}
    .ap strong, .ap span {{ display: block; overflow-wrap: anywhere; }}
    .ap span {{ color: #46516a; font-size: 12px; margin-top: 2px; }}
    @media (max-width: 620px) {{ .panel {{ top: 8px; right: 8px; left: 8px; width: auto; }} }}
  </style>
</head>
<body>
  <div id="map"></div>
  <aside class="panel">
    <h1>Ruta de wardriving</h1>
    <p><span>Registros</span><strong id="records"></strong></p>
    <p><span>Puntos GPS</span><strong id="gps"></strong></p>
    <p><span>AP únicos</span><strong id="aps"></strong></p>
    <p><span>AP abiertos</span><strong id="open"></strong></p>
    <small>{escape(source.name)}</small>
  </aside>
  <script src="https://cdn.jsdelivr.net/npm/leaflet@1.9.4/dist/leaflet.js"></script>
  <script>
    const data = {json.dumps(payload, ensure_ascii=False)};
    const map = L.map('map', {{ preferCanvas: true }}).setView(data.center, 15);
    L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Street_Map/MapServer/tile/{{z}}/{{y}}/{{x}}', {{
      maxZoom: 19,
      attribution: 'Tiles &copy; Esri'
    }}).addTo(map);
    const line = L.polyline(data.route, {{ color: '#28d7ec', weight: 5, opacity: .9 }}).addTo(map);
    data.markers.forEach((point, index) => {{
      L.circleMarker([point.lat, point.lng], {{
        radius: Math.min(12, 5 + Math.sqrt(point.count)),
        color: index === 0 ? '#29ef9f' : '#101426',
        weight: 2,
        fillColor: index === data.markers.length - 1 ? '#ff547d' : '#ff70c7',
        fillOpacity: .95
      }}).bindPopup(point.popup, {{ maxWidth: 340 }}).addTo(map);
    }});
    if (data.route.length > 1) map.fitBounds(line.getBounds(), {{ padding: [45, 45] }});
    for (const [key, value] of Object.entries(data.stats)) document.getElementById(key).textContent = value;
  </script>
</body>
</html>
"""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Genera un mapa interactivo a partir de WARD_DRIVE.csv.")
    parser.add_argument("csv", type=Path, help="Ruta al archivo WARD_DRIVE.csv")
    parser.add_argument("-o", "--output", type=Path, help="Ruta del HTML de salida")
    parser.add_argument("--no-open", action="store_true", help="No abrir el navegador")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source = args.csv.expanduser().resolve()
    if not source.is_file():
        print(f"Error: no existe el archivo {source}", file=sys.stderr)
        return 2
    output = (args.output or source.with_name("WARD_DRIVE_MAP.html")).expanduser().resolve()
    try:
        rows = read_csv(source)
        output.write_text(build_map(rows, source), encoding="utf-8")
    except (OSError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    print(f"Mapa creado: {output}")
    if not args.no_open:
        webbrowser.open(output.as_uri())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
