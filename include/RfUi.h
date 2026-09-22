#ifndef RF_UI_H
#define RF_UI_H

#include <Arduino.h>
#include <TFT_eSPI.h>

static constexpr uint16_t RF_UI_BG      = TFT_BLACK;
static constexpr uint16_t RF_UI_PANEL   = 0x1082;
static constexpr uint16_t RF_UI_PANEL_2 = 0x2104;
static constexpr uint16_t RF_UI_ACCENT  = 0xFD80;
static constexpr uint16_t RF_UI_LINE    = 0x7B40;
static constexpr uint16_t RF_UI_TEXT    = TFT_WHITE;
static constexpr uint16_t RF_UI_MUTED   = 0x9CF3;
static constexpr uint16_t RF_UI_OK      = 0x6FF7;
static constexpr uint16_t RF_UI_DANGER  = 0xF971;

void rfUiFrame(const String& title, const String& badge = "RF LAB",
               uint16_t badgeColor = RF_UI_ACCENT);
void rfUiFooter(const String& left, const String& right,
                uint16_t rightColor = RF_UI_ACCENT);
void rfUiCard(int x, int y, int w, int h, bool selected = false,
              uint16_t accent = RF_UI_ACCENT);
void rfUiMetric(int x, int y, int w, const String& label,
                const String& value, uint16_t valueColor = RF_UI_ACCENT);
void rfUiProgress(int x, int y, int w, int h, int percent,
                  uint16_t color = RF_UI_ACCENT);

#endif
