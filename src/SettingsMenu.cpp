#include "PepeDraw.h"
#include "Settings.h"
#include "Pins.h"
#include "NVSStore.h"
#include "WifiConfig.h"
#include "SoundUtils.h"
#include "SystemUi.h"

static int cursor = 0;
static const int MENU_ITEMS = 3;
static bool settingsFrameReady = false;

// ═══════════════════════════════════════════════════════════════════════════
//  OLVIDAR RED WIFI · borra credenciales guardadas en NVS
// ═══════════════════════════════════════════════════════════════════════════
static void runForgetWifi() {
    // Esperar liberación de OK
    while (navEnterPressed() || navBackPressed()) delay(5);
    delay(100);

    // Caso 1: no hay red guardada
    if (!wifiConfigHasSaved()) {
        systemUiFrame("WIFI CONFIG", "SETTINGS");
        systemUiCard(18, 72, 284, 92);
        drawStringCustom(50, 91, "SIN RED GUARDADA", SYS_UI_ACCENT, 2);
        drawStringCustom(42, 130, "No hay credenciales WiFi guardadas.",
                         SYS_UI_TEXT, 1);
        systemUiFooter("NOTHING TO DELETE", "OK/BACK: RETURN");

        beep(1500, 60);

        while (!navEnterPressed() && !navBackPressed()) delay(20);
        beep(1800, 40);
        while (navEnterPressed() || navBackPressed()) delay(5);
        delay(100);
        return;
    }

    // Caso 2: hay red guardada → confirmar
    String savedSSID = wifiConfigGetSavedSSID();

    systemUiFrame("OLVIDAR WIFI", "CONFIRM");
    systemUiCard(14, 60, 292, 128, false, SYS_UI_DANGER);

    drawStringCustom(24, 72, "Red guardada:", SYS_UI_MUTED, 1);

    if (getTextWidth(savedSSID, 2) <= 280) {
        drawStringCustom(24, 90, savedSSID, SYS_UI_ACCENT, 2);
    } else {
        drawStringFit(24, 95, savedSSID, SYS_UI_ACCENT, 272, 1);
    }

    drawStringCustom(24, 126, "Eliminar credenciales?", SYS_UI_TEXT, 1);
    drawStringCustom(24, 143, "La siguiente conexion pedira", SYS_UI_MUTED, 1);
    drawStringCustom(24, 156, "seleccionar red y clave de nuevo.", SYS_UI_MUTED, 1);
    systemUiFooter("OK: DELETE", "BACK/UP/DN: CANCEL");

    while (true) {
        if (navEnterPressed()) {
            beep(1200, 80);
            while (navEnterPressed() || navBackPressed()) delay(5);
            delay(100);

            // Borrar credenciales
            wifiConfigForget();

            // Pantalla de confirmación
            systemUiFrame("WIFI CONFIG", "COMPLETE");
            systemUiCard(28, 78, 264, 82, false, SYS_UI_OK);
            drawStringCustom(56, 96, "RED OLVIDADA", SYS_UI_OK, 3);
            drawStringCustom(62, 139, "Credenciales eliminadas.", SYS_UI_TEXT, 1);

            beep(2400, 50); delay(30);
            beep(3000, 80);
            delay(1500);
            return;
        }
        if (navBackPressed() || navUpPressed() || navDownPressed()) {
            beep(2000, 40);
            while (navBackPressed() || navUpPressed() || navDownPressed())
                delay(5);
            delay(100);
            return;
        }
        delay(20);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  MENU SETTINGS PRINCIPAL
// ═══════════════════════════════════════════════════════════════════════════

void drawSettings() {
    if (!settingsFrameReady) {
        systemUiFrame("SETTINGS", "DEVICE CTRL");
        systemUiFooter("UP/DN: MOVE  OK: CHANGE", "HOLD/BACK: EXIT");
        settingsFrameReady = true;
    }

    String soundStr = soundEnabled ? "ON" : "OFF";

    for (int i = 0; i < MENU_ITEMS; i++) {
        int y = 54 + (i * 50);
        bool selected = i == cursor;
        tft.fillRect(9, y - 2, 302, 47, SYS_UI_BG);
        systemUiCard(12, y, 296, 42, selected);
        uint16_t textColor = selected ? TFT_BLACK : SYS_UI_TEXT;

        if (i == 0) {
            drawStringCustom(28, y + 12, "SOUND: " + soundStr, textColor, 2);
        }
        else if (i == 1) {
            drawStringCustom(28, y + 12, "VOLUME: " + String(soundVolume),
                             textColor, 2);
        }
        else if (i == 2) {
            drawStringCustom(28, y + 12, "FORGET WIFI", textColor, 2);
        }
    }

}

void runSettings() {

    cursor = 0;
    settingsFrameReady = false;

    // Evitar doble OK
    while (navEnterPressed() || navBackPressed());
    delay(150);

    bool exitMenu = false;

    drawSettings();

    while (!exitMenu) {

        if (navBackPressed()) {
            exitMenu = true;
            beep(1000, 40);
            while (navBackPressed()) delay(5);
            delay(120);
            continue;
        }

        if (navDownPressed()) {
            cursor = (cursor + 1) % MENU_ITEMS;
            drawSettings();
            delay(200);
        }

        if (navUpPressed()) {
            cursor = (cursor - 1 + MENU_ITEMS) % MENU_ITEMS;
            drawSettings();
            delay(200);
        }

        if (navEnterPressed()) {
            bool held = waitOkReleaseWasLong();
            if (held) {
                exitMenu = true;
                beep(1000, 40);
                delay(120);
                continue;
            }

            if (cursor == 0) {
                soundEnabled = !soundEnabled;
                nvsSetBool("sound_on", soundEnabled);
            }
            else if (cursor == 1) {
                soundVolume++;
                if (soundVolume > 5) soundVolume = 1;
                nvsSetInt("sound_vol", soundVolume);
            }
            else if (cursor == 2) {
                // Esperar liberación antes de entrar a la sub-pantalla
                while (navEnterPressed() || navBackPressed());
                delay(100);
                runForgetWifi();
                settingsFrameReady = false;
            }
            drawSettings();
            delay(150);
        }

        delay(10);
    }
}
