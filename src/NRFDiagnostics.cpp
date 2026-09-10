#include "NRFDiagnostics.h"

#include <RF24.h>
#include <SPI.h>

#include "DisplayTFT.h"
#include "Input.h"
#include "PepeDraw.h"
#include "Pins.h"

extern DisplayTFT tft;

static RF24 diagNrf1(NRF1_CE_PIN, NRF1_CSN_PIN, NRF_SPI_SPEED);
#if NRF2_ENABLED
static RF24 diagNrf2(NRF2_CE_PIN, NRF2_CSN_PIN, NRF_SPI_SPEED);
#endif

struct NrfDiagResult {
    bool beginOk;
    bool chipOk;
};

struct NrfLinkResult {
    bool tx12Ok;
    bool tx21Ok;
};

static void prepareNrfBus() {
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
    delay(20);
}

static NrfDiagResult testRadio(RF24& radio, uint8_t cePin, uint8_t csnPin) {
    digitalWrite(TFT_CS_PIN, HIGH);
    digitalWrite(NRF1_CSN_PIN, HIGH);
    digitalWrite(NRF1_CE_PIN, LOW);
#if NRF2_ENABLED
    digitalWrite(NRF2_CSN_PIN, HIGH);
    digitalWrite(NRF2_CE_PIN, LOW);
#endif
    delay(8);

    bool beginOk = radio.begin();
    bool chipOk = false;

    if (beginOk) {
        radio.setAutoAck(false);
        radio.setPALevel(RF24_PA_LOW);
        radio.setDataRate(RF24_1MBPS);
        radio.stopListening();
        chipOk = radio.isChipConnected();
        radio.powerDown();
    }

    digitalWrite(cePin, LOW);
    digitalWrite(csnPin, HIGH);
    digitalWrite(TFT_CS_PIN, HIGH);
    delay(8);

    return { beginOk, chipOk };
}

static void configureLinkRadio(RF24& radio) {
    radio.powerUp();
    radio.setAutoAck(true);
    radio.setRetries(5, 15);
    radio.setAddressWidth(5);
    radio.setChannel(76);
    radio.setDataRate(RF24_1MBPS);
    radio.setPALevel(RF24_PA_LOW);
    radio.setCRCLength(RF24_CRC_16);
    radio.stopListening();
}

static bool testPacketLink(RF24& tx, RF24& rx, const uint8_t* address,
                           uint8_t marker) {
    configureLinkRadio(tx);
    configureLinkRadio(rx);

    rx.openReadingPipe(1, address);
    tx.openWritingPipe(address);
    rx.startListening();
    delay(15);

    uint8_t payload[8] = { 'N', 'R', 'F', marker, 0x5A, 0xA5, 0x11, 0x22 };
    bool wrote = tx.write(payload, sizeof(payload));

    bool received = false;
    unsigned long start = millis();
    while (millis() - start < 80) {
        if (rx.available()) {
            uint8_t got[8] = { 0 };
            rx.read(got, sizeof(got));
            received = (got[0] == 'N' && got[1] == 'R' && got[2] == 'F' &&
                        got[3] == marker);
            break;
        }
        delay(2);
    }

    rx.stopListening();
    tx.stopListening();
    tx.powerDown();
    rx.powerDown();
    delay(8);

    return wrote && received;
}

static NrfLinkResult testRadioLink(const NrfDiagResult& nrf1,
                                   const NrfDiagResult& nrf2) {
    NrfLinkResult result = { false, false };
#if NRF2_ENABLED
    if (!(nrf1.beginOk && nrf1.chipOk && nrf2.beginOk && nrf2.chipOk)) {
        return result;
    }

    static const uint8_t addr12[5] = { 'D', 'I', 'A', '1', '2' };
    static const uint8_t addr21[5] = { 'D', 'I', 'A', '2', '1' };

    digitalWrite(TFT_CS_PIN, HIGH);
    result.tx12Ok = testPacketLink(diagNrf1, diagNrf2, addr12, 0x12);
    result.tx21Ok = testPacketLink(diagNrf2, diagNrf1, addr21, 0x21);
#endif
    return result;
}

static void prepareNrfDiagnosticsDisplay() {
    digitalWrite(NRF1_CSN_PIN, HIGH);
    digitalWrite(NRF1_CE_PIN, LOW);
#if NRF2_ENABLED
    digitalWrite(NRF2_CSN_PIN, HIGH);
    digitalWrite(NRF2_CE_PIN, LOW);
#endif
    digitalWrite(TFT_CS_PIN, HIGH);
    delayMicroseconds(80);

    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN);
    tft.begin();
    tft.invertDisplay(false);
    tft.setRotation(3);
    delay(20);
}

static void clearDiagnosticsScreen() {
    prepareNrfDiagnosticsDisplay();
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, 320, 240, TFT_BLACK);
}

static void drawTestingScreen() {
    clearDiagnosticsScreen();
    tft.drawRect(0, 0, 320, 240, TFT_WHITE);
    drawStringCentered(74, "NRF24 DIAGNOSTIC", TFT_CYAN, 1, FONT_BIG);
    drawStringCentered(118, "TESTING RADIOS...", TFT_YELLOW, 2, FONT_SMALL);
    drawStringCentered(154, "Please wait", TFT_WHITE, 1, FONT_SMALL);
}

static void drawResultRow(int y, const char* name, const NrfDiagResult& result,
                          uint8_t cePin, uint8_t csnPin) {
    bool ok = result.beginOk && result.chipOk;
    uint16_t color = ok ? TFT_GREEN : TFT_RED;

    tft.drawRect(18, y, 284, 50, TFT_DARKGREY);
    drawStringCustom(30, y + 9, name, TFT_WHITE, 1);
    drawStringCustom(126, y + 9, ok ? "OK" : "FAILED", color, 2);
    drawStringCustom(30, y + 32,
                     "CE:" + String(cePin) + " CSN:" + String(csnPin) +
                     " BEGIN:" + String(result.beginOk ? "OK" : "FAIL") +
                     " CHIP:" + String(result.chipOk ? "OK" : "FAIL"),
                     TFT_WHITE, 1);
}

static void drawDisabledRow(int y, const char* name, uint8_t cePin, uint8_t csnPin) {
    tft.drawRect(18, y, 284, 50, TFT_DARKGREY);
    drawStringCustom(30, y + 9, name, TFT_WHITE, 1);
    drawStringCustom(126, y + 9, "DISABLED", TFT_YELLOW, 2);
    drawStringCustom(30, y + 32,
                     "CE:" + String(cePin) + " CSN:" + String(csnPin) +
                     " single NRF mode",
                     TFT_WHITE, 1);
}

static void drawLinkRow(int y, const NrfLinkResult& link) {
#if NRF2_ENABLED
    bool ok = link.tx12Ok && link.tx21Ok;
    tft.drawRect(18, y, 284, 42, TFT_DARKGREY);
    drawStringCustom(30, y + 8, "RF LINK", TFT_WHITE, 1);
    drawStringCustom(126, y + 6, ok ? "OK" : "WEAK/FAIL",
                     ok ? TFT_GREEN : TFT_YELLOW, 1);
    drawStringCustom(30, y + 26,
                     "1>2:" + String(link.tx12Ok ? "OK" : "FAIL") +
                     "  2>1:" + String(link.tx21Ok ? "OK" : "FAIL"),
                     TFT_WHITE, 1);
#else
    (void)y;
    (void)link;
#endif
}

static void drawNrfDiagnostics(const NrfDiagResult& nrf1, const NrfDiagResult& nrf2,
                               const NrfLinkResult& link) {
    bool anyOk = nrf1.beginOk && nrf1.chipOk;

    clearDiagnosticsScreen();
    tft.drawRect(0, 0, 320, 240, TFT_WHITE);
    drawStringCentered(12, "NRF24 DIAGNOSTIC", TFT_CYAN, 1, FONT_BIG);
    tft.drawFastHLine(14, 36, 292, TFT_DARKGREY);

    drawResultRow(54, "NRF1", nrf1, NRF1_CE_PIN, NRF1_CSN_PIN);
#if NRF2_ENABLED
    drawResultRow(116, "NRF2", nrf2, NRF2_CE_PIN, NRF2_CSN_PIN);
    anyOk = anyOk || (nrf2.beginOk && nrf2.chipOk);
    drawLinkRow(170, link);
#else
    drawDisabledRow(116, "NRF2", NRF2_CE_PIN, NRF2_CSN_PIN);
#endif

    drawStringCentered(214, "SPI SCK:12 MOSI:11 MISO:13  1MHz",
                       TFT_WHITE, 1, FONT_SMALL);
    drawStringCentered(228, "OK: RETEST   BACK: MENU", TFT_WHITE, 1, FONT_SMALL);
}

static void runNrfTestOnce() {
    drawTestingScreen();
    delay(80);

    prepareNrfBus();

    NrfDiagResult nrf1 = testRadio(diagNrf1, NRF1_CE_PIN, NRF1_CSN_PIN);
#if NRF2_ENABLED
    NrfDiagResult nrf2 = testRadio(diagNrf2, NRF2_CE_PIN, NRF2_CSN_PIN);
#else
    NrfDiagResult nrf2 = { false, false };
#endif
    NrfLinkResult link = testRadioLink(nrf1, nrf2);

    Serial.printf("[NRFDiagnostics] NRF1 CE:%d CSN:%d begin:%s chip:%s\n",
                  NRF1_CE_PIN, NRF1_CSN_PIN,
                  nrf1.beginOk ? "OK" : "FAIL",
                  nrf1.chipOk ? "OK" : "FAIL");
#if NRF2_ENABLED
    Serial.printf("[NRFDiagnostics] NRF2 CE:%d CSN:%d begin:%s chip:%s\n",
                  NRF2_CE_PIN, NRF2_CSN_PIN,
                  nrf2.beginOk ? "OK" : "FAIL",
                  nrf2.chipOk ? "OK" : "FAIL");
    Serial.printf("[NRFDiagnostics] RF link 1>2:%s 2>1:%s\n",
                  link.tx12Ok ? "OK" : "FAIL",
                  link.tx21Ok ? "OK" : "FAIL");
#else
    Serial.println("[NRFDiagnostics] NRF2 disabled");
#endif

    drawNrfDiagnostics(nrf1, nrf2, link);
}

void runNRFDiagnostics() {
    while (isEnterPressed() || isBackPressed()) delay(5);
    delay(80);
    flushNavInput(40);

    runNrfTestOnce();

    bool exitTool = false;
    while (!exitTool) {
        NavAction action = readNavAction(120);
        if (action == NAV_BACK || isBackPressed()) {
            exitTool = true;
        } else if (action == NAV_ENTER) {
            runNrfTestOnce();
            flushNavInput(80);
        }
        delay(8);
    }

    while (isEnterPressed() || isBackPressed()) delay(5);
    flushNavInput(80);
    tft.fillScreen(TFT_BLACK);
}
