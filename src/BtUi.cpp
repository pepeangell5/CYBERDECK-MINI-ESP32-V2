#include "BtUi.h"

#include "DisplayTFT.h"
#include "PepeDraw.h"

extern DisplayTFT tft;

static void btDrawRight(int xRight, int y, const String& text,
                        uint16_t color, int size = 1) {
    drawStringCustom(xRight - getTextWidth(text, size), y, text, color, size);
}

void btUiFrame(const String& title, const String& badge,
               uint16_t badgeColor) {
    tft.fillScreen(BT_UI_BG);
    tft.drawRoundRect(4, 4, 312, 232, 12, BT_UI_ACCENT);
    tft.fillRoundRect(9, 9, 302, 29, 8, BT_UI_PANEL);
    tft.drawFastHLine(10, 41, 300, BT_UI_ACCENT);
    tft.drawFastHLine(10, 207, 300, BT_UI_ACCENT);
    drawStringBig(14, 13, title, BT_UI_TEXT, 1);
    btDrawRight(302, 17, badge, badgeColor, 1);
}

void btUiFooter(const String& left, const String& right,
                uint16_t rightColor) {
    tft.fillRect(10, 211, 300, 20, BT_UI_BG);
    drawStringCustom(14, 218, left, BT_UI_TEXT, 1);
    btDrawRight(304, 218, right, rightColor, 1);
}

void btUiCard(int x, int y, int w, int h, bool selected, uint16_t accent) {
    uint16_t fill = selected ? accent : BT_UI_PANEL;
    uint16_t border = selected ? BT_UI_TEXT : accent;
    tft.fillRoundRect(x, y, w, h, 7, fill);
    tft.drawRoundRect(x, y, w, h, 7, border);
}

void btUiMetric(int x, int y, int w, const String& label,
                const String& value, uint16_t valueColor) {
    btUiCard(x, y, w, 42, false, valueColor);
    drawStringCustom(x + 8, y + 7, label, BT_UI_MUTED, 1);
    drawStringBig(x + 8, y + 20, value, valueColor, 1);
}

void btUiProgress(int x, int y, int w, int h, int percent, uint16_t color) {
    percent = constrain(percent, 0, 100);
    tft.fillRoundRect(x, y, w, h, h / 2, BT_UI_PANEL_2);
    tft.drawRoundRect(x, y, w, h, h / 2, BT_UI_LINE);
    int inner = ((w - 4) * percent) / 100;
    if (inner > 0) {
        tft.fillRoundRect(x + 2, y + 2, inner, h - 4,
                          (h - 4) / 2, color);
    }
}
