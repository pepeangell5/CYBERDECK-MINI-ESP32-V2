#pragma once

#include <Arduino.h>

void sharedSpiInitPins(bool radioCeLow = true);
void sharedSpiRelease(bool radioCeLow = true);
void sharedSpiBeginMainBus();
void sharedSpiPrepareDisplay(bool radioCeLow = true);
void sharedSpiPrepareRadio(bool radioCeLow = true);
void sharedSpiPrepareSd();

