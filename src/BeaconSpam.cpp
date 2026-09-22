#include "BeaconSpam.h"
#include "DisplayTFT.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include "PepeDraw.h"
#include "Pins.h"
#include "SoundUtils.h"
#include "WifiUi.h"

extern DisplayTFT tft;

// ═══════════════════════════════════════════════════════════════════════════
//  MODO 1: MEXIPICANTE 🌶️ (tu lista, la estrella del show)
// ═══════════════════════════════════════════════════════════════════════════
static const char* SSIDS_MEXI[] = {
    "👹Eres_Un_Pendejo", "💀Wifi_Para_Pendejos", "😈Wifi_Gratis",
    "📡Ey_Tu_La_De_Negro", "🔥Meteme_la_Vagina", "👾Te_Estoy_Viendo",
    "💣Enanos_y_caballos", "😱Porno_homosexual", "🤖Amlo_es_Puto",
    "🔞Puro_Morena_AMLO", "🧠Sheinbaum_se_la_come", "⚠️Puto_el_que_lo_lea",
    "🐍Conectate_y_te_hackeo", "💥El_Diablo_Te_Bendiga",
    "🔍Chupame_la_verga", "👽Paga_tu_internet", "🔥Pinche_Pobre",
    "💾Putas_Gratis", "🚨Me_Cogi_a_tu_mama", "🖕Chupa_Limón_Kbron",
    "🤌Tu_Mama_Es_Hombre", "🤡Payaso_El_Que_Se_Conecte",
    "🧼Bañate_Cochino", "🕵Cisen_Unidad_04", "👮Patrulla_Espacial_69",
    "🤮Tu_Cara_Da_Asquito", "🍄Vendo_Hongos_Alucinogenos",
    "🧨Cuidado_Explosivo", "🌚Me_Gustas_Cuando_Callas",
    "🥀Virgen_A_Los_40", "🍗Pollo_Frito_Gratis", "🍖Huele_A_Obito",
    "🦶Amo_Tus_Patas", "👅Lameme_El_Sipitajo", "🧟Zombi_En_Tu_Cochera",
    "👺Soy_Tu_Padre_HDP", "🍕Pizza_Con_Piña_Sux",
    "🌑Oscuro_Como_Tu_Conciencia", "💊Toma_Tu_Medicina",
    "📉Tu_IQ_Es_De_0"
};
static const int COUNT_MEXI = sizeof(SSIDS_MEXI) / sizeof(char*);

// ═══════════════════════════════════════════════════════════════════════════
//  MODO 2: MEMES CLÁSICOS
// ═══════════════════════════════════════════════════════════════════════════
static const char* SSIDS_MEMES[] = {
    "Camioneta_FBI_07",
    "Virus.exe_Conectate",
    "Internet_Gratis_Aqui",
    "Porfavor_Conectate",
    "NO_TE_CONECTES",
    "Dile_a_mi_Wifi_Que_Lo_Amo",
    "Wifi_En_Mantenimiento",
    "Esta_Red_Es_Lenta",
    "Baja_Tu_Velocidad",
    "El_Wifi_Del_Vecino",
    "Pagame_el_Internet",
    "Roba_Netflix_Aqui",
    "Router_En_El_Baño",
    "TelmexMeRobo",
    "IzziEsUnaEstafa",
    "TotalPlayFallo",
    "MegacableApesta",
    "Gratis_Porque_No",
    "ClickBait_Wifi",
    "Conectame_Si_Puedes"
};
static const int COUNT_MEMES = sizeof(SSIDS_MEMES) / sizeof(char*);

// ═══════════════════════════════════════════════════════════════════════════
//  MODO 3: PARANOIA
// ═══════════════════════════════════════════════════════════════════════════
static const char* SSIDS_PARANOIA[] = {
    "Camara_Oculta_Activa",
    "Te_Estamos_Grabando",
    "Microfono_Habitacion_3",
    "Policia_Cibernetica",
    "Tu_Celular_Fue_Hackeado",
    "Conexion_No_Segura",
    "Dispositivo_Infectado",
    "Se_Detecto_Malware",
    "Cuenta_Bancaria_Robada",
    "Ubicacion_Compartida",
    "Seguridad_Comprometida",
    "CISEN_Escucha_Activa",
    "Cartel_WiFi_Gratis",
    "Ransomware_En_Progreso",
    "Tus_Fotos_Fueron_Robadas",
    "Alguien_Entro_A_Tu_Red",
    "Contraseña_Filtrada",
    "Interpol_Unidad_MX",
    "Tu_Mama_Ya_Sabe_Todo",
    "Admin_Desde_Aqui"
};
static const int COUNT_PARANOIA = sizeof(SSIDS_PARANOIA) / sizeof(char*);

// ═══════════════════════════════════════════════════════════════════════════
//  MODO 4: CHAOS UTF-8 (puros emojis y caracteres raros)
// ═══════════════════════════════════════════════════════════════════════════
static const char* SSIDS_CHAOS[] = {
    "💀💀💀💀💀",
    "🔥🔥🔥HACK🔥🔥🔥",
    "👻👻👻👻",
    "🚨ALERTA🚨",
    "💣💣💣💣",
    "☠️☠️☠️",
    "🎃🎃🎃",
    "🤡🤡🤡",
    "👹👹👹👹",
    "🌚🌚🌚",
    "💩💩💩💩💩",
    "🧠🧠🧠",
    "⚡⚡⚡⚡",
    "🔴🔴🔴",
    "🟢🟡🔴",
    "🆘🆘🆘",
    "❌❌❌❌",
    "✅❌✅❌",
    "🔞🔞🔞",
    "📡📡📡📡"
};
static const int COUNT_CHAOS = sizeof(SSIDS_CHAOS) / sizeof(char*);

// ═══════════════════════════════════════════════════════════════════════════
//  SELECCIÓN DE MODO
// ═══════════════════════════════════════════════════════════════════════════
enum SpamMode {
    MODE_MEXI     = 0,
    MODE_MEMES    = 1,
    MODE_PARANOIA = 2,
    MODE_CHAOS    = 3,
    MODE_MIX      = 4
};

static const char* MODE_NAMES[] = {
    "Mexipicante",
    "Memes Clasicos",
    "Paranoia",
    "Chaos UTF-8",
    "Mix Total (all)"
};
static const char* MODE_DESCS[] = {
    "40 SSIDs en español",
    "Los clasicos de internet",
    "Pone nervioso a cualquiera",
    "Solo emojis y simbolos",
    "Mezcla aleatoria de todo"
};
static const int MODE_COUNT = 5;

// ═══════════════════════════════════════════════════════════════════════════
//  ESTADO
// ═══════════════════════════════════════════════════════════════════════════
static volatile unsigned long beaconsSent = 0;
static String  currentSSID = "";
static int     currentChannel = 1;
static SpamMode activeMode = MODE_MEXI;

// ═══════════════════════════════════════════════════════════════════════════
//  FRAME 802.11 BEACON RAW
//  Plantilla base que luego rellenamos con SSID/BSSID/channel dinámicos
// ═══════════════════════════════════════════════════════════════════════════
static uint8_t beaconFrame[200] = {
    // Frame Control (2 bytes): Beacon type 0x80
    0x80, 0x00,
    // Duration (2 bytes)
    0x00, 0x00,
    // Destination (6 bytes): broadcast FF:FF:FF:FF:FF:FF
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // Source / BSSID (6 bytes): se llena dinámicamente
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // BSSID duplicado (6 bytes)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Sequence number (2 bytes)
    0x00, 0x00,
    // ── Frame body ──
    // Timestamp (8 bytes)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Beacon interval (2 bytes): 0x0064 = 100 TU = ~102.4 ms
    0x64, 0x00,
    // Capability info (2 bytes): 0x0401 = ESS + Short Preamble
    0x01, 0x04,
    // ── Tagged parameters ──
    // SSID tag: tag=0x00, length=N, luego bytes del SSID
    0x00, 0x00,        // placeholder (length en [37])
    // (aquí va el SSID, desde offset 38)
};

// Offset dentro del frame donde comienza el SSID length tag
static const int SSID_LENGTH_OFFSET = 37;
static const int SSID_START_OFFSET  = 38;

// Tail: "supported rates" + "DS parameter" (channel)
// Se construye dinámicamente después del SSID
static const uint8_t FRAME_TAIL[] = {
    // Supported rates (tag=0x01, length=8)
    0x01, 0x08,
    0x82, 0x84, 0x8B, 0x96, 0x24, 0x30, 0x48, 0x6C,
    // DS Parameter Set (tag=0x03, length=1, channel)
    0x03, 0x01, 0x00    // último byte = canal actual
};
static const int FRAME_TAIL_SIZE = sizeof(FRAME_TAIL);

// ═══════════════════════════════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════════════════════════════

// Obtiene el número de SSIDs disponibles para un modo
static int countSSIDsForMode(SpamMode mode) {
    switch (mode) {
        case MODE_MEXI:     return COUNT_MEXI;
        case MODE_MEMES:    return COUNT_MEMES;
        case MODE_PARANOIA: return COUNT_PARANOIA;
        case MODE_CHAOS:    return COUNT_CHAOS;
        case MODE_MIX:      return COUNT_MEXI + COUNT_MEMES +
                                   COUNT_PARANOIA + COUNT_CHAOS;
        default:            return 0;
    }
}

// Obtiene el SSID de un modo por índice
static const char* getSSIDForMode(SpamMode mode, int idx) {
    switch (mode) {
        case MODE_MEXI:     return SSIDS_MEXI[idx];
        case MODE_MEMES:    return SSIDS_MEMES[idx];
        case MODE_PARANOIA: return SSIDS_PARANOIA[idx];
        case MODE_CHAOS:    return SSIDS_CHAOS[idx];
        case MODE_MIX: {
            if (idx < COUNT_MEXI)
                return SSIDS_MEXI[idx];
            idx -= COUNT_MEXI;
            if (idx < COUNT_MEMES)
                return SSIDS_MEMES[idx];
            idx -= COUNT_MEMES;
            if (idx < COUNT_PARANOIA)
                return SSIDS_PARANOIA[idx];
            idx -= COUNT_PARANOIA;
            if (idx < COUNT_CHAOS)
                return SSIDS_CHAOS[idx];
            return "?";
        }
        default: return "?";
    }
}

// Construye y transmite un beacon con el SSID y canal dados
static void sendBeacon(const char* ssid, int channel) {
    int ssidLen = strlen(ssid);
    if (ssidLen > 32) ssidLen = 32;   // 802.11 limit

    // ── BSSID aleatorio (MAC del "router" falso) ───────────────────────
    // Los primeros 2 bits del primer byte los ponemos a 0 para que
    // parezca una MAC unicast normal, no multicast
    for (int i = 0; i < 6; i++) {
        beaconFrame[10 + i] = (uint8_t)random(0, 256);
        beaconFrame[16 + i] = beaconFrame[10 + i];   // BSSID duplicado
    }
    beaconFrame[10] &= 0xFE;   // quitar bit multicast

    // ── SSID tag ───────────────────────────────────────────────────────
    beaconFrame[SSID_LENGTH_OFFSET] = (uint8_t)ssidLen;
    memcpy(&beaconFrame[SSID_START_OFFSET], ssid, ssidLen);

    // ── Tail con canal ─────────────────────────────────────────────────
    int tailOffset = SSID_START_OFFSET + ssidLen;
    memcpy(&beaconFrame[tailOffset], FRAME_TAIL, FRAME_TAIL_SIZE);
    beaconFrame[tailOffset + FRAME_TAIL_SIZE - 1] = (uint8_t)channel;

    int frameLen = tailOffset + FRAME_TAIL_SIZE;

    // ── Transmitir ──────────────────────────────────────────────────────
    // Canal 0 = interfaz WIFI_IF_STA (requiere que el canal ya esté fijado)
    esp_wifi_80211_tx(WIFI_IF_STA, beaconFrame, frameLen, false);

    beaconsSent++;
}

// ═══════════════════════════════════════════════════════════════════════════
//  DISCLAIMER
// ═══════════════════════════════════════════════════════════════════════════
static bool showDisclaimer() {
    wifiUiFrame("BEACON SPAM", "NOTICE", WIFI_UI_WARN);

    wifiUiCard(10, 49, 300, 151, false, WIFI_UI_WARN);
    int y = 58;
    drawStringCustom(10, y, "Transmite redes WiFi falsas", UI_MAIN, 1); y += 12;
    drawStringCustom(10, y, "que aparecen en tu lista WiFi.", UI_MAIN, 1); y += 20;

    drawStringCustom(10, y, "No interfiere conexiones reales,", UI_ACCENT, 1); y += 12;
    drawStringCustom(10, y, "solo agrega redes ficticias.", UI_ACCENT, 1); y += 20;

    drawStringCustom(10, y, "Usalo con responsabilidad:", UI_MAIN, 1); y += 12;
    drawStringCustom(20, y, "- Diviertete con amigos", UI_ACCENT, 1); y += 12;
    drawStringCustom(20, y, "- NO en lugares sensibles", UI_ACCENT, 1); y += 20;

    drawStringCustom(10, y, "Tu eres responsable del uso.", UI_MAIN, 1);

    wifiUiFooter("AUTHORIZED USE", "OK: ACCEPT", WIFI_UI_WARN);

    while (true) {
        if (navEnterPressed()) {
            beep(2200, 60);
            while (navEnterPressed() || navBackPressed()) delay(5);
            delay(100);
            return true;
        }
        if (navBackPressed() || navUpPressed() || navDownPressed()) {
            beep(1000, 80);
            while (navBackPressed() || navUpPressed() || navDownPressed())
                delay(5);
            delay(100);
            return false;
        }
        delay(20);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  MENÚ DE SELECCIÓN DE MODO
// ═══════════════════════════════════════════════════════════════════════════
static void drawModeMenuRow(int idx, bool selected) {
    int y = 47 + idx * 31;
    tft.fillRect(8, y, 303, 30, WIFI_UI_BG);
    wifiUiCard(10, y + 1, 298, 28, selected);
    uint16_t colMain = selected ? WIFI_UI_BG : WIFI_UI_TEXT;
    uint16_t colSub  = selected ? WIFI_UI_PANEL : WIFI_UI_MUTED;
    drawStringCustom(20, y + 4, MODE_NAMES[idx], colMain, 2);
    drawStringCustom(20, y + 18, MODE_DESCS[idx], colSub, 1);
}

static void drawModeMenu(int cursor) {
    wifiUiFrame("BEACON SPAM", String(MODE_COUNT) + " MODES");

    int totalItems = MODE_COUNT;

    for (int i = 0; i < totalItems; i++) {
        drawModeMenuRow(i, i == cursor);
    }

    wifiUiFooter("UP/DN: MODE", "OK: START");
}

static int selectMode() {
    int cursor = 0;
    int totalItems = MODE_COUNT;

    drawModeMenu(cursor);

    while (true) {
        if (navBackPressed()) {
            beep(1000, 40);
            while (navBackPressed()) delay(5);
            return -1;
        }
        if (navUpPressed()) {
            int oldCursor = cursor;
            cursor = (cursor - 1 + totalItems) % totalItems;
            beep(2100, 20);
            tft.startWrite();
            drawModeMenuRow(oldCursor, false);
            drawModeMenuRow(cursor, true);
            tft.endWrite();
            delay(70);
        }
        if (navDownPressed()) {
            int oldCursor = cursor;
            cursor = (cursor + 1) % totalItems;
            beep(2100, 20);
            tft.startWrite();
            drawModeMenuRow(oldCursor, false);
            drawModeMenuRow(cursor, true);
            tft.endWrite();
            delay(70);
        }
        if (navEnterPressed()) {
            bool held = waitOkReleaseWasLong();
            beep(held ? 1000 : 1800, 40);
            delay(100);
            if (held) return -1;
            return cursor;
        }
        delay(20);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  PANTALLA DE ATAQUE
// ═══════════════════════════════════════════════════════════════════════════
static void drawAttackFrame() {
    wifiUiFrame("BEACON SPAM", "BROADCAST", WIFI_UI_OK);

    wifiUiCard(10, 49, 300, 53, false);
    drawStringCustom(18, 57, "MODE", WIFI_UI_MUTED, 1);
    drawStringFit(70, 57, String(MODE_NAMES[activeMode]),
                  WIFI_UI_TEXT, 228, 1);

    // Labels estáticos
    drawStringCustom(18, 78, "CHANNEL", WIFI_UI_MUTED, 1);
    wifiUiCard(10, 108, 145, 48, false, WIFI_UI_OK);
    wifiUiCard(165, 108, 145, 48, false, WIFI_UI_ACCENT);
    drawStringCustom(18, 116, "BEACONS", WIFI_UI_MUTED, 1);
    drawStringCustom(173, 116, "RATE", WIFI_UI_MUTED, 1);
    drawStringCustom(18, 163, "CURRENT SSID", WIFI_UI_MUTED, 1);

    // Activity bar frame
    wifiUiProgress(10, 189, 300, 10, 0, WIFI_UI_OK);
    wifiUiFooter("LIVE TRANSMISSION", "HOLD OK: STOP", WIFI_UI_DANGER);
}

static void drawAttackStats(unsigned long pkts, float rate) {
    // Channel
    tft.fillRect(86, 73, 210, 14, WIFI_UI_PANEL);
    drawStringCustom(86, 78, "CH " + String(currentChannel), WIFI_UI_WARN, 1);

    // Current SSID (puede tener emojis = más ancho, truncar visualmente)
    tft.fillRect(18, 174, 284, 12, WIFI_UI_BG);
    String s = currentSSID;
    drawStringFit(18, 175, s, WIFI_UI_ACCENT, 284, 1);

    // Beacons
    tft.fillRect(18, 132, 128, 18, WIFI_UI_PANEL);
    drawStringBig(18, 132, String(pkts), WIFI_UI_OK, 1);

    // Rate
    tft.fillRect(173, 132, 128, 18, WIFI_UI_PANEL);
    char rbuf[24];
    snprintf(rbuf, sizeof(rbuf), "%d beacons/s", (int)rate);
    drawStringBig(173, 132, String(rbuf), WIFI_UI_ACCENT, 1);

    // Activity bar animada
    int activity = random(20, 100);
    wifiUiProgress(10, 189, 300, 10, activity, WIFI_UI_OK);
}

// ═══════════════════════════════════════════════════════════════════════════
//  LOOP DE ATAQUE
// ═══════════════════════════════════════════════════════════════════════════
static void runAttackLoop() {
    drawAttackFrame();
    beep(2400, 40); delay(20);
    beep(3000, 60); delay(20);
    beep(3600, 80);

    // ── Setup WiFi para raw tx ──────────────────────────────────────────
    WiFi.mode(WIFI_MODE_NULL);
    esp_wifi_set_promiscuous(false);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_promiscuous(true);   // habilita tx raw
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    currentChannel = 1;

    int ssidCount = countSSIDsForMode(activeMode);

    beaconsSent = 0;
    unsigned long startMs         = millis();
    unsigned long lastStatsUpdate = millis();
    unsigned long lastChannelHop  = millis();
    unsigned long lastPktCount    = 0;
    float rate = 0;

    // Channels que vamos a rotar: 1, 6, 11 (los no-overlapping en 2.4GHz)
    const int channels[] = {1, 6, 11};
    int channelIdx = 0;

    int ssidIdx = 0;

    bool stopAttack = false;
    unsigned long okPressStart = 0;
    bool okHeld = false;

    while (!stopAttack) {
        if (navBackPressed()) {
            stopAttack = true;
            while (navBackPressed()) delay(5);
            continue;
        }

        // ── Enviar un beacon ──────────────────────────────────────────
        const char* ssid = getSSIDForMode(activeMode, ssidIdx);
        currentSSID = String(ssid);
        sendBeacon(ssid, currentChannel);

        ssidIdx = (ssidIdx + 1) % ssidCount;

        // ── Channel hop cada 500 ms ───────────────────────────────────
        if (millis() - lastChannelHop > 500) {
            channelIdx = (channelIdx + 1) % 3;
            currentChannel = channels[channelIdx];
            esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
            lastChannelHop = millis();
        }

        // ── Update UI cada 250 ms ──────────────────────────────────────
        // Keep TFT work out of the raw-transmit hot path so the complete SSID
        // set remains visible in nearby network lists.
        if (millis() - lastStatsUpdate > 1000) {
            unsigned long now   = millis();
            unsigned long delta = beaconsSent - lastPktCount;
            unsigned long dt    = now - lastStatsUpdate;
            rate = (delta * 1000.0f) / dt;
            lastPktCount    = beaconsSent;
            lastStatsUpdate = now;
            drawAttackStats(beaconsSent, rate);
        }

        // ── Detectar OK HOLD ───────────────────────────────────────────
        if (navEnterPressed()) {
            if (!okHeld) {
                okPressStart = millis();
                okHeld = true;
            } else if (millis() - okPressStart > 500) {
                stopAttack = true;
            }
        } else {
            okHeld = false;
        }

        // yield al watchdog + pequeño delay para rate ~150-200 pkt/s
        yield();
        delay(5);
    }

    // ── Cleanup ─────────────────────────────────────────────────────────
    esp_wifi_set_promiscuous(false);
    esp_wifi_stop();
    esp_wifi_deinit();
    delay(100);

    beep(1800, 40); delay(20);
    beep(1200, 60);

    while (navEnterPressed() || navBackPressed()) delay(5);
    delay(150);
}

// ═══════════════════════════════════════════════════════════════════════════
//  MAIN
// ═══════════════════════════════════════════════════════════════════════════
void runBeaconSpam() {
    // Esperar liberación de OK
    while (navEnterPressed() || navBackPressed()) delay(5);
    delay(100);

    if (!showDisclaimer()) return;

    while (true) {
        int mode = selectMode();
        if (mode < 0) break;

        activeMode = (SpamMode)mode;
        runAttackLoop();
    }
}
