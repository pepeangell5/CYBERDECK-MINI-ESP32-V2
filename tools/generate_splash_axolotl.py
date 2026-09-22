from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = ROOT / "assets" / "splash" / "source" / "axolotl-sunglasses.png"
PREVIEW_PATH = ROOT / "assets" / "splash" / "preview" / "axolotl-sunglasses-96x80.png"
LOPAKA_PREVIEW_PATH = ROOT / "assets" / "splash" / "preview" / "cyberdeck-splash-320x240.png"
HEADER_PATH = ROOT / "include" / "SplashAxolotlAsset.h"
CPP_PATH = ROOT / "src" / "SplashAxolotlAsset.cpp"

WIDTH = 96
HEIGHT = 80
CONTENT_HEIGHT = 78
TRANSPARENT_KEY = 0x0001


def load_font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    font_paths = (
        Path("C:/Windows/Fonts/consola.ttf"),
        Path("C:/Windows/Fonts/lucon.ttf"),
    )
    for font_path in font_paths:
        if font_path.exists():
            return ImageFont.truetype(font_path, size)
    return ImageFont.load_default()


def draw_centered(draw: ImageDraw.ImageDraw, text: str, y: int,
                  font: ImageFont.ImageFont, fill: tuple[int, int, int]) -> None:
    box = draw.textbbox((0, 0), text, font=font)
    width = box[2] - box[0]
    draw.text(((320 - width) // 2, y), text, font=font, fill=fill)


def create_lopaka_preview(axolotl: Image.Image) -> None:
    canvas = Image.new("RGB", (320, 240), (0, 0, 0))
    draw = ImageDraw.Draw(canvas)
    green = (0, 255, 80)
    dim_green = (0, 100, 38)
    cyan = (0, 230, 255)
    muted = (135, 135, 135)
    yellow = (255, 235, 0)

    draw.rectangle((0, 0, 319, 239), outline=green)
    draw.rectangle((2, 2, 317, 237), outline=dim_green)
    canvas.paste(axolotl, (112, 18), axolotl)

    title_font = load_font(20)
    small_font = load_font(10)
    draw_centered(draw, "CYBERDECK", 103, title_font, (255, 255, 255))
    draw.rectangle((70, 128, 249, 135), outline=green)
    draw.rectangle((72, 130, 247, 133), fill=green)
    draw_centered(draw, "// ESP32-TOOLS PRO", 140, small_font, muted)

    log_lines = (
        ("[OK] ", "ESP32-S3 @ 240 MHz", green),
        ("[OK] ", "TFT ST7789 240x320", green),
        ("[OK] ", "GPS UART1 RX18/TX17", green),
        ("[OK] ", "SD SPI CS10", yellow),
    )
    y = 164
    for prefix, text, color in log_lines:
        draw.text((8, y), prefix, font=small_font, fill=muted)
        prefix_width = draw.textbbox((0, 0), prefix, font=small_font)[2]
        draw.text((8 + prefix_width, y), text, font=small_font, fill=color)
        y += 12

    draw.text((8, 216), "BOOT #1", font=small_font, fill=muted)
    draw_centered(draw, "PRESS ANY KEY", 226, small_font, cyan)
    canvas.save(LOPAKA_PREVIEW_PATH)


def rgb565(red: int, green: int, blue: int) -> int:
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def prepare_image() -> Image.Image:
    image = Image.open(SOURCE_PATH).convert("RGBA")
    alpha = image.getchannel("A")
    bounds = alpha.getbbox()
    if bounds:
        image = image.crop(bounds)

    image.thumbnail((WIDTH - 2, CONTENT_HEIGHT), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))
    x = (WIDTH - image.width) // 2
    y = (HEIGHT - image.height) // 2
    canvas.alpha_composite(image, (x, y))
    return canvas


def pixel_values(image: Image.Image) -> list[int]:
    values: list[int] = []
    for red, green, blue, alpha in image.getdata():
        if alpha < 12:
            values.append(TRANSPARENT_KEY)
            continue

        # Preblend antialiased edges against the black splash background.
        opacity = alpha / 255.0
        red = round(red * opacity)
        green = round(green * opacity)
        blue = round(blue * opacity)
        value = rgb565(red, green, blue)
        values.append(0x0000 if value == TRANSPARENT_KEY else value)
    return values


def format_pixels(values: list[int]) -> str:
    lines = []
    for offset in range(0, len(values), 12):
        chunk = values[offset : offset + 12]
        lines.append("    " + ", ".join(f"0x{value:04X}" for value in chunk) + ",")
    return "\n".join(lines)


def main() -> None:
    image = prepare_image()
    PREVIEW_PATH.parent.mkdir(parents=True, exist_ok=True)
    image.save(PREVIEW_PATH)
    create_lopaka_preview(image)

    HEADER_PATH.write_text(
        """#ifndef SPLASH_AXOLOTL_ASSET_H
#define SPLASH_AXOLOTL_ASSET_H

#include <Arduino.h>

static constexpr int SPLASH_AXOLOTL_WIDTH = 96;
static constexpr int SPLASH_AXOLOTL_HEIGHT = 80;
static constexpr int SPLASH_AXOLOTL_PIXELS =
    SPLASH_AXOLOTL_WIDTH * SPLASH_AXOLOTL_HEIGHT;
static constexpr uint16_t SPLASH_AXOLOTL_TRANSPARENT = 0x0001;

extern const uint16_t SPLASH_AXOLOTL_IMAGE[SPLASH_AXOLOTL_PIXELS] PROGMEM;

#endif
""",
        encoding="utf-8",
        newline="\n",
    )

    CPP_PATH.write_text(
        """#include "SplashAxolotlAsset.h"

const uint16_t SPLASH_AXOLOTL_IMAGE[SPLASH_AXOLOTL_PIXELS] PROGMEM = {
"""
        + format_pixels(pixel_values(image))
        + "\n};\n",
        encoding="utf-8",
        newline="\n",
    )


if __name__ == "__main__":
    main()
