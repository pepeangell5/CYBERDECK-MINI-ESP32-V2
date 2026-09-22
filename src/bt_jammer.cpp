#include "bt_jammer.h"

#include <esp_system.h>
#include <RF24.h>
#include <SPI.h>

#include "DisplayTFT.h"
#include "Input.h"
#include "PepeDraw.h"
#include "Pins.h"
#include "SharedSpi.h"
#include "BtUi.h"

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
static unsigned long btLastUiAnimMs = 0;

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
    sharedSpiPrepareDisplay(!isBtJamming);
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

static void updateBtJammerAnimation() {
    if (!isBtJamming || millis() - btLastUiAnimMs < 240) return;
    btLastUiAnimMs = millis();

    // Constant carrier needs no continuous SPI traffic. Pause only for the
    // tiny partial redraw, then restore both carriers on their current channels.
    if (btJam1Ok) btJam1.stopConstCarrier();
    if (btJam2Ok) btJam2.stopConstCarrier();
    sharedSpiPrepareDisplay(true);

    uint8_t frame = btFrame++;
    tft.fillRect(18, 160, 284, 31, BT_UI_PANEL);
    for (int i = 0; i < 18; i++) {
        int h = 4 + ((frame * 5 + i * 7) % 25);
        uint16_t c = ((i + frame) % 3 == 0) ? BT_UI_GLOW : BT_UI_ACCENT;
        tft.fillRect(26 + i * 15, 190 - h, 8, h, c);
    }

    tft.fillRect(112, 119, 184, 14, BT_UI_PANEL);
    drawStringCustom(112, 122,
                     "NRF CH " + String(btCh1) + " / " + String(btCh2),
                     BT_UI_GLOW, 1);

    sharedSpiPrepareRadio(true);
    if (btJam1Ok) {
        configureBtRadio(btJam1);
        btJam1.startConstCarrier(RF24_PA_MAX, btCh1);
    }
    if (btJam2Ok) {
        configureBtRadio(btJam2);
        btJam2.startConstCarrier(RF24_PA_MAX, btCh2);
    }
}

static void drawBtGlyph(int x, int y, uint8_t frame) {
    tft.drawLine(x + 16, y + 4, x + 16, y + 52, BT_UI_GLOW);
    tft.drawLine(x + 16, y + 4, x + 36, y + 16, BT_UI_GLOW);
    tft.drawLine(x + 36, y + 16, x + 16, y + 28, BT_UI_GLOW);
    tft.drawLine(x + 16, y + 28, x + 36, y + 40, BT_UI_GLOW);
    tft.drawLine(x + 36, y + 40, x + 16, y + 52, BT_UI_GLOW);
    tft.drawLine(x + 4, y + 16, x + 48, y + 44, BT_UI_TEXT);
    tft.drawLine(x + 4, y + 40, x + 48, y + 12, BT_UI_TEXT);
    if ((frame / 4) % 2 == 0) {
        tft.drawCircle(x + 16, y + 28, 26, BT_UI_ACCENT);
        tft.drawCircle(x + 16, y + 28, 32, BT_UI_GLOW);
    }
}

static void drawBtScreen() {
    uint8_t frame = btFrame++;
    clearBtScreen();
    btUiFrame("BT JAMMER", isBtJamming ? "ACTIVE" : "READY",
              isBtJamming ? BT_UI_DANGER : BT_UI_OK);
    btUiCard(10, 49, 300, 96, false,
             isBtJamming ? BT_UI_DANGER : BT_UI_ACCENT);

    drawBtGlyph(25, 63, frame);
    drawStringBig(111, 60, isBtJamming ? "WIDEBAND" : "2.4 GHz LAB",
                  BT_UI_TEXT, 1);
    drawStringCustom(112, 83,
                     isBtJamming ? "CHANNEL HOPPING" : "RADIOS READY",
                     isBtJamming ? BT_UI_DANGER : BT_UI_OK, 1);
    drawStringCustom(112, 103,
                     "RADIOS " + String(activeBtRadioCount()) + "/" +
                     String(BT_JAMMER_RADIO_COUNT),
                     activeBtRadioCount() > 0 ? BT_UI_OK : BT_UI_DANGER, 1);
    if (isBtJamming) {
        drawStringCustom(112, 122,
                         "NRF CH " + String(btCh1) + " / " + String(btCh2),
                         BT_UI_GLOW, 1);
    }

    btUiCard(10, 152, 300, 47, false,
             isBtJamming ? BT_UI_DANGER : BT_UI_LINE);
    if (isBtJamming) {
        tft.fillRect(18, 160, 284, 31, BT_UI_PANEL);
        for (int i = 0; i < 18; i++) {
            int h = 4 + ((frame + i * 3) % 25);
            uint16_t c = (i % 3 == 0) ? BT_UI_GLOW : BT_UI_ACCENT;
            tft.fillRect(26 + i * 15, 190 - h, 8, h, c);
        }
    } else {
        drawStringCentered(168, "OK TO START AUTHORIZED TEST",
                           BT_UI_OK, 1, FONT_SMALL);
    }

    btUiFooter("OK: TOGGLE", "HOLD/BACK: EXIT",
               isBtJamming ? BT_UI_DANGER : BT_UI_ACCENT);
}

void btJammerSetup() {
    randomSeed(esp_random());

    sharedSpiInitPins(true);
    sharedSpiBeginMainBus();
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
        updateBtJammerAnimation();
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
