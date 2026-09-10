#include "SharedSpi.h"

#include <SPI.h>

#include "Pins.h"

void sharedSpiInitPins(bool radioCeLow) {
    pinMode(TFT_CS_PIN, OUTPUT);
    pinMode(NRF1_CSN_PIN, OUTPUT);
    pinMode(NRF1_CE_PIN, OUTPUT);
    pinMode(SD_CS_PIN, OUTPUT);

#if NRF2_ENABLED
    pinMode(NRF2_CSN_PIN, OUTPUT);
    pinMode(NRF2_CE_PIN, OUTPUT);
#endif

    sharedSpiRelease(radioCeLow);
}

void sharedSpiRelease(bool radioCeLow) {
    digitalWrite(TFT_CS_PIN, HIGH);
    digitalWrite(NRF1_CSN_PIN, HIGH);
#if NRF2_ENABLED
    digitalWrite(NRF2_CSN_PIN, HIGH);
#endif
    digitalWrite(SD_CS_PIN, HIGH);

    if (radioCeLow) {
        digitalWrite(NRF1_CE_PIN, LOW);
#if NRF2_ENABLED
        digitalWrite(NRF2_CE_PIN, LOW);
#endif
    }

    delayMicroseconds(80);
}

void sharedSpiBeginMainBus() {
    sharedSpiRelease(true);
    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN);
}

void sharedSpiPrepareDisplay(bool radioCeLow) {
    sharedSpiRelease(radioCeLow);
}

void sharedSpiPrepareRadio(bool radioCeLow) {
    sharedSpiRelease(radioCeLow);
}

void sharedSpiPrepareSd() {
    sharedSpiRelease(true);
}

