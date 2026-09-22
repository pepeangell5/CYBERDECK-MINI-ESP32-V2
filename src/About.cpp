#include "About.h"
#include "DisplayTFT.h"
#include "PepeDraw.h"
#include "Pins.h"
#include "SoundUtils.h"
#include "NVSStore.h"
#include "SystemInfo.h"
#include "SplashAxolotlAsset.h"
#include "SystemUi.h"

extern DisplayTFT tft;

// ═══════════════════════════════════════════════════════════════════════════
//  CONFIG
// ═══════════════════════════════════════════════════════════════════════════
#define VIEWPORT_TOP    34     // donde empieza el viewport (debajo del header)
#define VIEWPORT_BOTTOM 218    // donde termina (arriba del footer)
#define SCROLL_STEP     20

static int g_scrollY = 0;
static int g_maxScroll = 0;

// ═══════════════════════════════════════════════════════════════════════════
//  HELPERS DE DIBUJO CON CLIPPING (no dibuja fuera del viewport)
// ═══════════════════════════════════════════════════════════════════════════

// Dibuja texto SOLO si cae completamente dentro del viewport.
// Si está parcialmente fuera, no lo dibuja (evita manchar header/footer).
static void drawScrollableText(int yContent, int x, const String& text,
                                uint16_t color, int size) {
    int yScreen = VIEWPORT_TOP + (yContent - g_scrollY);
    int textH = (size == 1) ? 7 : (size * 8);

    // Si está completamente fuera del viewport, no dibujar
    if (yScreen + textH < VIEWPORT_TOP) return;
    if (yScreen > VIEWPORT_BOTTOM) return;

    // Si está parcialmente fuera, tampoco — evita que se desborde
    if (yScreen < VIEWPORT_TOP) return;
    if (yScreen + textH > VIEWPORT_BOTTOM) return;

    drawStringCustom(x, yScreen, text, color, size);
}

static void drawScrollableLine(int yContent, uint16_t color) {
    int yScreen = VIEWPORT_TOP + (yContent - g_scrollY);
    if (yScreen < VIEWPORT_TOP || yScreen > VIEWPORT_BOTTOM) return;
    tft.drawFastHLine(15, yScreen, 290, color);
}

// Usa exactamente el recurso RGB565 del splash. Se dibuja fila por fila para
// respetar el clipping vertical del contenido desplazable.
static void drawScrollableAjolote(int yContent) {
    const int W = SPLASH_AXOLOTL_WIDTH;
    const int H = SPLASH_AXOLOTL_HEIGHT;
    int x = (320 - W) / 2;
    int yBase = VIEWPORT_TOP + (yContent - g_scrollY);

    // Si está completamente fuera, salir
    if (yBase + H < VIEWPORT_TOP) return;
    if (yBase > VIEWPORT_BOTTOM) return;

    bool previousSwapBytes = tft.getSwapBytes();
    tft.setSwapBytes(true);

    for (int r = 0; r < H; r++) {
        int outY = yBase + r;
        if (outY < VIEWPORT_TOP) continue;     // arriba del viewport
        if (outY >= VIEWPORT_BOTTOM) break;    // no invadir el footer
        const uint16_t* rowPixels =
            SPLASH_AXOLOTL_IMAGE + r * SPLASH_AXOLOTL_WIDTH;
        tft.pushImage(x, outY, W, 1, rowPixels,
                      SPLASH_AXOLOTL_TRANSPARENT);
    }

    tft.setSwapBytes(previousSwapBytes);
}

// ═══════════════════════════════════════════════════════════════════════════
//  CONTENIDO PRINCIPAL
// ═══════════════════════════════════════════════════════════════════════════

static void drawAboutContent() {
    // Limpiar el viewport (NO el header ni el footer)
    tft.fillRect(2, VIEWPORT_TOP, 316, VIEWPORT_BOTTOM - VIEWPORT_TOP,
                 SYS_UI_BG);

    int y = 5;   // posición Y dentro del contenido virtual

    // ─── Título grande ───
    String title = "ESP32-TOOLS";
    int tw = title.length() * 8 * 3;   // size 3 con FONT_BIG
    drawScrollableText(y, (320 - tw) / 2, title, SYS_UI_TEXT, 3);
    y += 32;

    // ─── Versión ───
    String version = String(FW_VERSION);
    int vw = version.length() * 6 * 2;
    drawScrollableText(y, (320 - vw) / 2, version, SYS_UI_ACCENT, 2);
    y += 28;

    drawScrollableLine(y, SYS_UI_ACCENT);
    y += 12;

    // ─── Mismo ajolote RGB565 del splash (96x80) ───
    drawScrollableAjolote(y);
    y += SPLASH_AXOLOTL_HEIGHT + 10;

    drawScrollableLine(y, SYS_UI_ACCENT);
    y += 14;

    // ─── Autor ───
    drawScrollableText(y, 70, "By PepeAngell", SYS_UI_ACCENT, 2);
    y += 28;

    drawScrollableText(y, 30, "Jose Angel", SYS_UI_TEXT, 2);
    y += 22;
    drawScrollableText(y, 30, "Chavez Felix", SYS_UI_TEXT, 2);
    y += 28;

    drawScrollableText(y, 30, "Los Mochis, Sinaloa", UI_ACCENT, 1);
    y += 12;
    drawScrollableText(y, 30, "Mexico", UI_ACCENT, 1);
    y += 18;

    drawScrollableLine(y, SYS_UI_ACCENT);
    y += 14;

    // ─── Redes sociales ───
    drawScrollableText(y, 30, "REDES SOCIALES", SYS_UI_ACCENT, 1);
    y += 20;

    drawScrollableText(y, 30, "IG:", TFT_CYAN, 2);
    drawScrollableText(y, 80, "@ESP32_TOOLS", SYS_UI_TEXT, 2);
    y += 26;

    drawScrollableText(y, 30, "FB:", 0x041F, 2);
    drawScrollableText(y, 80, "/esp32tools", SYS_UI_TEXT, 2);
    y += 26;

    drawScrollableText(y, 30, "GH:", 0xA81F, 2);
    drawScrollableText(y, 80, "/pepeangell5", SYS_UI_TEXT, 2);
    y += 30;

    drawScrollableLine(y, SYS_UI_ACCENT);
    y += 14;

    // ─── Boot count ───
    int boots = nvsGetInt("boot_cnt", 0);
    String bootText = "Booteado " + String(boots) + " veces";
    int bw = bootText.length() * 6;
    drawScrollableText(y, (320 - bw) / 2, bootText, UI_ACCENT, 1);
    y += 18;

    drawScrollableLine(y, SYS_UI_ACCENT);
    y += 14;

    // ─── Quote / filosofía ───
    drawScrollableText(y, 30, "\"El conocimiento", TFT_GREEN, 2);
    y += 22;
    drawScrollableText(y, 30, "debe ser libre.\"", TFT_GREEN, 2);
    y += 32;

    drawScrollableText(y, 80, "HECHO", UI_ACCENT, 1);
    y += 12;
    drawScrollableText(y, 100, "EN MÉXICO", UI_ACCENT, 1);
    y += 25;

    // Calcular max scroll
    int viewportH = VIEWPORT_BOTTOM - VIEWPORT_TOP;
    g_maxScroll = y - viewportH;
    if (g_maxScroll < 0) g_maxScroll = 0;
}

// ═══════════════════════════════════════════════════════════════════════════
//  HEADER Y FOOTER (se redibujan SIEMPRE encima para evitar manchas)
// ═══════════════════════════════════════════════════════════════════════════

static void drawHeader() {
    tft.fillRect(0, 0, 320, VIEWPORT_TOP, SYS_UI_BG);
    tft.drawRoundRect(4, 4, 312, 232, 12, SYS_UI_ACCENT);
    drawStringCustom(110, 10, "ABOUT", SYS_UI_TEXT, 3);
    tft.drawFastHLine(10, VIEWPORT_TOP, 300, SYS_UI_ACCENT);
}

static void drawFooter() {
    tft.fillRect(5, VIEWPORT_BOTTOM, 310, 18, SYS_UI_BG);
    tft.drawFastHLine(10, VIEWPORT_BOTTOM, 300, SYS_UI_ACCENT);

    // Re-dibujar bordes laterales por si se mancharon
    tft.drawRoundRect(4, 4, 312, 232, 12, SYS_UI_ACCENT);

    if (g_maxScroll > 0) {
        if (g_scrollY == 0) {
            drawStringCustom(10, 226, "DOWN: VER MAS  BACK/OK: VOLVER",
                             UI_ACCENT, 1);
        } else if (g_scrollY >= g_maxScroll) {
            drawStringCustom(10, 226, "UP: SUBIR  BACK/OK: VOLVER",
                             UI_ACCENT, 1);
        } else {
            drawStringCustom(10, 226, "UP/DN: SCROLL  BACK/OK: VOLVER",
                             UI_ACCENT, 1);
        }

        // Indicador de scroll lateral
        int trackTop = VIEWPORT_TOP + 5;
        int trackBot = VIEWPORT_BOTTOM - 5;
        int trackH = trackBot - trackTop;
        int totalContent = g_maxScroll + (VIEWPORT_BOTTOM - VIEWPORT_TOP);
        int barH = (trackH * (VIEWPORT_BOTTOM - VIEWPORT_TOP)) / totalContent;
        if (barH < 10) barH = 10;
        int barY = trackTop;
        if (g_maxScroll > 0) {
            barY = trackTop + (g_scrollY * (trackH - barH)) / g_maxScroll;
        }
        // Limpiar track antes
        tft.fillRect(310, trackTop, 6, trackH, TFT_BLACK);
        tft.drawFastVLine(312, trackTop, trackH, UI_ACCENT);
        tft.fillRect(310, barY, 5, barH, UI_SELECT);
    } else {
        drawStringCustom(96, 226, "OK/BACK: VOLVER", UI_ACCENT, 1);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  REDIBUJADO COMPLETO (orden importante: contenido → header → footer)
// ═══════════════════════════════════════════════════════════════════════════

static void redrawAll() {
    drawAboutContent();    // 1. contenido scrolleable (puede manchar bordes)
    drawHeader();          // 2. header encima → tapa cualquier mancha arriba
    drawFooter();          // 3. footer encima → tapa cualquier mancha abajo
}

// ═══════════════════════════════════════════════════════════════════════════
//  ENTRY POINT
// ═══════════════════════════════════════════════════════════════════════════

void runAbout() {
    while (navEnterPressed() || navBackPressed()) delay(5);
    delay(100);

    g_scrollY = 0;

    // Beep de entrada (jingle de credits)
    beep(2400, 60); delay(40);
    beep(3000, 60); delay(40);
    beep(3600, 100);

    tft.fillScreen(TFT_BLACK);
    redrawAll();

    unsigned long lastBtn = 0;

    while (true) {
        if ((navEnterPressed() || navBackPressed()) && millis() - lastBtn > 200) {
            beep(1800, 50); delay(30);
            beep(1200, 80);
            while (navEnterPressed() || navBackPressed()) delay(5);
            delay(100);
            return;
        }

        if (navUpPressed() && millis() - lastBtn > 150) {
            if (g_scrollY > 0) {
                g_scrollY -= SCROLL_STEP;
                if (g_scrollY < 0) g_scrollY = 0;
                beep(2200, 20);
                redrawAll();
            }
            lastBtn = millis();
        }

        if (navDownPressed() && millis() - lastBtn > 150) {
            if (g_scrollY < g_maxScroll) {
                g_scrollY += SCROLL_STEP;
                if (g_scrollY > g_maxScroll) g_scrollY = g_maxScroll;
                beep(2200, 20);
                redrawAll();
            }
            lastBtn = millis();
        }

        delay(15);
    }
}
