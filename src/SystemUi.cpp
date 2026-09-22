#include "SystemUi.h"

#include "DisplayTFT.h"
#include "PepeDraw.h"

extern DisplayTFT tft;

void systemUiFrame(const String& title, const String& status) {
    tft.fillScreen(SYS_UI_BG);
    tft.drawRoundRect(4, 4, 312, 232, 12, SYS_UI_ACCENT);
    tft.fillRoundRect(9, 9, 302, 30, 8, SYS_UI_PANEL);
    tft.drawFastHLine(10, 41, 300, SYS_UI_ACCENT);
    tft.drawFastHLine(10, 211, 300, SYS_UI_ACCENT);
    drawStringBig(14, 12, title, SYS_UI_TEXT, 1);
    drawStringRight(302, 17, status, SYS_UI_ACCENT, 1);
}

void systemUiFooter(const String& left, const String& right) {
    tft.fillRect(11, 215, 298, 16, SYS_UI_BG);
    drawStringCustom(14, 220, left, SYS_UI_TEXT, 1);
    drawStringRight(304, 220, right, SYS_UI_ACCENT, 1);
}

void systemUiCard(int x, int y, int w, int h, bool selected,
                  uint16_t accent) {
    uint16_t fill = selected ? accent : SYS_UI_PANEL;
    tft.fillRoundRect(x, y, w, h, 7, fill);
    tft.drawRoundRect(x, y, w, h, 7, accent);
}

void systemUiProgress(int x, int y, int w, int h, int percent,
                      uint16_t color) {
    percent = constrain(percent, 0, 100);
    tft.fillRoundRect(x, y, w, h, h / 2, SYS_UI_PANEL_2);
    tft.drawRoundRect(x, y, w, h, h / 2, SYS_UI_MUTED);
    int innerW = ((w - 4) * percent) / 100;
    if (innerW > 0) tft.fillRoundRect(x + 2, y + 2, innerW, h - 4,
                                     max(1, (h - 4) / 2), color);
}
