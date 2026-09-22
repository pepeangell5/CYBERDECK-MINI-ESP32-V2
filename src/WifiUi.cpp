#include "WifiUi.h"

#include "DisplayTFT.h"
#include "PepeDraw.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern DisplayTFT tft;

static volatile bool scanAnimationRunning = false;
static TaskHandle_t scanAnimationTaskHandle = nullptr;
static String scanAnimationTitle;
static String scanAnimationSubtitle;

static void drawRight(int xRight, int y, const String& text,
                      uint16_t color, int size = 1) {
    int x = xRight - getTextWidth(text, size);
    drawStringCustom(x, y, text, color, size);
}

void wifiUiFrame(const String& title, const String& badge,
                 uint16_t badgeColor) {
    tft.fillScreen(WIFI_UI_BG);
    tft.drawRoundRect(4, 4, 312, 232, 12, WIFI_UI_ACCENT);
    tft.fillRoundRect(9, 9, 302, 29, 8, WIFI_UI_PANEL);
    tft.drawFastHLine(10, 41, 300, WIFI_UI_ACCENT);
    tft.drawFastHLine(10, 207, 300, WIFI_UI_ACCENT);
    drawStringBig(14, 13, title, WIFI_UI_TEXT, 1);
    drawRight(302, 17, badge, badgeColor, 1);
}

void wifiUiFooter(const String& left, const String& right,
                  uint16_t rightColor) {
    tft.fillRect(10, 211, 300, 20, WIFI_UI_BG);
    drawStringCustom(14, 218, left, WIFI_UI_TEXT, 1);
    drawRight(304, 218, right, rightColor, 1);
}

void wifiUiCard(int x, int y, int w, int h, bool selected,
                uint16_t accent) {
    uint16_t fill = selected ? accent : WIFI_UI_PANEL;
    uint16_t border = selected ? WIFI_UI_TEXT : accent;
    tft.fillRoundRect(x, y, w, h, 7, fill);
    tft.drawRoundRect(x, y, w, h, 7, border);
}

void wifiUiProgress(int x, int y, int w, int h, int percent,
                    uint16_t color) {
    percent = constrain(percent, 0, 100);
    tft.fillRoundRect(x, y, w, h, h / 2, WIFI_UI_PANEL_2);
    tft.drawRoundRect(x, y, w, h, h / 2, WIFI_UI_LINE);
    int inner = ((w - 4) * percent) / 100;
    if (inner > 0) tft.fillRoundRect(x + 2, y + 2, inner, h - 4,
                                     (h - 4) / 2, color);
}

void wifiUiScanning(const String& title, const String& subtitle, int tick,
                    int percent, int count) {
    const int cx = 160;
    const int cy = 113;
    static int previousPhase = -1;
    static int previousPercent = -999;
    static int previousCount = -999;
    static String previousTitle;
    static String previousSubtitle;
    bool reset = tick == 0 || title != previousTitle ||
                 subtitle != previousSubtitle;

    if (reset) {
        wifiUiFrame(title, "SCANNING", WIFI_UI_OK);
        tft.fillCircle(cx, cy, 50, WIFI_UI_PANEL);
        int subtitleWidth = getTextWidth(subtitle, 1);
        drawStringCustom(max(10, (320 - subtitleWidth) / 2), 172,
                         subtitle, WIFI_UI_MUTED, 1);
        previousPhase = -1;
        previousPercent = -999;
        previousCount = -999;
        previousTitle = title;
        previousSubtitle = subtitle;
    } else if (previousPhase >= 0) {
        static const int8_t dx[8] = {0, 24, 35, 24, 0, -24, -35, -24};
        static const int8_t dy[8] = {-35, -24, 0, 24, 35, 24, 0, -24};
        tft.drawLine(cx, cy, cx + dx[previousPhase], cy + dy[previousPhase],
                     WIFI_UI_PANEL);
        tft.fillCircle(cx + dx[previousPhase], cy + dy[previousPhase], 3,
                       WIFI_UI_PANEL);
    }

    // Restore the grid after erasing the previous sweep, then draw only the
    // new sweep. The frame, labels and footer remain untouched.
    tft.drawCircle(cx, cy, 50, WIFI_UI_ACCENT);
    tft.drawCircle(cx, cy, 35, WIFI_UI_LINE);
    tft.drawCircle(cx, cy, 20, WIFI_UI_LINE);
    tft.drawFastHLine(cx - 48, cy, 96, WIFI_UI_LINE);
    tft.drawFastVLine(cx, cy - 48, 96, WIFI_UI_LINE);

    int phase = tick % 8;
    static const int8_t dx[8] = {0, 24, 35, 24, 0, -24, -35, -24};
    static const int8_t dy[8] = {-35, -24, 0, 24, 35, 24, 0, -24};
    tft.drawLine(cx, cy, cx + dx[phase], cy + dy[phase], WIFI_UI_OK);
    tft.fillCircle(cx + dx[phase], cy + dy[phase], 3, WIFI_UI_OK);
    tft.fillCircle(cx, cy, 3, WIFI_UI_ACCENT);
    previousPhase = phase;

    if (count >= 0 && (reset || count != previousCount)) {
        tft.fillRect(88, 184, 144, 10, WIFI_UI_BG);
        String found = String(count) + " FOUND";
        int foundWidth = getTextWidth(found, 1);
        drawStringCustom((320 - foundWidth) / 2, 188, found,
                         WIFI_UI_ACCENT, 1);
    }
    if (percent >= 0 && (reset || percent != previousPercent)) {
        wifiUiProgress(38, 193, 244, 10, percent, WIFI_UI_OK);
    }
    if (reset || percent != previousPercent) {
        wifiUiFooter("PLEASE WAIT", percent >= 0 ? String(percent) + "%" : "LIVE");
    }
    previousPercent = percent;
    previousCount = count;
}

static void wifiUiScanAnimationTask(void*) {
    int tick = 1;
    while (scanAnimationRunning) {
        vTaskDelay(pdMS_TO_TICKS(120));
        if (!scanAnimationRunning) break;
        wifiUiScanning(scanAnimationTitle, scanAnimationSubtitle, tick++);
    }
    scanAnimationTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

void wifiUiScanAnimationStart(const String& title, const String& subtitle) {
    wifiUiScanAnimationStop();
    scanAnimationTitle = title;
    scanAnimationSubtitle = subtitle;
    wifiUiScanning(scanAnimationTitle, scanAnimationSubtitle, 0);
    scanAnimationRunning = true;
    if (xTaskCreatePinnedToCore(wifiUiScanAnimationTask, "wifi-ui-radar",
                                4096, nullptr, 1,
                                &scanAnimationTaskHandle, 1) != pdPASS) {
        scanAnimationRunning = false;
        scanAnimationTaskHandle = nullptr;
    }
}

void wifiUiScanAnimationStop() {
    scanAnimationRunning = false;
    unsigned long started = millis();
    while (scanAnimationTaskHandle != nullptr && millis() - started < 500) {
        delay(5);
    }
}

void wifiUiEmpty(const String& title, const String& message,
                 const String& hint, uint16_t color) {
    wifiUiFrame(title, "STATUS", color);
    wifiUiCard(20, 67, 280, 104, false, color);
    tft.drawCircle(160, 99, 18, color);
    drawStringBig(154, 90, "!", color, 1);
    int messageWidth = getTextWidth(message, 1);
    drawStringBig(max(12, (320 - messageWidth) / 2), 126,
                  message, WIFI_UI_TEXT, 1);
    int hintWidth = getTextWidth(hint, 1);
    drawStringCustom(max(12, (320 - hintWidth) / 2), 151,
                     hint, WIFI_UI_MUTED, 1);
    wifiUiFooter("BACK: RETURN", "OK: RETRY", color);
}

void wifiUiMetric(int x, int y, int w, const String& label,
                  const String& value, uint16_t valueColor) {
    wifiUiCard(x, y, w, 42, false, valueColor);
    drawStringCustom(x + 8, y + 7, label, WIFI_UI_MUTED, 1);
    drawStringBig(x + 8, y + 20, value, valueColor, 1);
}
