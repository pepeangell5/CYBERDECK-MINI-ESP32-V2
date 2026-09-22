#include "RfUi.h"

#include "DisplayTFT.h"
#include "PepeDraw.h"

extern DisplayTFT tft;

static void drawRight(int xRight, int y, const String& text,
                      uint16_t color, int size = 1) {
    drawStringCustom(xRight - getTextWidth(text, size), y, text, color, size);
}

void rfUiFrame(const String& title, const String& badge,
               uint16_t badgeColor) {
    tft.fillScreen(RF_UI_BG);
    tft.drawRoundRect(4, 4, 312, 232, 12, RF_UI_ACCENT);
    tft.fillRoundRect(9, 9, 302, 29, 8, RF_UI_PANEL);
    tft.drawFastHLine(10, 41, 300, RF_UI_ACCENT);
    tft.drawFastHLine(10, 207, 300, RF_UI_ACCENT);
    drawStringBig(14, 13, title, RF_UI_TEXT, 1);
    drawRight(302, 17, badge, badgeColor, 1);
}

void rfUiFooter(const String& left, const String& right,
                uint16_t rightColor) {
    tft.fillRect(10, 211, 300, 20, RF_UI_BG);
    drawStringCustom(14, 218, left, RF_UI_TEXT, 1);
    drawRight(304, 218, right, rightColor, 1);
}

void rfUiCard(int x, int y, int w, int h, bool selected, uint16_t accent) {
    uint16_t fill = selected ? accent : RF_UI_PANEL;
    uint16_t border = selected ? RF_UI_TEXT : accent;
    tft.fillRoundRect(x, y, w, h, 7, fill);
    tft.drawRoundRect(x, y, w, h, 7, border);
}

void rfUiMetric(int x, int y, int w, const String& label,
                const String& value, uint16_t valueColor) {
    rfUiCard(x, y, w, 42, false, valueColor);
    drawStringCustom(x + 8, y + 7, label, RF_UI_MUTED, 1);
    drawStringBig(x + 8, y + 20, value, valueColor, 1);
}

void rfUiProgress(int x, int y, int w, int h, int percent, uint16_t color) {
    percent = constrain(percent, 0, 100);
    tft.fillRoundRect(x, y, w, h, h / 2, RF_UI_PANEL_2);
    tft.drawRoundRect(x, y, w, h, h / 2, RF_UI_LINE);
    int inner = ((w - 4) * percent) / 100;
    if (inner > 0) {
        tft.fillRoundRect(x + 2, y + 2, inner, h - 4,
                          (h - 4) / 2, color);
    }
}
