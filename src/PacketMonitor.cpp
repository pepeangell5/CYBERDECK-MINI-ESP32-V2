#include "PacketMonitor.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include "Settings.h"
#include "Pins.h"
#include "SoundUtils.h"
#include "Input.h"

// ═════════════════════════════════════════════════════════════════════════════
//  CONFIGURACIÓN
// ═════════════════════════════════════════════════════════════════════════════
#define HISTORY_SIZE    60        // 60 segundos de historial
#define BAR_WIDTH       4         // px por cada barra

// Tope para escalar el meter y el history.
// Calibrado para entornos reales (ESP32 solo cuenta frames 802.11 válidos).
#define PPS_MAX_SCALE   500

// ── Layout ─────────────────────────────────────────────────────────────────
#define HISTORY_X     40
#define HISTORY_Y     179
#define HISTORY_W     (HISTORY_SIZE * BAR_WIDTH)
#define HISTORY_H     25

#define METER_X       177
#define METER_Y       96
#define METER_W       117
#define METER_H       12

// Palette del cuarto elemento del carrusel: MONITOR / mint.
static constexpr uint16_t MON_BG      = TFT_BLACK;
static constexpr uint16_t MON_PANEL   = 0x0883;
static constexpr uint16_t MON_PANEL_2 = 0x1128;
static constexpr uint16_t MON_ACCENT  = 0x6FF7;
static constexpr uint16_t MON_GLOW    = 0x26FE;
static constexpr uint16_t MON_MUTED   = 0x7CD5;
static constexpr uint16_t MON_ACTION  = 0xF971;

// ═════════════════════════════════════════════════════════════════════════════
//  ESTADO
// ═════════════════════════════════════════════════════════════════════════════
static volatile unsigned long totalPacketsSec = 0;
static volatile unsigned long managementPacketsSec = 0;
static volatile unsigned long dataPacketsSec = 0;
static volatile unsigned long controlPacketsSec = 0;
static portMUX_TYPE packetCounterMux = portMUX_INITIALIZER_UNLOCKED;

static unsigned long lastUpdate  = 0;
static unsigned long frameCount  = 0;

// Histórico circular
static int  history[HISTORY_SIZE];
static int  historyIdx   = 0;
static bool historyFull  = false;

// Estadísticas
static unsigned long totalEver   = 0;
static unsigned long sampleCount = 0;
static unsigned long sumPps      = 0;
static int peakPps               = 0;
static int currentPps            = 0;
static int smoothedPps           = 0;
static int lastDrawnPps          = -1;
static int currentManagementPps  = 0;
static int currentDataPps        = 0;
static int currentControlPps     = 0;

static int monitorChannel = 1;
static bool monitorMuted = false;

// Niveles (UMBRALES RECALIBRADOS)
enum ActivityLevel { LVL_QUIET, LVL_LOW, LVL_ACTIVE, LVL_BUSY, LVL_HEAVY, LVL_FLOODED };
static ActivityLevel currentLevel = LVL_QUIET;
static ActivityLevel lastLevel    = LVL_QUIET;

// Meter anim
static int meterWidth     = 0;
static int peakMeterWidth = 0;

// ═════════════════════════════════════════════════════════════════════════════
//  SNIFFER CALLBACK
// ═════════════════════════════════════════════════════════════════════════════
static void sniffer_callback(void* buf, wifi_promiscuous_pkt_type_t type) {
    (void)buf;
    portENTER_CRITICAL(&packetCounterMux);
    totalPacketsSec++;
    switch (type) {
        case WIFI_PKT_MGMT: managementPacketsSec++; break;
        case WIFI_PKT_DATA: dataPacketsSec++;       break;
        case WIFI_PKT_CTRL: controlPacketsSec++;    break;
        default: break;
    }
    portEXIT_CRITICAL(&packetCounterMux);
}

// ═════════════════════════════════════════════════════════════════════════════
//  HELPERS
// ═════════════════════════════════════════════════════════════════════════════

// 🔧 UMBRALES RECALIBRADOS para entornos reales
//    Max típico en entorno normal: ~100-200 pps
//    Max con jammer activo: 250-500+ pps
static ActivityLevel classify(int pps) {
    if (pps < 5)    return LVL_QUIET;
    if (pps < 25)   return LVL_LOW;
    if (pps < 80)   return LVL_ACTIVE;
    if (pps < 150)  return LVL_BUSY;
    if (pps < 250)  return LVL_HEAVY;
    return LVL_FLOODED;     // 250+ = probable jamming/flood
}

static const char* levelLabel(ActivityLevel l) {
    switch (l) {
        case LVL_QUIET:   return "QUIET";
        case LVL_LOW:     return "LOW";
        case LVL_ACTIVE:  return "ACTIVE";
        case LVL_BUSY:    return "BUSY";
        case LVL_HEAVY:   return "HEAVY";
        case LVL_FLOODED: return "FLOODED";
    }
    return "";
}

static uint16_t levelColor(ActivityLevel l) {
    switch (l) {
        case LVL_QUIET:   return TFT_CYAN;
        case LVL_LOW:     return TFT_GREEN;
        case LVL_ACTIVE:  return TFT_GREEN;
        case LVL_BUSY:    return TFT_YELLOW;
        case LVL_HEAVY:   return TFT_ORANGE;
        case LVL_FLOODED: return TFT_RED;
    }
    return TFT_WHITE;
}

// Escala pps → altura. Tope ajustado a PPS_MAX_SCALE.
static int scaleToHeight(int pps, int maxH) {
    if (pps <= 0) return 0;
    if (pps > PPS_MAX_SCALE) pps = PPS_MAX_SCALE;
    float ratio = sqrt((float)pps / (float)PPS_MAX_SCALE);
    int h = (int)(ratio * maxH);
    if (h < 1 && pps > 0) h = 1;
    return h;
}

static int channelFreq(int ch) { return 2407 + ch * 5; }

// ═════════════════════════════════════════════════════════════════════════════
//  SONIDOS CORTOS (eventos)
// ═════════════════════════════════════════════════════════════════════════════
static void playStartupChirp() {
    beep(1200, 70); delay(30);
    beep(1800, 70); delay(30);
    beep(2400, 100);
}

static void playExitChirp() {
    beep(2400, 70); delay(30);
    beep(1800, 70); delay(30);
    beep(1200, 100);
}

static void playChannelBlip() {
    if (soundEnabled && !monitorMuted) beep(2000, 25);
}

// ═════════════════════════════════════════════════════════════════════════════
//  SONIDO AMBIENTE (llamado continuamente en el loop)
//  🎵 Nueva lógica: patrones distintos por nivel, frecuencias en el sweet spot
//     del piezo para máxima intensidad percibida.
// ═════════════════════════════════════════════════════════════════════════════
static void updateAmbientSound(int pps) {
#if BUZZER_PIN < 0
    (void)pps;
    return;
#else
    static int lastTone = -1;
    static uint32_t lastToneUpdate = 0;
    const uint32_t now = millis();
    if (now - lastToneUpdate < 45) return;
    lastToneUpdate = now;

    // El monitor solo alerta cuando hay actividad realmente alta. Mantener el
    // buzzer callado en QUIET/LOW/ACTIVE hace útil la alarma y evita un tono
    // continuo durante el uso normal.
    int tone = 0;
    if (soundEnabled && !monitorMuted) {
        if (pps >= 250) {
            tone = ((now / 120) % 2) ? 2450 : 900;   // FLOODED: sirena
        } else if (pps >= 150) {
            tone = ((now / 100) % 2) ? 1850 : 1250; // HEAVY: wobble
        } else if (pps >= 80) {
            const int slot = (now / 140) % 5;        // BUSY: doble pulso
            if (slot == 0) tone = 1650;
            else if (slot == 1) tone = 1350;
        }
    }

    if (tone != lastTone) {
        ledcWriteTone(0, tone);
        lastTone = tone;
    }
#endif
}

// ═════════════════════════════════════════════════════════════════════════════
//  DIBUJO
// ═════════════════════════════════════════════════════════════════════════════
static void drawFrame() {
    tft.fillScreen(MON_BG);
    tft.drawRoundRect(5, 5, 310, 230, 14, MON_ACCENT);

    tft.fillRoundRect(10, 10, 300, 31, 8, MON_PANEL);
    tft.drawRoundRect(10, 10, 300, 31, 8, MON_GLOW);
    drawStringBig(17, 15, "PACKET MONITOR", TFT_WHITE, 1);
    drawStringCustom(17, 30, "PASSIVE 802.11", MON_MUTED, 1);

    tft.fillRoundRect(10, 47, 300, 77, 10, MON_PANEL);
    tft.drawRoundRect(10, 47, 300, 77, 10, MON_ACCENT);
    tft.drawFastVLine(166, 55, 60, MON_GLOW);
    drawStringCustom(18, 55, "LIVE TRAFFIC", MON_MUTED, 1);
    drawStringCustom(18, 108, "PACKETS / SECOND", MON_MUTED, 1);
    drawStringCustom(177, 55, "ACTIVITY", MON_MUTED, 1);
    drawStringCustom(177, 83, "LOAD", MON_MUTED, 1);
    tft.drawRoundRect(METER_X, METER_Y, METER_W, METER_H, 5, MON_GLOW);

    const int metricX[] = {10, 108, 206};
    const char* metricLabels[] = {"MGMT", "DATA", "CTRL"};
    for (int i = 0; i < 3; i++) {
        tft.fillRoundRect(metricX[i], 130, 94, 34, 7, MON_PANEL_2);
        tft.drawRoundRect(metricX[i], 130, 94, 34, 7, MON_GLOW);
        drawStringCustom(metricX[i] + 8, 135, metricLabels[i], MON_MUTED, 1);
    }

    tft.fillRoundRect(10, 169, 300, 41, 7, MON_PANEL);
    tft.drawRoundRect(10, 169, 300, 41, 7, MON_GLOW);
    drawStringCustom(17, 173, "60S", MON_ACCENT, 1);
    tft.fillRect(HISTORY_X, HISTORY_Y, HISTORY_W, HISTORY_H, MON_BG);
    tft.drawFastHLine(HISTORY_X, HISTORY_Y + HISTORY_H - 1,
                      HISTORY_W, MON_GLOW);

    tft.drawFastHLine(11, 214, 298, MON_GLOW);
}

static void drawChannel() {
    tft.fillRoundRect(220, 14, 82, 22, 10, MON_PANEL_2);
    tft.drawRoundRect(220, 14, 82, 22, 10, MON_ACCENT);
    String channelInfo = "CH" + String(monitorChannel) + " " +
                         String(channelFreq(monitorChannel));
    int textX = 261 - getTextWidth(channelInfo, 1, FONT_SMALL) / 2;
    drawStringCustom(textX, 21, channelInfo, MON_ACCENT, 1);
}

static void drawBigPps(int pps) {
    tft.fillRect(18, 68, 140, 38, MON_PANEL);
    String pStr = String(pps);
    uint16_t col = levelColor(currentLevel);
    drawStringBig(18, 69, pStr, col, 3);
}

static void drawStatus() {
    tft.fillRect(177, 65, 117, 18, MON_PANEL);
    uint16_t col = levelColor(currentLevel);
    tft.fillCircle(183, 74, 4, col);
    drawStringBig(193, 68, levelLabel(currentLevel), col, 1);
}

static void drawMeter(int pps) {
    int target = scaleToHeight(pps, METER_W - 4);

    if (target > meterWidth) meterWidth = target;
    else                     meterWidth -= max(2, meterWidth / 8);
    if (meterWidth < 0) meterWidth = 0;

    if (meterWidth > peakMeterWidth) peakMeterWidth = meterWidth;
    else if (peakMeterWidth > 0)     peakMeterWidth -= 1;

    tft.fillRect(METER_X + 2, METER_Y + 2, METER_W - 4, METER_H - 4,
                 MON_BG);
    for (int x = 0; x < meterWidth; x++) {
        uint16_t c;
        float ratio = (float)x / (METER_W - 4);
        if      (ratio < 0.40) c = TFT_GREEN;
        else if (ratio < 0.75) c = TFT_YELLOW;
        else                   c = TFT_RED;
        tft.drawFastVLine(METER_X + 2 + x, METER_Y + 2, METER_H - 4, c);
    }

    if (peakMeterWidth > meterWidth + 2 && peakMeterWidth < METER_W - 4) {
        tft.drawFastVLine(METER_X + 2 + peakMeterWidth, METER_Y + 1,
                          METER_H - 2, TFT_WHITE);
    }
}

static void pushHistory(int pps) {
    const int slot = historyIdx;
    history[slot] = pps;
    historyIdx = (historyIdx + 1) % HISTORY_SIZE;
    if (historyIdx == 0) historyFull = true;

    const int bx = HISTORY_X + slot * BAR_WIDTH;
    tft.fillRect(bx, HISTORY_Y, BAR_WIDTH - 1, HISTORY_H - 1, MON_BG);
    const int h = scaleToHeight(pps, HISTORY_H - 2);
    if (h > 0) {
        tft.fillRect(bx, HISTORY_Y + HISTORY_H - 1 - h,
                     BAR_WIDTH - 1, h, levelColor(classify(pps)));
    }
}

static void drawStats() {
    const int metricX[] = {10, 108, 206};
    const int values[] = {
        currentManagementPps, currentDataPps, currentControlPps
    };
    for (int i = 0; i < 3; i++) {
        tft.fillRect(metricX[i] + 43, 145, 45, 13, MON_PANEL_2);
        drawStringCustom(metricX[i] + 43, 147, String(values[i]),
                         MON_ACCENT, 1);
    }

    tft.fillRect(83, 171, 219, 8, MON_PANEL);
    String stats = "TOT " + String(totalEver) +
                   "  PK " + String(peakPps) +
                   "  AVG " + String(smoothedPps);
    drawStringFit(83, 172, stats, TFT_WHITE, 215, 1);
}

static void drawFooter() {
    tft.fillRect(12, 216, 296, 15, MON_BG);
    drawStringCustom(15, 219, "UP/DN: CHANNEL", TFT_WHITE, 1);
    drawStringCustom(181, 219, monitorMuted ? "OK:SND OFF H:BACK" : "OK:SND ON H:BACK",
                     monitorMuted ? MON_MUTED : MON_ACTION, 1);
}

static void resetChannelStatistics() {
    portENTER_CRITICAL(&packetCounterMux);
    totalPacketsSec = 0;
    managementPacketsSec = 0;
    dataPacketsSec = 0;
    controlPacketsSec = 0;
    managementPacketsSec = 0;
    dataPacketsSec = 0;
    controlPacketsSec = 0;
    portEXIT_CRITICAL(&packetCounterMux);

    historyIdx = 0;
    historyFull = false;
    totalEver = 0;
    sampleCount = 0;
    sumPps = 0;
    peakPps = 0;
    currentPps = 0;
    smoothedPps = 0;
    currentManagementPps = 0;
    currentDataPps = 0;
    currentControlPps = 0;
    lastDrawnPps = -1;
    meterWidth = 0;
    peakMeterWidth = 0;
    currentLevel = LVL_QUIET;
    lastLevel = LVL_QUIET;
    memset(history, 0, sizeof(history));

    tft.fillRect(HISTORY_X, HISTORY_Y, HISTORY_W, HISTORY_H, MON_BG);
    tft.drawFastHLine(HISTORY_X, HISTORY_Y + HISTORY_H - 1,
                      HISTORY_W, MON_GLOW);
    drawBigPps(0);
    drawStatus();
    drawMeter(0);
    drawStats();
    lastUpdate = millis();
}

// ═════════════════════════════════════════════════════════════════════════════
//  MAIN
// ═════════════════════════════════════════════════════════════════════════════
void runPacketMonitor() {

    // Reset
    totalPacketsSec = 0;
    lastUpdate      = 0;
    historyIdx      = 0;
    historyFull     = false;
    totalEver       = 0;
    sampleCount     = 0;
    sumPps          = 0;
    peakPps         = 0;
    currentPps      = 0;
    smoothedPps     = 0;
    lastDrawnPps    = -1;
    currentManagementPps = 0;
    currentDataPps       = 0;
    currentControlPps    = 0;
    meterWidth      = 0;
    peakMeterWidth  = 0;
    monitorChannel  = 1;
    monitorMuted    = false;
    frameCount      = 0;
    currentLevel    = LVL_QUIET;
    lastLevel       = LVL_QUIET;
    memset(history, 0, sizeof(history));

#if BUZZER_PIN >= 0
    ledcSetup(0, 2000, 8);
    ledcAttachPin(BUZZER_PIN, 0);
    ledcWriteTone(0, 0);
#endif

    drawFrame();
    drawChannel();
    drawBigPps(0);
    drawStatus();
    drawMeter(0);
    drawStats();
    drawFooter();

    playStartupChirp();

    WiFi.mode(WIFI_MODE_NULL);
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);

    WiFi.mode(WIFI_MODE_STA);
    esp_wifi_start();

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&sniffer_callback);
    esp_wifi_set_channel(monitorChannel, WIFI_SECOND_CHAN_NONE);
    lastUpdate = millis();

    bool exitMonitor = false;

    while (!exitMonitor) {
        frameCount++;

        // ─── Cada segundo: capturar y actualizar displays ─────────────
        if (millis() - lastUpdate > 1000) {
            portENTER_CRITICAL(&packetCounterMux);
            currentPps = totalPacketsSec;
            currentManagementPps = managementPacketsSec;
            currentDataPps = dataPacketsSec;
            currentControlPps = controlPacketsSec;
            totalPacketsSec = 0;
            managementPacketsSec = 0;
            dataPacketsSec = 0;
            controlPacketsSec = 0;
            portEXIT_CRITICAL(&packetCounterMux);
            lastUpdate = millis();

            totalEver += currentPps;
            if (currentPps > peakPps) peakPps = currentPps;
            sampleCount++;
            sumPps += currentPps;
            smoothedPps = (int)(sumPps / sampleCount);

            lastLevel = currentLevel;
            currentLevel = classify(currentPps);

            if (currentPps != lastDrawnPps) {
                drawBigPps(currentPps);
                lastDrawnPps = currentPps;
            }
            drawStatus();
            pushHistory(currentPps);
            drawStats();
        }

        // ─── Sonido ambiente (cada loop, crea patrones) ───────────────
        updateAmbientSound(currentPps);

        // ─── Animación del meter (~33 fps) ────────────────────────────
        if (frameCount % 3 == 0) drawMeter(currentPps);

        // ─── CONTROLES ────────────────────────────────────────────────
        NavAction action = readNavAction(115);
        if (action == NAV_BACK) {
            exitMonitor = true;
            while (isBackPressed()) delay(5);
            flushNavInput(60);
            continue;
        }

        if (action == NAV_UP) {
            if (monitorChannel < 13) {
                monitorChannel++;
                esp_wifi_set_channel(monitorChannel, WIFI_SECOND_CHAN_NONE);
                drawChannel();
                resetChannelStatistics();
                playChannelBlip();
            }
        }
        if (action == NAV_DOWN) {
            if (monitorChannel > 1) {
                monitorChannel--;
                esp_wifi_set_channel(monitorChannel, WIFI_SECOND_CHAN_NONE);
                drawChannel();
                resetChannelStatistics();
                playChannelBlip();
            }
        }
        if (action == NAV_ENTER) {
            bool held = waitOkReleaseWasLong();
            if (held) exitMonitor = true;
            else {
                monitorMuted = !monitorMuted;
                if (monitorMuted) ledcWriteTone(0, 0);
                drawFooter();
                flushNavInput(60);
            }
        }

        delay(10);
    }

    ledcWriteTone(0, 0);
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    WiFi.mode(WIFI_MODE_NULL);
    playExitChirp();
    ledcWriteTone(0, 0);
}
