from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIR = ROOT / "assets" / "ui-icons" / "source"
PREVIEW_DIR = ROOT / "assets" / "ui-icons" / "72px"
HEADER_PATH = ROOT / "include" / "CarouselIconAssets.h"
SOURCE_PATH = ROOT / "src" / "CarouselIconAssets.cpp"

ICON_SIZE = 72
CONTENT_SIZE = 68
PANEL_RGB = (8, 16, 24)
TRANSPARENT_KEY = 0x0001

ICONS = (
    ("WIFI", "wifi-router.png"),
    ("RADIO", "radio-rf.png"),
    ("BLUETOOTH", "bluetooth.png"),
    ("MONITOR", "packet-monitor.png"),
    ("SYSTEM", "system-control.png"),
)


def prepare_icon(path: Path) -> Image.Image:
    image = Image.open(path).convert("RGBA")
    alpha = image.getchannel("A")
    bbox = alpha.getbbox()
    if bbox:
        image = image.crop(bbox)

    image.thumbnail((CONTENT_SIZE, CONTENT_SIZE), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (ICON_SIZE, ICON_SIZE), (0, 0, 0, 0))
    x = (ICON_SIZE - image.width) // 2
    y = (ICON_SIZE - image.height) // 2
    canvas.alpha_composite(image, (x, y))
    return canvas


def rgb565(red: int, green: int, blue: int) -> int:
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def pixel_values(image: Image.Image) -> list[int]:
    values: list[int] = []
    for red, green, blue, alpha in image.getdata():
        if alpha < 12:
            values.append(TRANSPARENT_KEY)
            continue

        mix = alpha / 255.0
        red = round(red * mix + PANEL_RGB[0] * (1.0 - mix))
        green = round(green * mix + PANEL_RGB[1] * (1.0 - mix))
        blue = round(blue * mix + PANEL_RGB[2] * (1.0 - mix))
        value = rgb565(red, green, blue)
        values.append(0x0000 if value == TRANSPARENT_KEY else value)
    return values


def format_array(name: str, values: list[int]) -> str:
    rows = []
    for offset in range(0, len(values), 12):
        chunk = values[offset : offset + 12]
        rows.append("    " + ", ".join(f"0x{value:04X}" for value in chunk) + ",")
    return (
        f"const uint16_t CAROUSEL_ICON_{name}[CAROUSEL_ICON_PIXELS] PROGMEM = {{\n"
        + "\n".join(rows)
        + "\n};\n"
    )


def main() -> None:
    PREVIEW_DIR.mkdir(parents=True, exist_ok=True)
    arrays = []

    for name, filename in ICONS:
        icon = prepare_icon(SOURCE_DIR / filename)
        icon.save(PREVIEW_DIR / filename)
        arrays.append(format_array(name, pixel_values(icon)))

    header = """#ifndef CAROUSEL_ICON_ASSETS_H
#define CAROUSEL_ICON_ASSETS_H

#include <Arduino.h>

static constexpr int CAROUSEL_ICON_SIZE = 72;
static constexpr int CAROUSEL_ICON_PIXELS = CAROUSEL_ICON_SIZE * CAROUSEL_ICON_SIZE;
static constexpr uint16_t CAROUSEL_ICON_TRANSPARENT = 0x0001;

extern const uint16_t CAROUSEL_ICON_WIFI[CAROUSEL_ICON_PIXELS] PROGMEM;
extern const uint16_t CAROUSEL_ICON_RADIO[CAROUSEL_ICON_PIXELS] PROGMEM;
extern const uint16_t CAROUSEL_ICON_BLUETOOTH[CAROUSEL_ICON_PIXELS] PROGMEM;
extern const uint16_t CAROUSEL_ICON_MONITOR[CAROUSEL_ICON_PIXELS] PROGMEM;
extern const uint16_t CAROUSEL_ICON_SYSTEM[CAROUSEL_ICON_PIXELS] PROGMEM;

const uint16_t* getCarouselIcon(uint8_t index);

#endif
"""
    HEADER_PATH.write_text(header, encoding="utf-8", newline="\n")

    source = """#include "CarouselIconAssets.h"

""" + "\n".join(arrays) + """
const uint16_t* getCarouselIcon(uint8_t index) {
    switch (index) {
        case 0: return CAROUSEL_ICON_WIFI;
        case 1: return CAROUSEL_ICON_RADIO;
        case 2: return CAROUSEL_ICON_BLUETOOTH;
        case 3: return CAROUSEL_ICON_MONITOR;
        case 4: return CAROUSEL_ICON_SYSTEM;
        default: return CAROUSEL_ICON_WIFI;
    }
}
"""
    SOURCE_PATH.write_text(source, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
