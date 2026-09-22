#ifndef WIFI_UI_H
#define WIFI_UI_H

#include <Arduino.h>
#include "DisplayTFT.h"

// Shared Lopaka-inspired visual language for every screen inside WIFI.
static constexpr uint16_t WIFI_UI_BG       = TFT_BLACK;
static constexpr uint16_t WIFI_UI_PANEL    = 0x0883;
static constexpr uint16_t WIFI_UI_PANEL_2  = 0x1126;
static constexpr uint16_t WIFI_UI_ACCENT   = 0x26FE;
static constexpr uint16_t WIFI_UI_LINE     = 0x32CE;
static constexpr uint16_t WIFI_UI_TEXT     = TFT_WHITE;
static constexpr uint16_t WIFI_UI_MUTED    = 0x7CD5;
static constexpr uint16_t WIFI_UI_OK       = 0x6FF7;
static constexpr uint16_t WIFI_UI_WARN     = 0xFD80;
static constexpr uint16_t WIFI_UI_DANGER   = 0xF971;

void wifiUiFrame(const String& title, const String& badge = "WIFI",
                 uint16_t badgeColor = WIFI_UI_ACCENT);
void wifiUiFooter(const String& left, const String& right,
                  uint16_t rightColor = WIFI_UI_ACCENT);
void wifiUiCard(int x, int y, int w, int h, bool selected = false,
                uint16_t accent = WIFI_UI_ACCENT);
void wifiUiProgress(int x, int y, int w, int h, int percent,
                    uint16_t color = WIFI_UI_ACCENT);
void wifiUiScanning(const String& title, const String& subtitle, int tick,
                    int percent = -1, int count = -1);
// Runs the radar on a small FreeRTOS task while a blocking WiFi scan owns the
// calling task. Stop it before drawing the results screen.
void wifiUiScanAnimationStart(const String& title, const String& subtitle);
void wifiUiScanAnimationStop();
void wifiUiEmpty(const String& title, const String& message,
                 const String& hint, uint16_t color = WIFI_UI_DANGER);
void wifiUiMetric(int x, int y, int w, const String& label,
                  const String& value, uint16_t valueColor);

#endif
