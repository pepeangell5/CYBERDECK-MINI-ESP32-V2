#include "jammer.h"

#include <RF24.h>
#include <SPI.h>

#include "DisplayTFT.h"
#include "Input.h"
#include "PepeDraw.h"
#include "Pins.h"
#include "SharedSpi.h"
#include "RfUi.h"

extern DisplayTFT tft;

#ifndef RADIO_JAMMER_SPI_SPEED
#define RADIO_JAMMER_SPI_SPEED 8000000
#endif

static RF24 jam1(NRF1_CE_PIN, NRF1_CSN_PIN, RADIO_JAMMER_SPI_SPEED);
static RF24 jam2(NRF2_CE_PIN, NRF2_CSN_PIN, RADIO_JAMMER_SPI_SPEED);
static bool jam1Ok = false;
static bool jam2Ok = false;

static int jamChannel = 1;
static bool isAttacking = false;
static bool exitRequested = false;

static void drawChannelGauge(bool full);

static const uint8_t noisePayload[32] = {
    0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA,
    0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA,
    0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA,
    0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA
};

static uint8_t wifiChannelToNrf(int channel) {
    return (uint8_t)((channel * 5) + 2);
}

static void configureRadio(RF24& radio) {
    radio.powerUp();
    radio.setAutoAck(false);
    radio.setRetries(0, 0);
    radio.setPayloadSize(32);
    radio.setAddressWidth(5);
    radio.setPALevel(RF24_PA_MAX, true);
    radio.setDataRate(RF24_2MBPS);
    radio.setCRCLength(RF24_CRC_DISABLED);
    radio.stopListening();
}

static int activeRadioCount() {
    return (jam1Ok ? 1 : 0) + (jam2Ok ? 1 : 0);
}

static void prepareJammerDisplay() {
    // Every TFT operation in this module is now performed while the radios
    // are stopped. Always force both CE pins low before touching the display.
    sharedSpiPrepareDisplay(true);
}

static void clearJammerScreen() {
    prepareJammerDisplay();
    tft.fillScreen(TFT_BLACK);
    delay(12);
    tft.fillRect(0, 0, 320, 240, TFT_BLACK);
}

static void stopAttack() {
    isAttacking = false;
    if (jam1Ok) jam1.stopConstCarrier();
    if (jam2Ok) jam2.stopConstCarrier();
}

static void pauseAttackCarriers() {
    if (jam1Ok) {
        jam1.stopConstCarrier();
        jam1.powerDown();
    }
    if (jam2Ok) {
        jam2.stopConstCarrier();
        jam2.powerDown();
    }
    sharedSpiPrepareDisplay(true);
    // Allow the PA/carrier to physically settle before starting a TFT SPI
    // transaction. A sub-millisecond wait was not sufficient on both radios.
    delay(4);
}

static void resumeAttackCarriers() {
    if (!isAttacking) return;
    const uint8_t freq = wifiChannelToNrf(jamChannel);
    if (jam1Ok) {
        jam1.powerUp();
        jam1.startConstCarrier(RF24_PA_MAX, freq);
    }
    if (jam2Ok) {
        jam2.powerUp();
        jam2.startConstCarrier(RF24_PA_MAX, freq);
    }
}

static void pauseCarrierCeForAnimation() {
    if (jam1Ok) digitalWrite(NRF1_CE_PIN, LOW);
#if NRF2_ENABLED
    if (jam2Ok) digitalWrite(NRF2_CE_PIN, LOW);
#endif
    delayMicroseconds(180);
    sharedSpiPrepareDisplay(true);
}

static void resumeCarrierCeAfterAnimation() {
    if (!isAttacking) return;
    if (jam1Ok) digitalWrite(NRF1_CE_PIN, HIGH);
#if NRF2_ENABLED
    if (jam2Ok) digitalWrite(NRF2_CE_PIN, HIGH);
#endif
}

static void changeActiveChannel(int direction) {
    const bool resumeAfterDraw = isAttacking;
    if (resumeAfterDraw) pauseAttackCarriers();

    if (direction > 0) {
        jamChannel = (jamChannel == 14) ? 1 : jamChannel + 1;
    } else {
        jamChannel = (jamChannel == 1) ? 14 : jamChannel - 1;
    }

    // Force both CE pins low while clearing and repainting the TFT. Leaving
    // constant carrier active made some display writes incomplete on the
    // shared SPI wiring, which accumulated the old CH digits.
    sharedSpiPrepareDisplay(true);
    // Use the same known-good full-screen path used while Jammer is idle.
    // Besides clearing all glyphs, this resets the TFT address window.
    drawChannelGauge(true);

    if (resumeAfterDraw) resumeAttackCarriers();
}

static void drawHeader(const char* title, const String& status, uint16_t color) {
    tft.fillRect(0, 0, 320, 36, color);
    tft.drawRect(0, 0, 320, 240, TFT_WHITE);
    drawStringBig(10, 10, title, color == TFT_WHITE ? TFT_BLACK : TFT_WHITE, 1);
    drawStringRight(305, 14, status, color == TFT_WHITE ? TFT_BLACK : TFT_WHITE, 1);
    tft.drawFastHLine(0, 36, 320, TFT_WHITE);
}

static void drawChannelBars();

static void drawChannelGauge(bool full = true) {
    if (full) {
        clearJammerScreen();
        rfUiFrame("RF CHANNEL", isAttacking ? "ACTIVE" : "READY",
                  isAttacking ? RF_UI_DANGER : RF_UI_OK);
        rfUiCard(10, 161, 300, 39, false,
                 isAttacking ? RF_UI_DANGER : RF_UI_LINE);
        rfUiFooter("UP/DN: CHANNEL",
                   isAttacking ? "OK: STOP  HOLD: BACK" : "OK: START",
                   isAttacking ? RF_UI_DANGER : RF_UI_OK);
    } else {
        prepareJammerDisplay();
    }

    // Rebuild the complete card instead of erasing only the text rectangle.
    // This guarantees that every scaled glyph pixel from the previous channel
    // is removed on the real TFT, including while returning from RF mode.
    rfUiCard(10, 49, 300, 105, false,
             isAttacking ? RF_UI_DANGER : RF_UI_ACCENT);
    drawStringCustom(18, 57, "SELECTED WIFI CHANNEL", RF_UI_MUTED, 1);
    drawStringCustom(18, 132, "RADIO MODULES", RF_UI_MUTED, 1);

    String chText = "CH " + String(jamChannel);
    drawStringCentered(72, chText, RF_UI_ACCENT, 3, FONT_BIG);
    drawStringCentered(111,
        String(2400 + wifiChannelToNrf(jamChannel)) + " MHz NRF",
        RF_UI_TEXT, 1, FONT_SMALL);

    int pct = 7 + ((jamChannel - 1) * 93) / 13;
    rfUiProgress(20, 123, 280, 9, pct,
                 isAttacking ? RF_UI_DANGER : RF_UI_OK);

    tft.fillRect(116, 133, 178, 12, RF_UI_PANEL);
    drawStringCustom(116, 134,
                     String(activeRadioCount()) + "/" + String(NRF_RADIO_COUNT),
                     activeRadioCount() > 0 ? RF_UI_OK : RF_UI_DANGER, 1);

    if (isAttacking) {
        drawChannelBars();
    } else {
        tft.fillRect(18, 168, 284, 25, RF_UI_PANEL);
        drawStringCentered(176, "READY FOR AUTHORIZED LAB", RF_UI_OK,
                           1, FONT_SMALL);
    }

}

static void drawChannelBars() {
    prepareJammerDisplay();
    tft.fillRect(18, 168, 284, 25, RF_UI_PANEL);
    uint8_t frame = (millis() / 70) & 0xFF;
    for (int i = 0; i < 28; i++) {
        int h = 3 + ((frame + i * 5) % 20);
        uint16_t c = h > 14 ? RF_UI_DANGER : RF_UI_ACCENT;
        tft.fillRect(20 + i * 10, 192 - h, 6, h, c);
    }
}

void jammerSetup() {
    sharedSpiInitPins(true);
    sharedSpiBeginMainBus();
    delay(100);

    jam1.begin();
#if NRF2_ENABLED
    jam2.begin();
#else
    jam2Ok = false;
#endif

    delay(500);

    bool jam1BeginOk = jam1.begin();
    jam1Ok = jam1BeginOk && jam1.isChipConnected();
    if (jam1Ok) configureRadio(jam1);

#if NRF2_ENABLED
    bool jam2BeginOk = jam2.begin();
    jam2Ok = jam2BeginOk && jam2.isChipConnected();
    if (jam2Ok) configureRadio(jam2);
#endif

    Serial.printf("[jammer] NRF1 CE:%d CSN:%d -> %s\n",
                  NRF1_CE_PIN, NRF1_CSN_PIN, jam1Ok ? "OK" : "FAIL");
#if NRF2_ENABLED
    Serial.printf("[jammer] NRF2 CE:%d CSN:%d -> %s\n",
                  NRF2_CE_PIN, NRF2_CSN_PIN, jam2Ok ? "OK" : "FAIL");
#else
    Serial.println("[jammer] NRF2 disabled");
#endif

    prepareJammerDisplay();
}

void jammerLoop() {
    static unsigned long lastBars = 0;
    NavAction action = readNavAction(130);

    if (action == NAV_BACK || isBackPressed()) {
        stopAttack();
        exitRequested = true;
        while (isBackPressed()) delay(5);
        flushNavInput(60);
        return;
    }

    if (action == NAV_UP) {
        changeActiveChannel(1);
    }

    if (action == NAV_DOWN) {
        changeActiveChannel(-1);
    }

    if (action == NAV_ENTER) {
        bool held = waitOkReleaseWasLong();
        if (held) {
            stopAttack();
            exitRequested = true;
            flushNavInput(60);
            return;
        }

        if (!isAttacking) {
            // Draw the ACTIVE state while both radios are still quiet. Once
            // carrier starts, do not touch the TFT until the next controlled
            // pause; both devices share the same SPI bus.
            sharedSpiPrepareDisplay(true);
            isAttacking = true;
            drawChannelGauge();
            resumeAttackCarriers();
        } else {
            pauseAttackCarriers();
            isAttacking = false;
            drawChannelGauge();
        }
        delay(220);
    }

    if (isAttacking) {
        if (millis() - lastBars >= 180) {
            // CE-low is enough to pause constant carrier without losing its
            // configuration. Draw one small frame, then resume immediately.
            pauseCarrierCeForAnimation();
            drawChannelBars();
            resumeCarrierCeAfterAnimation();
            lastBars = millis();
        }
        delayMicroseconds(150);
    }
}

void runJammer() {
    while (isEnterPressed() || isBackPressed()) delay(5);
    delay(100);

    jammerSetup();
    exitRequested = false;
    drawChannelGauge();

    if (activeRadioCount() == 0) {
        drawStringCentered(112, "NRF24 ERROR", TFT_RED, 2, FONT_BIG);
        drawStringCentered(150, "Revisa CE/CSN/SPI", TFT_WHITE, 1, FONT_SMALL);
        delay(2500);
        clearJammerScreen();
        flushNavInput(80);
        return;
    }

    while (!exitRequested) {
        jammerLoop();
        delay(5);
    }

    stopAttack();
    if (jam1Ok) jam1.powerDown();
    if (jam2Ok) jam2.powerDown();
    clearJammerScreen();
    while (isEnterPressed() || isBackPressed()) delay(5);
    flushNavInput(80);
}
