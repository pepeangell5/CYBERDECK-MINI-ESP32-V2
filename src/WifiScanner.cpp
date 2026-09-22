#include "WifiScanner.h"
#include "PepeDraw.h"
#include "Pins.h"
#include "SoundUtils.h"
#include "WifiUi.h"

// ═════════════════════════════════════════════════════════════════════════════
// CONFIGURACIÓN
// ═════════════════════════════════════════════════════════════════════════════
#define MAX_NETWORKS     30    // Tope de redes (protege el stack)
#define VISIBLE_LINES    4     // Tarjetas grandes visibles en la lista

// ═════════════════════════════════════════════════════════════════════════════
// ESTRUCTURA DE RED
// ═════════════════════════════════════════════════════════════════════════════
struct NetInfo {
    String ssid;
    int channel;
    int rssi;
    String bssid;
    uint8_t authType;   // guardamos el tipo crudo para evaluar seguridad
};

// ═════════════════════════════════════════════════════════════════════════════
// TABLA OUI — Fabricantes comunes en México
// (formato: 0xAABBCC = primeros 3 bytes de la MAC)
// Cubre routers de Telmex, Totalplay, Megacable, Izzi, AT&T + dispositivos
// ═════════════════════════════════════════════════════════════════════════════
struct OuiEntry {
    uint32_t prefix;
    const char* vendor;
};

static const OuiEntry OUI_TABLE[] = {
    // ── Huawei (Telmex / Totalplay / AT&T) ──
    {0x001882, "Huawei"},   {0x00E0FC, "Huawei"},   {0x0CFC18, "Huawei"},
    {0xE00630, "Huawei"},   {0xF0A0B1, "Huawei"},   {0x404F42, "Huawei"},
    {0x7066B9, "Huawei"},   {0xC4A1AE, "Huawei"},   {0x001E10, "Huawei"},
    {0x002568, "Huawei"},   {0x70723C, "Huawei"},   {0xACE215, "Huawei"},

    // ── Sercomm (Telmex Infinitum modems) ──
    {0x0022F7, "Sercomm"},  {0x14C03E, "Sercomm"},  {0xA84E3F, "Sercomm"},
    {0x5004B8, "Sercomm"},  {0x74884E, "Sercomm"},

    // ── Arcadyan (Telmex antiguo, AT&T) ──
    {0x0015E0, "Arcadyan"}, {0x002308, "Arcadyan"}, {0x60310F, "Arcadyan"},
    {0x507E5D, "Arcadyan"},

    // ── Askey (Totalplay HGU) ──
    {0x001D7E, "Askey"},    {0xC85195, "Askey"},    {0x889676, "Askey"},

    // ── Nokia / Alcatel (Totalplay) ──
    {0x00603E, "Nokia"},    {0x1CD446, "Nokia"},    {0xF8E4FB, "Nokia"},
    {0x00194B, "Nokia"},    {0x80D4A5, "Nokia"},

    // ── Fiberhome (Totalplay) ──
    {0x3086F1, "Fiberhome"},{0xD876E7, "Fiberhome"},

    // ── Arris (Megacable, Izzi cable) ──
    {0x0015CE, "Arris"},    {0x0026CE, "Arris"},    {0x54A619, "Arris"},
    {0xB83F5D, "Arris"},    {0x00248C, "Arris"},

    // ── Technicolor (Izzi, Megacable) ──
    {0x00147F, "Technicolor"},{0x002417, "Technicolor"},{0xC42795, "Technicolor"},
    {0x6C5697, "Technicolor"},{0xD44B5E, "Technicolor"},

    // ── Hitron (Megacable) ──
    {0x008A5A, "Hitron"},   {0x688F2E, "Hitron"},   {0x749D8F, "Hitron"},

    // ── ZTE ──
    {0x0015EB, "ZTE"},      {0x344B50, "ZTE"},      {0xD0154A, "ZTE"},
    {0x4C16F1, "ZTE"},

    // ── TP-Link (retail muy común) ──
    {0x14D864, "TP-Link"},  {0x40ED00, "TP-Link"},  {0x68DDB7, "TP-Link"},
    {0x002719, "TP-Link"},  {0x14CC20, "TP-Link"},  {0x50C7BF, "TP-Link"},
    {0xE894F6, "TP-Link"},  {0xA842A1, "TP-Link"},

    // ── Mercusys (TP-Link económico) ──
    {0x50D4F7, "Mercusys"}, {0xB4B024, "Mercusys"},

    // ── Xiaomi / Mi Router ──
    {0x8CBEBE, "Xiaomi"},   {0x28E31F, "Xiaomi"},   {0x508F4C, "Xiaomi"},
    {0xF0B429, "Xiaomi"},   {0x64B473, "Xiaomi"},   {0x74510F, "Xiaomi"},

    // ── Apple ──
    {0x000393, "Apple"},    {0xACBC32, "Apple"},    {0xF45C89, "Apple"},
    {0xA45E60, "Apple"},    {0x001B63, "Apple"},

    // ── Samsung ──
    {0x0012FB, "Samsung"},  {0x34BE00, "Samsung"},  {0x5C0A5B, "Samsung"},
    {0xE8508B, "Samsung"},  {0x001D25, "Samsung"},
};
static const int OUI_COUNT = sizeof(OUI_TABLE) / sizeof(OuiEntry);

// ═════════════════════════════════════════════════════════════════════════════
// HELPERS
// ═════════════════════════════════════════════════════════════════════════════

// Convierte "AA:BB:CC:DD:EE:FF" → 0xAABBCC (primeros 3 bytes)
static uint32_t macToOui(const String& mac) {
    if (mac.length() < 8) return 0;
    char buf[9];
    buf[0] = mac[0]; buf[1] = mac[1];
    buf[2] = mac[3]; buf[3] = mac[4];
    buf[4] = mac[6]; buf[5] = mac[7];
    buf[6] = '\0';
    return (uint32_t) strtoul(buf, nullptr, 16);
}

// Lookup en la tabla OUI
static String lookupVendor(const String& mac) {
    uint32_t oui = macToOui(mac);
    for (int i = 0; i < OUI_COUNT; i++) {
        if (OUI_TABLE[i].prefix == oui) return String(OUI_TABLE[i].vendor);
    }
    return "Unknown";
}

// Texto legible de encriptación
static String authToString(uint8_t type) {
    switch (type) {
        case WIFI_AUTH_OPEN:            return "OPEN";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/3";
        default:                        return "?";
    }
}

// Abreviatura corta (2-3 chars) para la lista
static String authToShort(uint8_t type) {
    switch (type) {
        case WIFI_AUTH_OPEN:            return "OP";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "W1";
        case WIFI_AUTH_WPA2_PSK:        return "W2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "W2";
        case WIFI_AUTH_WPA3_PSK:        return "W3";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "W3";
        default:                        return "?";
    }
}

// Color según nivel de seguridad
static uint16_t authToColor(uint8_t type) {
    switch (type) {
        case WIFI_AUTH_OPEN:            return TFT_RED;
        case WIFI_AUTH_WEP:             return TFT_ORANGE;
        case WIFI_AUTH_WPA_PSK:         return TFT_YELLOW;
        case WIFI_AUTH_WPA2_PSK:
        case WIFI_AUTH_WPA_WPA2_PSK:    return TFT_GREEN;
        case WIFI_AUTH_WPA3_PSK:
        case WIFI_AUTH_WPA2_WPA3_PSK:   return TFT_CYAN;
        default:                        return TFT_WHITE;
    }
}

// Canal WiFi → frecuencia en MHz
static int channelToFreq(int ch) {
    if (ch >= 1 && ch <= 13) return 2407 + ch * 5;
    if (ch == 14) return 2484;
    return 0;
}

// RSSI → 0-4 barras
static int rssiToBars(int rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -67) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
}

// Color de las barras según fuerza
static uint16_t barsColor(int bars) {
    if (bars >= 3) return TFT_GREEN;
    if (bars == 2) return TFT_YELLOW;
    return TFT_RED;
}

// Dibuja 4 bloques verticales tipo señal celular
static void drawSignalBars(int x, int y, int bars) {
    const int bw = 3;       // ancho de cada bloque
    const int gap = 2;      // separación
    const int heights[4] = {4, 8, 12, 16};
    uint16_t onColor = barsColor(bars);
    for (int i = 0; i < 4; i++) {
        int bx = x + i * (bw + gap);
        int by = y + (16 - heights[i]);
        if (i < bars) tft.fillRect(bx, by, bw, heights[i], onColor);
        else          tft.drawRect(bx, by, bw, heights[i], UI_ACCENT);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// PANTALLA DE DETALLES (mejorada)
// ═════════════════════════════════════════════════════════════════════════════
static void showDetails(const NetInfo& net) {

    ledcWriteTone(0, 0);

    wifiUiFrame("NETWORK DETAILS", "AP INFO", WIFI_UI_OK);
    int y = 50;

    // SSID (con detección de oculta)
    bool hidden = (net.ssid.length() == 0);
    String displaySsid = hidden ? "<HIDDEN>" : net.ssid;
    wifiUiCard(10, y - 3, 300, 38, false);
    drawStringCustom(18, y + 3, "SSID", WIFI_UI_MUTED, 1);
    if (!hidden && getTextWidth(displaySsid, 2) > 300) {
        drawStringFit(82, y + 8, displaySsid, TFT_WHITE, 216, 1);
    } else {
        drawStringFit(82, y + 8, displaySsid,
                      hidden ? WIFI_UI_DANGER : TFT_WHITE, 216, 1);
    }
    y += 42;

    // Canal + frecuencia
    String chStr = "CH " + String(net.channel) + "  " +
                   String(channelToFreq(net.channel)) + " MHz";
    wifiUiMetric(10, y, 145, "CHANNEL", chStr, WIFI_UI_ACCENT);

    // RSSI + barras
    wifiUiMetric(165, y, 145, "SIGNAL", String(net.rssi) + " dBm",
                 barsColor(rssiToBars(net.rssi)));
    drawSignalBars(274, y + 18, rssiToBars(net.rssi));
    y += 44;

    // MAC + fabricante
    String vendor = lookupVendor(net.bssid);
    wifiUiCard(10, y, 300, 30, false);
    drawStringCustom(18, y + 6, "BSSID", WIFI_UI_MUTED, 1);
    drawStringCustom(74, y + 6, net.bssid, WIFI_UI_WARN, 1);
    drawStringCustom(220, y + 17, vendor,
                     vendor == "Unknown" ? WIFI_UI_MUTED : WIFI_UI_ACCENT, 1);
    y += 35;

    // Seguridad + rating de color
    wifiUiCard(10, y, 300, 30, false, authToColor(net.authType));
    drawStringCustom(18, y + 6, "SECURITY", WIFI_UI_MUTED, 1);
    drawStringCustom(96, y + 6, authToString(net.authType),
                     authToColor(net.authType), 2);
    wifiUiFooter("NETWORK DETAILS", "OK/BACK: RETURN");

    delay(400);
    while (!navEnterPressed() && !navBackPressed()) delay(10);
    while (navEnterPressed() || navBackPressed()) delay(5);
    delay(200);
    ledcWriteTone(0, 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// ANIMACIÓN DE SCANNING
// ═════════════════════════════════════════════════════════════════════════════
static void drawScanningAnim(int tick) {
    wifiUiScanning("WIFI SCANNER", "SEARCHING 2.4 GHZ NETWORKS", tick);
}

// ═════════════════════════════════════════════════════════════════════════════
// LISTA DE REDES (con scroll bar + barras + encryption color)
// ═════════════════════════════════════════════════════════════════════════════
static void drawNetworkRow(const NetInfo* nets, int n, int idx, int row,
                           bool selected) {
    int y = 48 + row * 39;
    tft.fillRect(8, y, 302, 38, WIFI_UI_BG);
    if (idx < 0 || idx >= n) return;

    const NetInfo& net = nets[idx];
    wifiUiCard(10, y + 1, 298, 35, selected);
    uint16_t fg = selected ? TFT_BLACK : WIFI_UI_TEXT;
    uint16_t sub = selected ? WIFI_UI_PANEL : WIFI_UI_MUTED;
    drawSignalBars(19, y + 10, rssiToBars(net.rssi));
    String label = net.ssid.length() == 0 ? "<HIDDEN>" : net.ssid;
    drawStringFit(50, y + 5, label,
                  net.ssid.length() == 0 ? WIFI_UI_DANGER : fg, 202, 1);
    drawStringCustom(50, y + 20,
                     "CH" + String(net.channel) + "  " + String(net.rssi) + " dBm",
                     sub, 1);
    uint16_t enc = selected ? TFT_BLACK : authToColor(net.authType);
    drawStringCustom(270, y + 12, authToShort(net.authType), enc, 1);
}

static void drawNetworkScroll(int n, int scrollOffset) {
    tft.fillRect(312, 50, 3, 146, WIFI_UI_BG);
    int totalEntries = n;
    if (totalEntries > VISIBLE_LINES) {
        int barH = max(18, (VISIBLE_LINES * 146) / totalEntries);
        int barY = 50 + (scrollOffset * (146 - barH)) /
                          (totalEntries - VISIBLE_LINES);
        tft.fillRect(312, barY, 3, barH, WIFI_UI_ACCENT);
    }
}

static void drawList(const NetInfo* nets, int n, int cursor, int scrollOffset) {
    wifiUiFrame("WIFI SCANNER", String(n) + " AP", WIFI_UI_OK);
    for (int row = 0; row < VISIBLE_LINES; row++) {
        int idx = scrollOffset + row;
        drawNetworkRow(nets, n, idx, row, idx == cursor);
    }
    drawNetworkScroll(n, scrollOffset);
    wifiUiFooter("UP/DN: MOVE", "OK: DETAILS");
}

static void redrawListMove(const NetInfo* nets, int n, int oldCursor,
                           int cursor, int oldScroll, int scrollOffset) {
    if (oldScroll != scrollOffset) {
        drawList(nets, n, cursor, scrollOffset);
        return;
    }
    drawNetworkRow(nets, n, oldCursor, oldCursor - scrollOffset, false);
    drawNetworkRow(nets, n, cursor, cursor - scrollOffset, true);
    drawNetworkScroll(n, scrollOffset);
}

// ═════════════════════════════════════════════════════════════════════════════
// MAIN — SCAN DE REDES
// ═════════════════════════════════════════════════════════════════════════════
void runWifiScan() {

#if BUZZER_PIN >= 0
    // Init buzzer
    ledcSetup(0, 2000, 8);
    ledcAttachPin(BUZZER_PIN, 0);
    ledcWriteTone(0, 0);
#endif

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // The ESP32 Arduino async scanner proved unreliable after returning from
    // promiscuous-mode tools.  Draw one stable search frame and use the proven
    // blocking scan so results are always complete before they are copied.
    WiFi.scanDelete();
    wifiUiScanAnimationStart("WIFI SCANNER", "SEARCHING 2.4 GHZ NETWORKS");
    int n = WiFi.scanNetworks(false, true);
    wifiUiScanAnimationStop();

    if (n <= 0) {
        wifiUiEmpty("WIFI SCANNER", "NO NETWORKS FOUND",
                    "MOVE CLOSER OR TRY AGAIN");
        delay(1600);
        return;
    }

    // 🛡️ Tope de seguridad (protege el stack)
    if (n > MAX_NETWORKS) n = MAX_NETWORKS;

    NetInfo networks[MAX_NETWORKS];
    for (int i = 0; i < n; i++) {
        networks[i].ssid     = WiFi.SSID(i);
        networks[i].channel  = WiFi.channel(i);
        networks[i].rssi     = WiFi.RSSI(i);
        networks[i].bssid    = WiFi.BSSIDstr(i);
        networks[i].authType = WiFi.encryptionType(i);
    }

    // Liberar memoria interna del driver WiFi
    WiFi.scanDelete();

    // Ordenar por RSSI descendente (señal fuerte primero)
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - 1 - i; j++) {
            if (networks[j].rssi < networks[j + 1].rssi) {
                NetInfo tmp     = networks[j];
                networks[j]     = networks[j + 1];
                networks[j + 1] = tmp;
            }
        }
    }

    // ── UI principal ─────────────────────────────────────────────────────────
    int cursor = 0;
    int scrollOffset = 0;
    bool exitScan = false;
    bool needsRedraw = true;

    tft.fillScreen(TFT_BLACK);

    while (!exitScan) {

        if (navBackPressed() || isBackPressed()) {
            exitScan = true;
            while (isBackPressed()) delay(5);
            flushNavInput(60);
            continue;
        }

        if (needsRedraw) {
            drawList(networks, n, cursor, scrollOffset);
            needsRedraw = false;
        }

        // DOWN
        if (navDownPressed()) {
            int oldCursor = cursor;
            int oldScrollOffset = scrollOffset;
            cursor = (cursor + 1) % n;
            if (cursor < scrollOffset) scrollOffset = cursor;
            if (cursor >= scrollOffset + VISIBLE_LINES)
                scrollOffset = cursor - VISIBLE_LINES + 1;
            tft.startWrite();
            redrawListMove(networks, n, oldCursor, cursor,
                           oldScrollOffset, scrollOffset);
            tft.endWrite();
            needsRedraw = false;
            beep(2000, 30);
            delay(70);
        }

        // UP
        if (navUpPressed()) {
            int oldCursor = cursor;
            int oldScrollOffset = scrollOffset;
            cursor = (cursor + n - 1) % n;
            if (cursor < scrollOffset) scrollOffset = cursor;
            if (cursor >= scrollOffset + VISIBLE_LINES)
                scrollOffset = cursor - VISIBLE_LINES + 1;
            tft.startWrite();
            redrawListMove(networks, n, oldCursor, cursor,
                           oldScrollOffset, scrollOffset);
            tft.endWrite();
            needsRedraw = false;
            beep(2000, 30);
            delay(70);
        }

        // OK
        if (navEnterPressed()) {
            bool held = waitOkReleaseWasLong();
            if (held) {
                exitScan = true;
            } else {
                beep(1200, 50);
                showDetails(networks[cursor]);
                tft.fillScreen(TFT_BLACK);
                needsRedraw = true;
            }
            delay(250);
        }

        delay(10);
    }

    ledcWriteTone(0, 0);
}
