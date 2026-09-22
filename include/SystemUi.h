#ifndef SYSTEM_UI_H
#define SYSTEM_UI_H

#include <Arduino.h>

// Paleta de SYSTEM.  Se mantiene independiente de RF/WiFi para que las
// herramientas internas conserven una identidad visual consistente.
static constexpr uint16_t SYS_UI_BG      = 0x0000;
static constexpr uint16_t SYS_UI_PANEL   = 0x10A3;
static constexpr uint16_t SYS_UI_PANEL_2 = 0x18E5;
static constexpr uint16_t SYS_UI_ACCENT  = 0xFE4A;
static constexpr uint16_t SYS_UI_AMBER   = 0xFD20;
static constexpr uint16_t SYS_UI_TEXT    = 0xFFFF;
static constexpr uint16_t SYS_UI_MUTED   = 0x8C71;
static constexpr uint16_t SYS_UI_OK      = 0x6FEF;
static constexpr uint16_t SYS_UI_DANGER  = 0xF9A7;

void systemUiFrame(const String& title, const String& status = "SYSTEM");
void systemUiFooter(const String& left, const String& right = "HOLD: BACK");
void systemUiCard(int x, int y, int w, int h, bool selected = false,
                  uint16_t accent = SYS_UI_ACCENT);
void systemUiProgress(int x, int y, int w, int h, int percent,
                      uint16_t color = SYS_UI_ACCENT);

#endif
