#include "MenuSystem.h"

#include <string.h>

#include "PepeDraw.h"
#include "Pins.h"
#include "Input.h"
#include "SoundUtils.h"

#include "WifiScanner.h"
#include "ThreatMonitor.h"
#include "WifiAudit.h"
#include "RadioScanner.h"
#include "RadioJammer.h"
#include "NRFDiagnostics.h"
#include "PacketMonitor.h"
#include "SettingsMenu.h"
#include "SystemInfo.h"
#include "BLEAudit.h"
#include "BLEScanner.h"
#include "BLESpam.h"
#include "BTDisruptor.h"
#include "bt_jammer.h"
#include "BeaconSpam.h"
#include "Deauther.h"
#include "EvilPortal.h"
#include "Screensaver.h"
#include "ProbeSniffer.h"
#include "Karma.h"
#include "ClockWeather.h"
#include "About.h"
#include "PeripheralTools.h"
#include "CarouselIconAssets.h"

// Palette and panel hierarchy designed in the Lopaka "Main Launcher" screen.
static constexpr uint16_t MOD_BG       = TFT_BLACK;
static constexpr uint16_t MOD_PANEL    = 0x0883;
static constexpr uint16_t MOD_SELECTED = 0x1147;
static constexpr uint16_t MOD_TEXT     = TFT_WHITE;
static constexpr uint16_t MOD_LINE     = 0x26FE;
static constexpr uint16_t MOD_INVERT   = TFT_BLACK;
static constexpr uint16_t MOD_GLOW     = 0x26FE;
static constexpr uint16_t MOD_GLOW_2   = 0x32CE;
static constexpr uint16_t MOD_ACTION   = 0xF971;
static constexpr uint16_t MOD_OK       = 0x6FF7;
static constexpr uint16_t MOD_MUTED    = 0x7CD5;

static constexpr int MENU_TOP = 42;
static constexpr int ROW_H    = 31;
static constexpr int ROW_X    = 8;
static constexpr int ROW_W    = 152;
static constexpr int ROW_BOX_H = 27;
static constexpr int PREVIEW_X = 168;
static constexpr int PREVIEW_Y = 42;
static constexpr int PREVIEW_W = 144;
static constexpr int PREVIEW_H = 156;

static void handlerWifi();
static void handlerRadio();
static void handlerBT();
static void handlerMonitor();
static void handlerSystem();

enum class SubMenuIconSet : uint8_t {
    NONE,
    WIFI,
    RADIO,
    BLUETOOTH,
    SYSTEM,
    GPS,
    STORAGE,
    REPORTS
};

static int runSubMenuStyled(const char* title, const char* items[], int count,
                            uint16_t accent, SubMenuIconSet iconSet);

static const MainMenuEntry MAIN_ENTRIES[] = {
    { "WIFI",      "Scanner / portal", ICON_WIFI,      handlerWifi    },
    { "RADIO / RF","Dual NRF tools",   ICON_RADIO,     handlerRadio   },
    { "BLUETOOTH", "BLE lab modes",    ICON_BLUETOOTH, handlerBT      },
    { "MONITOR",   "Live packets",     ICON_MONITOR,   handlerMonitor },
    { "SYSTEM",    "Device config",    ICON_SYSTEM,    handlerSystem  },
};

static const int MAIN_COUNT = sizeof(MAIN_ENTRIES) / sizeof(MainMenuEntry);
static int currentEntry = 0;

static constexpr int SUBMENU_STATE_SLOTS = 16;

struct SubMenuState {
    const char* title;
    int cursor;
    int scrollOffset;
};

static SubMenuState subMenuStates[SUBMENU_STATE_SLOTS] = {};
static int nextSubMenuStateSlot = 0;

static SubMenuState* stateForSubMenu(const char* title) {
    for (int i = 0; i < SUBMENU_STATE_SLOTS; i++) {
        if (subMenuStates[i].title && strcmp(subMenuStates[i].title, title) == 0) {
            return &subMenuStates[i];
        }
    }

    for (int i = 0; i < SUBMENU_STATE_SLOTS; i++) {
        if (!subMenuStates[i].title) {
            subMenuStates[i] = { title, 0, 0 };
            return &subMenuStates[i];
        }
    }

    SubMenuState* state = &subMenuStates[nextSubMenuStateSlot];
    nextSubMenuStateSlot = (nextSubMenuStateSlot + 1) % SUBMENU_STATE_SLOTS;
    *state = { title, 0, 0 };
    return state;
}

static void drawBitmapCentered(int y, const String& text, uint16_t color,
                               int size, FontType font) {
    int w = getTextWidth(text, size, font);
    int x = (320 - w) / 2;
    if (x < 0) x = 0;

    if (font == FONT_BIG) drawStringBig(x, y, text, color, size);
    else drawStringCustom(x, y, text, color, size);
}

static void drawBitmapCenteredIn(int x0, int width, int y, const String& text,
                                 uint16_t color, int size, FontType font) {
    int w = getTextWidth(text, size, font);
    int x = x0 + (width - w) / 2;
    if (x < x0) x = x0;

    if (font == FONT_BIG) drawStringBig(x, y, text, color, size);
    else drawStringCustom(x, y, text, color, size);
}

static void drawBitmapRight(int xRight, int y, const String& text,
                            uint16_t color, int size, FontType font) {
    int w = getTextWidth(text, size, font);
    int x = xRight - w;

    if (font == FONT_BIG) drawStringBig(x, y, text, color, size);
    else drawStringCustom(x, y, text, color, size);
}

static void drawFrame() {
    tft.fillScreen(MOD_BG);
    tft.drawRoundRect(4, 4, 312, 232, 12, MOD_LINE);
    tft.fillRect(10, 10, 300, 29, MOD_PANEL);
    tft.drawFastHLine(10, 40, 300, MOD_LINE);
    tft.drawFastHLine(10, 207, 300, MOD_LINE);
}

static void drawWifiToolIcon(int icon, int cx, int cy, uint16_t color,
                             uint16_t detail, uint16_t background) {
    switch (icon) {
        case 0:  // Scanner: WiFi waves + magnifier
            tft.fillCircle(cx - 3, cy + 5, 2, color);
            tft.drawArc(cx - 3, cy + 5, 8, 7, 215, 325, color, background);
            tft.drawArc(cx - 3, cy + 5, 13, 12, 220, 320, color, background);
            tft.drawCircle(cx + 8, cy + 7, 5, detail);
            tft.drawLine(cx + 12, cy + 11, cx + 16, cy + 15, detail);
            break;

        case 1:  // Threat monitor: shield + activity pulse
            tft.drawLine(cx, cy - 14, cx - 11, cy - 9, color);
            tft.drawLine(cx - 11, cy - 9, cx - 9, cy + 5, color);
            tft.drawLine(cx - 9, cy + 5, cx, cy + 14, color);
            tft.drawLine(cx, cy + 14, cx + 9, cy + 5, color);
            tft.drawLine(cx + 9, cy + 5, cx + 11, cy - 9, color);
            tft.drawLine(cx + 11, cy - 9, cx, cy - 14, color);
            tft.drawLine(cx - 8, cy, cx - 4, cy, detail);
            tft.drawLine(cx - 4, cy, cx - 1, cy - 5, detail);
            tft.drawLine(cx - 1, cy - 5, cx + 3, cy + 5, detail);
            tft.drawLine(cx + 3, cy + 5, cx + 7, cy, detail);
            break;

        case 2:  // Audit: clipboard + check
            tft.drawRoundRect(cx - 10, cy - 11, 20, 25, 3, color);
            tft.drawRect(cx - 5, cy - 14, 10, 5, color);
            tft.drawLine(cx - 6, cy + 2, cx - 1, cy + 7, detail);
            tft.drawLine(cx - 1, cy + 7, cx + 7, cy - 3, detail);
            break;

        case 3:  // Beacon: antenna + broadcast rings
            tft.drawLine(cx, cy - 2, cx, cy + 13, color);
            tft.drawLine(cx - 6, cy + 13, cx + 6, cy + 13, color);
            tft.fillCircle(cx, cy - 5, 2, detail);
            tft.drawCircle(cx, cy - 5, 8, color);
            tft.drawCircle(cx, cy - 5, 13, color);
            break;

        case 4:  // Deauther: disconnected link
            tft.drawRoundRect(cx - 14, cy - 7, 13, 10, 5, color);
            tft.drawRoundRect(cx + 1, cy - 3, 13, 10, 5, color);
            tft.drawLine(cx - 3, cy - 11, cx + 3, cy + 11, detail);
            tft.drawLine(cx + 3, cy - 11, cx - 3, cy + 11, detail);
            break;

        case 5:  // Evil portal: doorway
            tft.drawRoundRect(cx - 12, cy - 12, 24, 24, 4, color);
            tft.drawRect(cx - 6, cy - 7, 13, 19, detail);
            tft.fillCircle(cx + 3, cy + 2, 1, color);
            tft.drawLine(cx - 15, cy - 15, cx + 15, cy + 15, detail);
            break;

        case 6:  // Probe sniffer: packet + magnifier
            tft.drawRect(cx - 14, cy - 10, 18, 15, color);
            tft.drawFastHLine(cx - 10, cy - 5, 10, detail);
            tft.drawFastHLine(cx - 10, cy, 7, detail);
            tft.drawCircle(cx + 7, cy + 6, 6, color);
            tft.drawLine(cx + 11, cy + 10, cx + 15, cy + 14, color);
            break;

        case 7:  // KARMA: target
        default:
            tft.drawCircle(cx, cy, 12, color);
            tft.drawCircle(cx, cy, 6, detail);
            tft.fillCircle(cx, cy, 2, color);
            tft.drawFastHLine(cx - 16, cy, 8, color);
            tft.drawFastHLine(cx + 8, cy, 8, color);
            tft.drawFastVLine(cx, cy - 16, 8, color);
            tft.drawFastVLine(cx, cy + 8, 8, color);
            break;
    }
}

static void drawRadioToolIcon(int icon, int cx, int cy, uint16_t color,
                              uint16_t detail, uint16_t background) {
    (void)background;
    switch (icon) {
        case 0:  // Channel control / carrier
            tft.drawFastHLine(cx - 14, cy + 11, 28, color);
            for (int i = 0; i < 5; i++) {
                int h = 5 + i * 4;
                tft.fillRect(cx - 13 + i * 6, cy + 10 - h, 3, h,
                             i == 3 ? detail : color);
            }
            break;
        case 1:  // Spectrum
            for (int i = 0; i < 7; i++) {
                int h = 4 + ((i * 7) % 17);
                tft.fillRect(cx - 15 + i * 5, cy + 11 - h, 3, h,
                             i > 4 ? detail : color);
            }
            tft.drawFastHLine(cx - 16, cy + 12, 33, color);
            break;
        case 2:  // Baseline comparison
            tft.drawFastHLine(cx - 15, cy + 11, 30, color);
            tft.drawLine(cx - 14, cy + 4, cx - 8, cy - 3, color);
            tft.drawLine(cx - 8, cy - 3, cx - 2, cy + 2, color);
            tft.drawLine(cx - 2, cy + 2, cx + 4, cy - 10, detail);
            tft.drawLine(cx + 4, cy - 10, cx + 10, cy - 2, detail);
            tft.drawLine(cx + 10, cy - 2, cx + 15, cy - 6, color);
            break;
        case 3:  // NRF diagnostic chip
        default:
            tft.drawRoundRect(cx - 11, cy - 10, 22, 21, 3, color);
            for (int i = -8; i <= 8; i += 5) {
                tft.drawFastHLine(cx - 15, cy + i, 4, detail);
                tft.drawFastHLine(cx + 11, cy + i, 4, detail);
            }
            tft.drawCircle(cx, cy, 4, detail);
            tft.fillCircle(cx, cy, 1, color);
            break;
    }
}

static void drawBluetoothToolIcon(int icon, int cx, int cy, uint16_t color,
                                  uint16_t detail, uint16_t background) {
    (void)background;
    switch (icon) {
        case 0:  // Defense shield
            tft.drawLine(cx, cy - 14, cx - 12, cy - 8, color);
            tft.drawLine(cx - 12, cy - 8, cx - 9, cy + 7, color);
            tft.drawLine(cx - 9, cy + 7, cx, cy + 14, color);
            tft.drawLine(cx, cy + 14, cx + 9, cy + 7, color);
            tft.drawLine(cx + 9, cy + 7, cx + 12, cy - 8, color);
            tft.drawLine(cx + 12, cy - 8, cx, cy - 14, color);
            tft.drawCircle(cx, cy, 4, detail);
            break;
        case 1:  // BLE scan / magnifier
            tft.drawCircle(cx - 3, cy - 3, 9, color);
            tft.drawLine(cx + 4, cy + 4, cx + 14, cy + 14, color);
            tft.drawLine(cx - 3, cy - 9, cx - 3, cy + 4, detail);
            tft.drawLine(cx - 3, cy - 9, cx + 3, cy - 4, detail);
            tft.drawLine(cx - 3, cy + 4, cx + 3, cy - 1, detail);
            break;
        case 2:  // Advertisement broadcast
            tft.fillCircle(cx, cy, 2, detail);
            tft.drawCircle(cx, cy, 7, color);
            tft.drawCircle(cx, cy, 13, color);
            tft.drawFastVLine(cx, cy + 13, 5, detail);
            break;
        case 3:  // Directed disruptor / target
            tft.drawCircle(cx, cy, 12, color);
            tft.drawCircle(cx, cy, 6, detail);
            tft.drawFastHLine(cx - 16, cy, 8, color);
            tft.drawFastHLine(cx + 8, cy, 8, color);
            tft.drawFastVLine(cx, cy - 16, 8, color);
            tft.drawFastVLine(cx, cy + 8, 8, color);
            break;
        case 4:  // Wideband jammer
        default:
            for (int i = 0; i < 6; i++) {
                int h = 5 + ((i * 7) % 18);
                tft.fillRect(cx - 14 + i * 5, cy + 11 - h, 3, h,
                             i == 4 ? detail : color);
            }
            tft.drawFastHLine(cx - 15, cy + 12, 31, color);
            break;
    }
}

static void drawSystemToolIcon(int icon, int cx, int cy, uint16_t color,
                               uint16_t detail, uint16_t background) {
    (void)background;
    switch (icon) {
        case 0:  // Mission dashboard
            tft.drawRoundRect(cx - 14, cy - 12, 28, 24, 3, color);
            tft.drawFastVLine(cx, cy - 10, 20, detail);
            tft.drawFastHLine(cx - 12, cy, 24, detail);
            tft.fillRect(cx - 10, cy - 8, 7, 5, color);
            tft.drawLine(cx + 3, cy + 7, cx + 7, cy + 3, color);
            tft.drawLine(cx + 7, cy + 3, cx + 11, cy + 6, color);
            break;

        case 1:  // Audit reports
            tft.drawRoundRect(cx - 11, cy - 14, 22, 28, 3, color);
            tft.drawFastHLine(cx - 6, cy - 7, 12, detail);
            tft.drawFastHLine(cx - 6, cy - 2, 12, detail);
            tft.drawLine(cx - 6, cy + 5, cx - 2, cy + 9, detail);
            tft.drawLine(cx - 2, cy + 9, cx + 7, cy + 1, detail);
            break;

        case 2:  // Settings gear
            tft.drawCircle(cx, cy, 10, color);
            tft.drawCircle(cx, cy, 4, detail);
            for (int i = 0; i < 8; i++) {
                float a = i * 0.785398f;
                int x1 = cx + (int)(11 * cosf(a));
                int y1 = cy + (int)(11 * sinf(a));
                int x2 = cx + (int)(15 * cosf(a));
                int y2 = cy + (int)(15 * sinf(a));
                tft.drawLine(x1, y1, x2, y2, color);
            }
            break;

        case 3:  // System information
            tft.drawCircle(cx, cy, 13, color);
            drawStringBig(cx - 3, cy - 9, "i", detail, 1);
            break;

        case 4:  // NRF diagnostic chip
            tft.drawRoundRect(cx - 11, cy - 10, 22, 21, 3, color);
            for (int i = -8; i <= 8; i += 5) {
                tft.drawFastHLine(cx - 15, cy + i, 4, detail);
                tft.drawFastHLine(cx + 11, cy + i, 4, detail);
            }
            tft.drawFastHLine(cx - 6, cy - 3, 12, detail);
            tft.drawFastHLine(cx - 6, cy + 3, 8, detail);
            break;

        case 5:  // GPS tools / location pin
            tft.drawCircle(cx, cy - 5, 9, color);
            tft.fillCircle(cx, cy - 5, 3, detail);
            tft.drawLine(cx - 7, cy + 1, cx, cy + 14, color);
            tft.drawLine(cx + 7, cy + 1, cx, cy + 14, color);
            break;

        case 6:  // MicroSD manager
            tft.drawRoundRect(cx - 13, cy - 11, 26, 23, 3, color);
            tft.drawLine(cx - 13, cy - 5, cx + 13, cy - 5, color);
            tft.drawLine(cx - 9, cy - 11, cx - 3, cy - 15, detail);
            tft.drawLine(cx - 3, cy - 15, cx + 4, cy - 15, detail);
            tft.drawLine(cx + 4, cy - 15, cx + 8, cy - 11, detail);
            break;

        case 7:  // MicroSD information
            tft.drawRect(cx - 10, cy - 14, 20, 28, color);
            tft.drawLine(cx + 3, cy - 14, cx + 10, cy - 7, color);
            tft.drawFastVLine(cx - 6, cy - 10, 5, detail);
            tft.drawFastVLine(cx - 2, cy - 10, 5, detail);
            drawStringBig(cx - 3, cy, "i", detail, 1);
            break;

        case 8:  // Battery ADC
            tft.drawRoundRect(cx - 14, cy - 9, 26, 18, 3, color);
            tft.fillRect(cx + 12, cy - 4, 4, 8, color);
            tft.fillRect(cx - 10, cy - 5, 10, 10, detail);
            tft.drawLine(cx + 4, cy - 5, cx, cy + 1, color);
            tft.drawLine(cx, cy + 1, cx + 6, cy + 1, color);
            break;

        case 9:  // Clock and weather
            tft.drawCircle(cx - 4, cy + 1, 11, color);
            tft.drawLine(cx - 4, cy + 1, cx - 4, cy - 6, detail);
            tft.drawLine(cx - 4, cy + 1, cx + 2, cy + 5, detail);
            tft.drawCircle(cx + 10, cy - 9, 4, detail);
            tft.drawFastHLine(cx + 6, cy + 12, 12, color);
            break;

        case 10: // About
        default:
            tft.drawCircle(cx, cy, 13, color);
            tft.fillCircle(cx, cy - 6, 2, detail);
            tft.drawFastVLine(cx, cy - 1, 10, detail);
            tft.drawFastHLine(cx - 3, cy + 9, 7, detail);
            break;
    }
}

static void drawGpsToolIcon(int icon, int cx, int cy, uint16_t color,
                            uint16_t detail, uint16_t background) {
    (void)background;
    switch (icon) {
        case 0: // dashboard
            tft.drawCircle(cx, cy, 13, color);
            tft.drawLine(cx, cy, cx + 8, cy - 6, detail);
            tft.fillCircle(cx, cy, 2, detail);
            break;
        case 1: // fix assist / satellite
            tft.drawCircle(cx - 5, cy - 4, 6, color);
            tft.drawLine(cx - 1, cy, cx + 10, cy + 11, color);
            tft.drawArc(cx + 6, cy - 7, 8, 6, 205, 335, detail, background);
            break;
        case 2: // track
            tft.drawCircle(cx - 9, cy + 8, 3, detail);
            tft.drawCircle(cx + 10, cy - 9, 3, detail);
            tft.drawLine(cx - 6, cy + 7, cx - 1, cy - 3, color);
            tft.drawLine(cx - 1, cy - 3, cx + 4, cy + 4, color);
            tft.drawLine(cx + 4, cy + 4, cx + 8, cy - 6, color);
            break;
        case 3: // wardrive
            tft.drawRect(cx - 13, cy + 5, 26, 8, color);
            tft.drawLine(cx - 9, cy + 5, cx - 5, cy - 2, color);
            tft.drawLine(cx + 9, cy + 5, cx + 5, cy - 2, color);
            tft.drawCircle(cx, cy - 7, 3, detail);
            tft.drawCircle(cx, cy - 7, 8, detail);
            break;
        case 4: // compass
            tft.drawCircle(cx, cy, 14, color);
            tft.fillTriangle(cx, cy - 11, cx - 4, cy + 5, cx + 4, cy + 5, detail);
            tft.drawLine(cx, cy + 11, cx - 4, cy + 5, color);
            tft.drawLine(cx, cy + 11, cx + 4, cy + 5, color);
            break;
        case 5: // waypoint pin
            tft.drawCircle(cx, cy - 5, 8, color);
            tft.fillCircle(cx, cy - 5, 2, detail);
            tft.drawLine(cx - 6, cy, cx, cy + 14, color);
            tft.drawLine(cx + 6, cy, cx, cy + 14, color);
            break;
        case 6: // live pulse
            tft.drawLine(cx - 14, cy, cx - 7, cy, color);
            tft.drawLine(cx - 7, cy, cx - 3, cy - 9, color);
            tft.drawLine(cx - 3, cy - 9, cx + 2, cy + 10, detail);
            tft.drawLine(cx + 2, cy + 10, cx + 6, cy, color);
            tft.drawLine(cx + 6, cy, cx + 14, cy, color);
            break;
        case 7: // coordinates
            tft.drawCircle(cx, cy, 11, color);
            tft.drawFastHLine(cx - 15, cy, 30, detail);
            tft.drawFastVLine(cx, cy - 15, 30, detail);
            break;
        case 8: // signal stats
            for (int i = 0; i < 4; i++)
                tft.fillRect(cx - 13 + i * 8, cy + 10 - (i + 1) * 5,
                             5, (i + 1) * 5, i == 3 ? detail : color);
            break;
        case 9: // console
            tft.drawRoundRect(cx - 14, cy - 11, 28, 22, 3, color);
            tft.drawLine(cx - 9, cy - 4, cx - 4, cy, detail);
            tft.drawLine(cx - 4, cy, cx - 9, cy + 4, detail);
            tft.drawFastHLine(cx, cy + 4, 8, detail);
            break;
        case 10: // SD log
            tft.drawRect(cx - 10, cy - 14, 20, 28, color);
            tft.drawLine(cx + 3, cy - 14, cx + 10, cy - 7, color);
            tft.drawFastHLine(cx - 5, cy, 10, detail);
            tft.drawFastHLine(cx - 5, cy + 5, 10, detail);
            break;
        case 11: // export
        default:
            tft.drawRect(cx - 12, cy - 10, 24, 22, color);
            tft.drawFastVLine(cx, cy - 15, 17, detail);
            tft.drawLine(cx, cy - 15, cx - 6, cy - 9, detail);
            tft.drawLine(cx, cy - 15, cx + 6, cy - 9, detail);
            break;
    }
}

static void drawStorageToolIcon(int icon, int cx, int cy, uint16_t color,
                                uint16_t detail, uint16_t background) {
    (void)background;
    if (icon == 0) { // folder browser
        tft.drawRoundRect(cx - 14, cy - 8, 28, 20, 3, color);
        tft.drawRect(cx - 11, cy - 13, 12, 6, color);
    } else if (icon == 1) { // reports
        tft.drawRect(cx - 11, cy - 14, 22, 28, color);
        tft.drawFastHLine(cx - 6, cy - 6, 12, detail);
        tft.drawFastHLine(cx - 6, cy, 12, detail);
        tft.drawFastHLine(cx - 6, cy + 6, 8, detail);
    } else if (icon == 2) { // create folder
        tft.drawRoundRect(cx - 14, cy - 7, 28, 19, 3, color);
        tft.drawFastHLine(cx - 6, cy + 2, 12, detail);
        tft.drawFastVLine(cx, cy - 4, 13, detail);
    } else if (icon == 3) { // export index
        tft.drawRect(cx - 12, cy - 10, 24, 22, color);
        tft.drawFastVLine(cx, cy - 15, 17, detail);
        tft.drawLine(cx, cy - 15, cx - 5, cy - 10, detail);
        tft.drawLine(cx, cy - 15, cx + 5, cy - 10, detail);
    } else if (icon == 4) { // clean
        tft.drawRect(cx - 9, cy - 8, 18, 20, color);
        tft.drawFastHLine(cx - 12, cy - 11, 24, detail);
        tft.drawFastVLine(cx - 4, cy - 5, 12, detail);
        tft.drawFastVLine(cx + 4, cy - 5, 12, detail);
    } else { // information
        tft.drawRect(cx - 10, cy - 14, 20, 28, color);
        tft.fillCircle(cx, cy - 6, 2, detail);
        tft.drawFastVLine(cx, cy, 9, detail);
    }
}

static void drawReportToolIcon(int icon, int cx, int cy, uint16_t color,
                               uint16_t detail, uint16_t background) {
    (void)background;
    tft.drawRoundRect(cx - 13, cy - 13, 26, 26, 4, color);
    if (icon == 0) {
        tft.drawCircle(cx, cy + 2, 7, detail);
        tft.drawArc(cx, cy + 2, 12, 10, 210, 330, color, background);
    } else if (icon == 1) {
        tft.drawCircle(cx, cy - 3, 6, detail);
        tft.drawLine(cx - 5, cy + 1, cx, cy + 10, detail);
        tft.drawLine(cx + 5, cy + 1, cx, cy + 10, detail);
    } else {
        tft.drawRoundRect(cx - 8, cy - 5, 16, 12, 2, detail);
        tft.fillRect(cx + 8, cy - 2, 3, 6, detail);
    }
}

static void drawMainHeader() {
    tft.fillRect(10, 10, 300, 29, MOD_PANEL);
    drawStringBig(14, 14, "CYBERDECK MINI", MOD_TEXT, 1);
    drawStringCustom(136, 17, "MAIN MENU", MOD_GLOW, 1);

    String count = String(currentEntry + 1) + "/" + String(MAIN_COUNT);
    drawBitmapRight(302, 17, count, MOD_OK, 1, FONT_SMALL);

    tft.drawRect(236, 29, 66, 5, MOD_GLOW);
    int fillW = ((currentEntry + 1) * 68) / MAIN_COUNT;
    if (fillW > 64) fillW = 64;
    if (fillW > 0) tft.fillRect(237, 30, fillW, 3, MOD_OK);
}

static void drawMainFooter() {
    tft.fillRect(10, 208, 300, 24, MOD_BG);
    drawStringCustom(14, 218, "UP/DN: SELECT", MOD_TEXT, 1);
    drawBitmapRight(304, 218, "OK: OPEN", MOD_ACTION, 1, FONT_SMALL);
}

static void clearMainArea() {
    tft.fillRect(1, 35, 318, 171, MOD_BG);
}

static void drawGlowBox(int x, int y, int w, int h) {
    tft.drawRect(x - 2, y - 2, w + 4, h + 4, MOD_GLOW_2);
    tft.drawRect(x - 1, y - 1, w + 2, h + 2, MOD_GLOW);
    tft.drawRect(x, y, w, h, MOD_TEXT);
}

static void drawWifiIcon(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawRect(cx - 10, cy + 5, 20, 7, color);
    tft.fillCircle(cx - 5, cy + 8, 1, accent);
    tft.fillCircle(cx + 5, cy + 8, 1, accent);

    tft.drawLine(cx - 11, cy - 1, cx - 6, cy - 6, color);
    tft.drawLine(cx - 6, cy - 6, cx + 6, cy - 6, color);
    tft.drawLine(cx + 6, cy - 6, cx + 11, cy - 1, color);
    tft.drawLine(cx - 6, cy + 2, cx - 3, cy - 1, accent);
    tft.drawLine(cx - 3, cy - 1, cx + 3, cy - 1, accent);
    tft.drawLine(cx + 3, cy - 1, cx + 6, cy + 2, accent);
}

static void drawRadioIcon(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawFastHLine(cx - 14, cy + 13, 28, color);
    for (int i = 0; i < 5; i++) {
        int h = 6 + i * 3;
        int x = cx - 13 + i * 6;
        tft.fillRect(x, cy + 12 - h, 3, h, (i == 3) ? accent : color);
    }
    tft.drawCircle(cx + 12, cy - 7, 5, color);
    tft.fillCircle(cx + 12, cy - 7, 2, accent);
}

static void drawBluetoothIcon(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawLine(cx, cy - 12, cx, cy + 12, color);
    tft.drawLine(cx, cy - 12, cx + 9, cy - 4, color);
    tft.drawLine(cx + 9, cy - 4, cx - 7, cy + 9, color);
    tft.drawLine(cx, cy + 12, cx + 9, cy + 4, color);
    tft.drawLine(cx + 9, cy + 4, cx - 7, cy - 9, color);
    tft.fillCircle(cx, cy, 2, accent);
}

static void drawMonitorIcon(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawRect(cx - 15, cy - 12, 30, 20, color);
    tft.drawLine(cx - 11, cy, cx - 5, cy, color);
    tft.drawLine(cx - 5, cy, cx - 2, cy - 6, accent);
    tft.drawLine(cx - 2, cy - 6, cx + 3, cy + 5, accent);
    tft.drawLine(cx + 3, cy + 5, cx + 7, cy - 2, color);
    tft.drawLine(cx + 7, cy - 2, cx + 12, cy - 2, color);
    tft.drawFastVLine(cx, cy + 8, 5, color);
    tft.drawFastHLine(cx - 8, cy + 13, 16, color);
}

static void drawSystemIcon(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawCircle(cx, cy, 8, color);
    tft.drawCircle(cx, cy, 4, accent);
    tft.drawFastHLine(cx - 13, cy, 5, color);
    tft.drawFastHLine(cx + 8, cy, 5, color);
    tft.drawFastVLine(cx, cy - 13, 5, color);
    tft.drawFastVLine(cx, cy + 8, 5, color);
    tft.drawLine(cx - 8, cy - 8, cx - 11, cy - 11, color);
    tft.drawLine(cx + 8, cy - 8, cx + 11, cy - 11, color);
    tft.drawLine(cx - 8, cy + 8, cx - 11, cy + 11, color);
    tft.drawLine(cx + 8, cy + 8, cx + 11, cy + 11, color);
}

static void drawVectorIcon(IconID id, int cx, int cy, uint16_t color,
                           uint16_t accent) {
    tft.drawCircle(cx, cy, 13, color);
    tft.drawCircle(cx, cy, 11, accent);

    switch (id) {
        case ICON_WIFI:      drawWifiIcon(cx, cy, color, accent);      break;
        case ICON_RADIO:     drawRadioIcon(cx, cy, color, accent);     break;
        case ICON_BLUETOOTH: drawBluetoothIcon(cx, cy, color, accent); break;
        case ICON_MONITOR:   drawMonitorIcon(cx, cy, color, accent);   break;
        case ICON_SYSTEM:
        default:             drawSystemIcon(cx, cy, color, accent);    break;
    }
}

static void drawHeroWifi(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawRect(cx - 25, cy + 18, 50, 14, color);
    tft.drawFastHLine(cx - 20, cy + 24, 40, accent);
    tft.fillCircle(cx - 13, cy + 25, 2, accent);
    tft.fillCircle(cx + 13, cy + 25, 2, accent);

    tft.drawLine(cx - 34, cy + 8, cx - 20, cy - 6, color);
    tft.drawLine(cx - 20, cy - 6, cx + 20, cy - 6, color);
    tft.drawLine(cx + 20, cy - 6, cx + 34, cy + 8, color);
    tft.drawLine(cx - 24, cy + 9, cx - 13, cy - 1, accent);
    tft.drawLine(cx - 13, cy - 1, cx + 13, cy - 1, accent);
    tft.drawLine(cx + 13, cy - 1, cx + 24, cy + 9, accent);
}

static void drawHeroRadio(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawFastHLine(cx - 28, cy + 26, 56, color);
    for (int i = 0; i < 7; i++) {
        int h = 10 + (i % 4) * 8;
        int x = cx - 25 + i * 8;
        tft.fillRect(x, cy + 25 - h, 4, h, (i == 4) ? accent : color);
    }
    tft.drawCircle(cx + 27, cy - 18, 10, color);
    tft.drawCircle(cx + 27, cy - 18, 16, accent);
    tft.fillCircle(cx + 27, cy - 18, 3, accent);
}

static void drawHeroBluetooth(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawLine(cx, cy - 34, cx, cy + 34, color);
    tft.drawLine(cx, cy - 34, cx + 26, cy - 12, color);
    tft.drawLine(cx + 26, cy - 12, cx - 20, cy + 24, color);
    tft.drawLine(cx, cy + 34, cx + 26, cy + 12, color);
    tft.drawLine(cx + 26, cy + 12, cx - 20, cy - 24, color);
    tft.fillCircle(cx, cy, 5, accent);
    tft.drawCircle(cx, cy, 10, accent);
}

static void drawHeroMonitor(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawRect(cx - 36, cy - 26, 72, 48, color);
    tft.drawRect(cx - 31, cy - 21, 62, 36, color);
    tft.drawLine(cx - 26, cy, cx - 14, cy, color);
    tft.drawLine(cx - 14, cy, cx - 8, cy - 14, accent);
    tft.drawLine(cx - 8, cy - 14, cx + 4, cy + 12, accent);
    tft.drawLine(cx + 4, cy + 12, cx + 13, cy - 7, color);
    tft.drawLine(cx + 13, cy - 7, cx + 26, cy - 7, color);
    tft.drawFastVLine(cx, cy + 22, 10, color);
    tft.drawFastHLine(cx - 18, cy + 32, 36, color);
}

static void drawHeroSystem(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawCircle(cx, cy, 25, color);
    tft.drawCircle(cx, cy, 12, accent);
    tft.drawFastHLine(cx - 42, cy, 17, color);
    tft.drawFastHLine(cx + 25, cy, 17, color);
    tft.drawFastVLine(cx, cy - 42, 17, color);
    tft.drawFastVLine(cx, cy + 25, 17, color);
    tft.drawLine(cx - 26, cy - 26, cx - 36, cy - 36, color);
    tft.drawLine(cx + 26, cy - 26, cx + 36, cy - 36, color);
    tft.drawLine(cx - 26, cy + 26, cx - 36, cy + 36, color);
    tft.drawLine(cx + 26, cy + 26, cx + 36, cy + 36, color);
    tft.fillCircle(cx, cy, 4, accent);
}

static void drawHeroIcon(IconID id, int cx, int cy, uint16_t color,
                         uint16_t accent) {
    tft.drawCircle(cx, cy, 43, MOD_GLOW_2);
    tft.drawCircle(cx, cy, 40, MOD_GLOW);
    tft.drawCircle(cx, cy, 36, MOD_TEXT);

    switch (id) {
        case ICON_WIFI:      drawHeroWifi(cx, cy, color, accent);      break;
        case ICON_RADIO:     drawHeroRadio(cx, cy, color, accent);     break;
        case ICON_BLUETOOTH: drawHeroBluetooth(cx, cy, color, accent); break;
        case ICON_MONITOR:   drawHeroMonitor(cx, cy, color, accent);   break;
        case ICON_SYSTEM:
        default:             drawHeroSystem(cx, cy, color, accent);    break;
    }
}

static const uint16_t CAROUSEL_COLORS[] = {
    0x26FE,  // WIFI: cyan
    0xFD80,  // RADIO/RF: amber
    0xA35F,  // BLUETOOTH: violet
    0x6FF7,  // MONITOR: mint
    0xFE4A   // SYSTEM: warm yellow
};

static const char* CAROUSEL_SECTION[] = {
    "WIRELESS OPERATIONS",
    "2.4 GHZ SIGNAL LAB",
    "BLE DEVICE LAB",
    "LIVE PACKET VIEW",
    "DEVICE CONTROL"
};

static const char* CAROUSEL_FUNCTIONS[] = {
    "SCAN  AUDIT  PORTAL",
    "JAMMER  SPECTRUM  NRF",
    "SCAN  INSPECT  AUDIT",
    "CAPTURE  GRAPH  STATS",
    "INFO  SETTINGS  CLOCK"
};

static String carouselLabel(int idx) {
    return "0" + String(idx + 1) + "  " + String(MAIN_ENTRIES[idx].title);
}

static void drawCarouselPill(int y, int idx, bool pointsUp) {
    tft.fillRoundRect(68, y, 184, 22, 11, MOD_PANEL);
    tft.drawRoundRect(68, y, 184, 22, 11, MOD_GLOW_2);
    drawBitmapCentered(y + 7, carouselLabel(idx), MOD_MUTED, 1, FONT_SMALL);

    int cy = pointsUp ? y + 4 : y + 18;
    int tip = pointsUp ? cy - 3 : cy + 3;
    tft.drawLine(155, cy, 160, tip, MOD_MUTED);
    tft.drawLine(160, tip, 165, cy, MOD_MUTED);
}

static void drawCarouselDots(int active, uint16_t color) {
    for (int i = 0; i < MAIN_COUNT; i++) {
        int y = 91 + i * 14;
        tft.fillCircle(294, y, i == active ? 3 : 2,
                       i == active ? color : MOD_GLOW_2);
    }
}

static void drawCarouselMenu(bool pressed = false) {
    if (currentEntry < 0 || currentEntry >= MAIN_COUNT) return;

    const MainMenuEntry& entry = MAIN_ENTRIES[currentEntry];
    int previous = (currentEntry - 1 + MAIN_COUNT) % MAIN_COUNT;
    int next = (currentEntry + 1) % MAIN_COUNT;
    uint16_t accent = CAROUSEL_COLORS[currentEntry];
    uint16_t border = pressed ? MOD_ACTION : accent;

    tft.fillScreen(MOD_BG);
    tft.drawRoundRect(5, 5, 310, 230, 16, accent);
    drawCarouselPill(10, previous, true);

    // Keep the bitmap's dark pre-blended background stable while pressing.
    // Feedback is shown with the border and action marker, avoiding a flash
    // or a dark halo around the antialiased icon.
    tft.fillRoundRect(14, 39, 292, 158, 18, MOD_PANEL);
    tft.drawRoundRect(14, 39, 292, 158, 18, border);
    tft.drawRoundRect(18, 43, 284, 150, 15, MOD_GLOW_2);

    tft.fillRoundRect(28, 53, 38, 25, 8, MOD_SELECTED);
    tft.fillRoundRect(28, 53, 5, 25, 3, pressed ? MOD_INVERT : MOD_ACTION);
    drawStringCustom(39, 62, "0" + String(currentEntry + 1),
                     pressed ? MOD_INVERT : MOD_TEXT, 1);

    const uint16_t* icon = getCarouselIcon(currentEntry);
    // The generated assets are regular host-order RGB565 words. TFT_eSPI's
    // PROGMEM pushImage overload defaults to byte-swapped input, which both
    // corrupts the colours and turns the 0x0001 transparency key into a green
    // square. Swap while drawing the icon, then restore the previous setting.
    bool previousSwapBytes = tft.getSwapBytes();
    tft.setSwapBytes(true);
    tft.pushImage(124, 67, CAROUSEL_ICON_SIZE, CAROUSEL_ICON_SIZE,
                  icon, CAROUSEL_ICON_TRANSPARENT);
    tft.setSwapBytes(previousSwapBytes);
    tft.fillRect(78, 146, 164, 2, MOD_GLOW_2);

    drawBitmapCentered(153, entry.title, MOD_TEXT, 1, FONT_BIG);
    drawBitmapCentered(169, CAROUSEL_SECTION[currentEntry],
                       pressed ? MOD_ACTION : accent, 1, FONT_SMALL);
    drawBitmapCentered(182, CAROUSEL_FUNCTIONS[currentEntry],
                       MOD_OK, 1, FONT_SMALL);

    drawCarouselPill(202, next, false);
    drawCarouselDots(currentEntry, accent);
}

static void drawMainRow(int idx, bool selected, bool pressed) {
    if (selected && idx == currentEntry) drawCarouselMenu(pressed);
}

static void redrawMainMenu(bool pressed = false) {
    tft.startWrite();
    drawCarouselMenu(pressed);
    tft.endWrite();
}

static void changeMainEntry(int nextEntry, bool updatePreview = true) {
    (void)updatePreview;
    currentEntry = nextEntry;
    redrawMainMenu(false);
}

static void handlerWifi() {
    static const char* wifiItems[] = {
        "WiFi Scanner",
        "Threat Monitor",
        "WiFi Audit",
        "Beacon Spam",
        "Deauther",
        "Evil Portal",
        "Probe Sniffer",
        "KARMA Attack"
    };

    bool exitSub = false;
    while (!exitSub) {
        int choice = runSubMenuStyled("WIFI TOOLS", wifiItems,
                                      sizeof(wifiItems) / sizeof(char*),
                                      CAROUSEL_COLORS[0], SubMenuIconSet::WIFI);
        switch (choice) {
            case -1: exitSub = true;    break;
            case  0: runWifiScan();     break;
            case  1: runThreatMonitor(); break;
            case  2: runWifiAudit();    break;
            case  3: runBeaconSpam();   break;
            case  4: runDeauther();     break;
            case  5: runEvilPortal();   break;
            case  6: runProbeSniffer(); break;
            case  7: runKarma();        break;
        }
    }
}

static void handlerRadio() {
    static const char* radioItems[] = {
        "Jammer",
        "Spectrum",
        "RF Baseline",
        "NRF Diagnostic"
    };

    bool exitSub = false;
    while (!exitSub) {
        int choice = runSubMenuStyled("RADIO TOOLS", radioItems,
                                      sizeof(radioItems) / sizeof(char*),
                                      CAROUSEL_COLORS[1], SubMenuIconSet::RADIO);
        switch (choice) {
            case -1: exitSub = true;       break;
            case  0: runRadioJammer();     break;
            case  1: runRadioScanner();    break;
            case  2: runRfBaseline();      break;
            case  3: runNRFDiagnostics();  break;
        }
    }
}

static void handlerBT() {
    static const char* btItems[] = {
        "BLE Defense",
        "BLE Scanner",
        "BLE Spam",
        "BT Disruptor",
        "BT Jammer"
    };

    bool exitSub = false;
    while (!exitSub) {
        int choice = runSubMenuStyled("BLUETOOTH LAB", btItems,
                                      sizeof(btItems) / sizeof(char*),
                                      CAROUSEL_COLORS[2],
                                      SubMenuIconSet::BLUETOOTH);
        switch (choice) {
            case -1: exitSub = true;   break;
            case  0: runBLEAudit();    break;
            case  1: runBLEScanner();  break;
            case  2: runBLESpam();     break;
            case  3: runBTDisruptor(); break;
            case  4: runBTJammer();    break;
        }
    }
}

static void handlerMonitor() {
    runPacketMonitor();
}

static void handlerSystem() {
    static const char* systemItems[] = {
        "Mission Dash",
        "Audit Reports",
        "Settings",
        "System Info",
        "NRF Diagnostic",
        "GPS Tools",
        "MicroSD Manager",
        "MicroSD Info",
        "Battery ADC",
        "Clock & Weather",
        "About"
    };

    bool exitSub = false;
    while (!exitSub) {
        int choice = runSubMenuStyled("SYSTEM TOOLS", systemItems,
                                      sizeof(systemItems) / sizeof(char*),
                                      CAROUSEL_COLORS[4],
                                      SubMenuIconSet::SYSTEM);
        switch (choice) {
            case -1: exitSub = true;      break;
            case  0: runMissionDashboard(); break;
            case  1: runAuditReports();   break;
            case  2: runSettings();       break;
            case  3: runSystemInfo();     break;
            case  4: runNRFDiagnostics(); break;
            case  5: runGpsTools();       break;
            case  6: runSdFileBrowser();  break;
            case  7: runSdStatus();       break;
            case  8: runBatteryStatus();  break;
            case  9: runClockWeather();   break;
            case 10: runAbout();          break;
        }
    }
}

void runMainMenu() {
    redrawMainMenu(false);

    unsigned long lastPress = 0;
    unsigned long lastActivity = millis();

    while (true) {
        NavAction action = readNavAction(80);

        if (action == NAV_UP && (millis() - lastPress > 55)) {
            int prev = (currentEntry - 1 + MAIN_COUNT) % MAIN_COUNT;
            beep(2200, 25);
            changeMainEntry(prev);
            lastPress = millis();
            lastActivity = millis();
        }

        if (action == NAV_DOWN && (millis() - lastPress > 55)) {
            int next = (currentEntry + 1) % MAIN_COUNT;
            beep(2200, 25);
            changeMainEntry(next);
            lastPress = millis();
            lastActivity = millis();
        }

        if (action == NAV_ENTER && (millis() - lastPress > 220)) {
            tft.startWrite();
            drawMainRow(currentEntry, true, true);
            tft.endWrite();

            beep(1800, 40);
            delay(80);
            while (isEnterPressed() || isBackPressed()) delay(5);

            MAIN_ENTRIES[currentEntry].handler();

            redrawMainMenu(false);
            lastPress = millis();
            lastActivity = millis();
        }

        if (millis() - lastActivity > SCREENSAVER_IDLE_MS) {
            runScreensaver();
            redrawMainMenu(false);
            lastActivity = millis();
            lastPress = millis();
        }

        delay(10);
    }
}

static int runSubMenuStyled(const char* title, const char* items[], int count,
                            uint16_t accent, SubMenuIconSet iconSet) {
    const bool themed = iconSet != SubMenuIconSet::NONE;
    const int VISIBLE = themed ? 3 : 5;
    const int LINE_H = themed ? 52 : 30;
    const int LIST_Y = themed ? 46 : 50;

    if (count <= 0) return -1;

    int totalItems = count;
    SubMenuState* state = stateForSubMenu(title);
    int cursor = state->cursor;
    int scrollOffset = state->scrollOffset;
    int result = -2;
    unsigned long lastPress = 0;

    if (cursor < 0) cursor = 0;
    if (cursor >= totalItems) cursor = totalItems - 1;

    int maxScroll = totalItems > VISIBLE ? totalItems - VISIBLE : 0;
    if (scrollOffset < 0) scrollOffset = 0;
    if (scrollOffset > maxScroll) scrollOffset = maxScroll;
    if (cursor < scrollOffset) scrollOffset = cursor;
    if (cursor >= scrollOffset + VISIBLE) scrollOffset = cursor - VISIBLE + 1;

    auto rememberPosition = [&]() {
        state->cursor = cursor;
        state->scrollOffset = scrollOffset;
    };

    auto drawHeaderFooter = [&]() {
        if (!themed) {
            drawFrame();
            drawStringBig(12, 8, title, MOD_TEXT, 1);
            drawBitmapRight(306, 13, String(count) + " ITEMS", MOD_TEXT, 1, FONT_SMALL);
            drawStringCustom(10, 218, "UP/DN: MOVE", MOD_TEXT, 1);
            drawBitmapRight(310, 218, "OK:HOLD BACK", MOD_TEXT, 1, FONT_SMALL);
            return;
        }

        tft.fillScreen(MOD_BG);
        tft.drawRoundRect(4, 4, 312, 232, 12, accent);
        tft.fillRoundRect(9, 9, 302, 30, 8, MOD_PANEL);
        tft.drawFastHLine(10, 41, 300, accent);
        tft.drawFastHLine(10, 207, 300, accent);
        drawStringBig(14, 12, title, MOD_TEXT, 1);
        const char* section = "WIRELESS OPS";
        if (iconSet == SubMenuIconSet::RADIO) section = "RF LAB";
        else if (iconSet == SubMenuIconSet::BLUETOOTH) section = "BLE LAB";
        else if (iconSet == SubMenuIconSet::SYSTEM) section = "DEVICE CTRL";
        else if (iconSet == SubMenuIconSet::GPS) section = "NAVIGATION";
        else if (iconSet == SubMenuIconSet::STORAGE) section = "STORAGE";
        else if (iconSet == SubMenuIconSet::REPORTS) section = "EXPORTS";
        drawStringCustom(128, 17, section, accent, 1);
        drawBitmapRight(302, 17, String(count) + " ITEMS", accent, 1, FONT_SMALL);
        drawStringCustom(14, 218, "UP/DN: MOVE", MOD_TEXT, 1);
        drawBitmapRight(304, 218, "OK:OPEN  HOLD:BACK", accent, 1, FONT_SMALL);
    };

    auto drawItem = [&](int idx, int row, bool selected) {
        if (row < 0 || row >= VISIBLE) return;

        int y = LIST_Y + row * LINE_H;
        String label = String(items[idx]);

        if (!themed) {
            uint16_t bg = selected ? MOD_TEXT : MOD_BG;
            uint16_t fg = selected ? MOD_INVERT : MOD_TEXT;
            tft.fillRect(10, y - 5, 300, LINE_H - 3, bg);
            tft.drawRect(10, y - 5, 300, LINE_H - 3, MOD_LINE);
            drawStringCustom(22, y + 2, label, fg, 2);
            return;
        }

        const int cardY = y;
        const int cardH = 47;
        uint16_t bg = selected ? accent : MOD_PANEL;
        uint16_t fg = selected ? MOD_INVERT : MOD_TEXT;
        uint16_t iconColor = selected ? MOD_INVERT : accent;
        uint16_t iconDetail = selected ? MOD_PANEL : MOD_MUTED;

        // Clear one slot only; movement stays fluid without a full-screen flash.
        tft.fillRect(8, cardY, 303, LINE_H, MOD_BG);
        tft.fillRoundRect(10, cardY + 2, 298, cardH, 8, bg);
        tft.drawRoundRect(10, cardY + 2, 298, cardH, 8, accent);
        tft.fillRoundRect(16, cardY + 8, 38, 35, 7,
                          selected ? accent : MOD_BG);
        tft.drawRoundRect(16, cardY + 8, 38, 35, 7,
                          selected ? MOD_INVERT : accent);

        if (iconSet == SubMenuIconSet::WIFI) {
            uint16_t iconBackground = selected ? accent : MOD_BG;
            drawWifiToolIcon(idx, 35, cardY + 25, iconColor, iconDetail,
                             iconBackground);
        } else if (iconSet == SubMenuIconSet::RADIO) {
            uint16_t iconBackground = selected ? accent : MOD_BG;
            drawRadioToolIcon(idx, 35, cardY + 25, iconColor, iconDetail,
                              iconBackground);
        } else if (iconSet == SubMenuIconSet::BLUETOOTH) {
            uint16_t iconBackground = selected ? accent : MOD_BG;
            drawBluetoothToolIcon(idx, 35, cardY + 25, iconColor, iconDetail,
                                  iconBackground);
        } else if (iconSet == SubMenuIconSet::SYSTEM) {
            uint16_t iconBackground = selected ? accent : MOD_BG;
            drawSystemToolIcon(idx, 35, cardY + 25, iconColor, iconDetail,
                               iconBackground);
        } else if (iconSet == SubMenuIconSet::GPS) {
            drawGpsToolIcon(idx, 35, cardY + 25, iconColor, iconDetail,
                            selected ? accent : MOD_BG);
        } else if (iconSet == SubMenuIconSet::STORAGE) {
            drawStorageToolIcon(idx, 35, cardY + 25, iconColor, iconDetail,
                                selected ? accent : MOD_BG);
        } else if (iconSet == SubMenuIconSet::REPORTS) {
            drawReportToolIcon(idx, 35, cardY + 25, iconColor, iconDetail,
                               selected ? accent : MOD_BG);
        }

        String number = idx < 9 ? "0" + String(idx + 1) : String(idx + 1);
        drawStringCustom(65, cardY + 10, number, iconColor, 1);
        drawStringCustom(65, cardY + 24, label, fg, 2);
        drawStringCustom(287, cardY + 22, selected ? ">" : ".", iconColor, 1);
    };

    auto drawScrollBar = [&]() {
        int trackY = themed ? 50 : 42;
        int trackH = themed ? 146 : 162;
        int trackX = themed ? 312 : 313;
        tft.fillRect(trackX, trackY, 3, trackH, MOD_BG);
        if (totalItems <= VISIBLE) return;

        int barH = (VISIBLE * trackH) / totalItems;
        int barY = trackY + (scrollOffset * (trackH - barH)) /
                              (totalItems - VISIBLE);
        tft.fillRect(trackX, barY, 3, barH, themed ? accent : MOD_TEXT);
    };

    auto drawVisible = [&]() {
        for (int row = 0; row < VISIBLE; row++) {
            int idx = scrollOffset + row;
            if (idx < totalItems) {
                drawItem(idx, row, idx == cursor);
            } else {
                int y = LIST_Y + row * LINE_H;
                if (themed) tft.fillRect(8, y, 303, LINE_H, MOD_BG);
                else tft.fillRect(10, y - 5, 300, LINE_H - 3, MOD_BG);
            }
        }
        drawScrollBar();
    };

    auto redrawMove = [&](int oldCursor, int oldScrollOffset) {
        if (oldScrollOffset != scrollOffset) {
            drawVisible();
            return;
        }

        int oldRow = oldCursor - scrollOffset;
        int newRow = cursor - scrollOffset;
        if (oldRow >= 0 && oldRow < VISIBLE) {
            drawItem(oldCursor, oldRow, false);
        }
        if (newRow >= 0 && newRow < VISIBLE) {
            drawItem(cursor, newRow, true);
        }
        drawScrollBar();
    };

    while (isEnterPressed() || isBackPressed()) delay(5);
    delay(80);
    flushNavInput();

    tft.startWrite();
    drawHeaderFooter();
    drawVisible();
    tft.endWrite();

    while (result == -2) {
        NavAction action = readNavAction(60);

        if (action == NAV_UP && (millis() - lastPress > 45)) {
            int oldCursor = cursor;
            int oldScrollOffset = scrollOffset;
            cursor = (cursor - 1 + totalItems) % totalItems;
            if (cursor < scrollOffset) scrollOffset = cursor;
            if (cursor >= scrollOffset + VISIBLE) scrollOffset = cursor - VISIBLE + 1;
            rememberPosition();

            beep(2200, 15);
            tft.startWrite();
            redrawMove(oldCursor, oldScrollOffset);
            tft.endWrite();
            lastPress = millis();
        }

        if (action == NAV_DOWN && (millis() - lastPress > 45)) {
            int oldCursor = cursor;
            int oldScrollOffset = scrollOffset;
            cursor = (cursor + 1) % totalItems;
            if (cursor < scrollOffset) scrollOffset = cursor;
            if (cursor >= scrollOffset + VISIBLE) scrollOffset = cursor - VISIBLE + 1;
            rememberPosition();

            beep(2200, 15);
            tft.startWrite();
            redrawMove(oldCursor, oldScrollOffset);
            tft.endWrite();
            lastPress = millis();
        }

        if (action == NAV_BACK) {
            rememberPosition();
            beep(1000, 40);
            result = -1;
            lastPress = millis();
        }

        if (action == NAV_ENTER && (millis() - lastPress > 180)) {
            bool held = waitOkReleaseWasLong();
            rememberPosition();
            beep(held ? 1000 : 1500, 40);
            result = held ? -1 : cursor;
            lastPress = millis();
        }

        delay(4);
    }

    while (isEnterPressed() || isBackPressed()) delay(5);
    delay(60);

    rememberPosition();
    return result;
}

int runSubMenu(const char* title, const char* items[], int count) {
    return runSubMenuStyled(title, items, count, MOD_TEXT,
                            SubMenuIconSet::NONE);
}

int runSystemSubMenu(const char* title, const char* items[], int count,
                     SystemSubMenuStyle style) {
    SubMenuIconSet icons = SubMenuIconSet::SYSTEM;
    if (style == SystemSubMenuStyle::GPS) icons = SubMenuIconSet::GPS;
    else if (style == SystemSubMenuStyle::STORAGE) icons = SubMenuIconSet::STORAGE;
    else if (style == SystemSubMenuStyle::REPORTS) icons = SubMenuIconSet::REPORTS;
    return runSubMenuStyled(title, items, count, CAROUSEL_COLORS[4], icons);
}
