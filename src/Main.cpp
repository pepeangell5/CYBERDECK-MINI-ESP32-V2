#include <Arduino.h>
#include <SPI.h>
#include "DisplayTFT.h"
#include "PepeDraw.h"
#include "MenuSystem.h"
#include "Pins.h"
#include "Settings.h"
#include "NVSStore.h"
#include "SplashScreen.h"
#include "Input.h"
#include "PeripheralTools.h"
#include "SharedSpi.h"

// ═══════════════════════════════════════════════════════════════════════════
//  ESP32-TOOLS · Firmware principal
//  El main.cpp solo inicializa hardware y entrega el control al menú.
// ═══════════════════════════════════════════════════════════════════════════

DisplayTFT tft;

// ── Carga todas las preferencias desde NVS a las variables globales ──────
static void loadPreferences() {
    soundEnabled = nvsGetBool("sound_on",  true);
    soundVolume  = nvsGetInt ("sound_vol", 3);
    if (soundVolume < 1) soundVolume = 1;
    if (soundVolume > 5) soundVolume = 5;
}

// ── Incrementa contador de arranques (útil para System Info después) ─────
static void bumpBootCount() {
    unsigned long bc = nvsGetULong("boot_cnt", 0);
    bc++;
    nvsSetULong("boot_cnt", bc);
    Serial.printf("[NVS] Boot count: %lu\n", bc);
}

void setup() {
    Serial.begin(115200);

    // ── Inputs: 4 botones + encoder ────────────────────────────────────
    initInput();

    // ── SPI shared devices ──────────────────────────────────────────────
    sharedSpiInitPins(true);

#if TFT_LED_PIN >= 0
    pinMode(TFT_LED_PIN, OUTPUT);
    digitalWrite(TFT_LED_PIN, HIGH);
#endif

#if BUZZER_PIN >= 0
    ledcSetup(0, 2000, 8);
    ledcAttachPin(BUZZER_PIN, 0);
    ledcWriteTone(0, 0);
#endif

    initPeripherals();

    // ── NVS: cargar configuración guardada ──────────────────────────────
    nvsBegin();
    loadPreferences();
    bumpBootCount();

    // ── Reset pantalla ──────────────────────────────────────────────────
    pinMode(TFT_RST_PIN, OUTPUT);
    digitalWrite(TFT_RST_PIN, LOW);  delay(100);
    digitalWrite(TFT_RST_PIN, HIGH); delay(100);

    sharedSpiBeginMainBus();
    sharedSpiPrepareDisplay(true);
    tft.begin();
    tft.invertDisplay(false);
    tft.setRotation(3);

    tft.fillScreen(TFT_BLACK);

    // ── Splash screen (espera a que usuario presione OK) ────────────────
    runSplashScreen();

    // ── Menú principal (bucle infinito, nunca regresa) ──────────────────
    runMainMenu();

    // ── Menú principal (bucle infinito, nunca regresa) ──────────────────
}

void loop() {
    delay(1000);
}
