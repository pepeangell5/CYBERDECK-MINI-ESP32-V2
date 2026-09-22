#ifndef BT_UI_H
#define BT_UI_H

#include <Arduino.h>
#include <TFT_eSPI.h>

static constexpr uint16_t BT_UI_BG      = TFT_BLACK;
static constexpr uint16_t BT_UI_PANEL   = 0x1083;
static constexpr uint16_t BT_UI_PANEL_2 = 0x2107;
static constexpr uint16_t BT_UI_ACCENT  = 0xA35F;
static constexpr uint16_t BT_UI_GLOW    = 0x4DFF;
static constexpr uint16_t BT_UI_LINE    = 0x51AA;
static constexpr uint16_t BT_UI_TEXT    = TFT_WHITE;
static constexpr uint16_t BT_UI_MUTED   = 0x9CF3;
static constexpr uint16_t BT_UI_OK      = 0x6FF7;
static constexpr uint16_t BT_UI_DANGER  = 0xF971;

void btUiFrame(const String& title, const String& badge = "BLE LAB",
               uint16_t badgeColor = BT_UI_ACCENT);
void btUiFooter(const String& left, const String& right,
                uint16_t rightColor = BT_UI_ACCENT);
void btUiCard(int x, int y, int w, int h, bool selected = false,
              uint16_t accent = BT_UI_ACCENT);
void btUiMetric(int x, int y, int w, const String& label,
                const String& value, uint16_t valueColor = BT_UI_ACCENT);
void btUiProgress(int x, int y, int w, int h, int percent,
                  uint16_t color = BT_UI_ACCENT);

#endif
