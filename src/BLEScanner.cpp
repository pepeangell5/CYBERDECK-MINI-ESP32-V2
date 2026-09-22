#include "BLEScanner.h"
#include "DisplayTFT.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "PepeDraw.h"
#include "Pins.h"
#include "Input.h"
#include "SoundUtils.h"
#include "BtUi.h"

extern DisplayTFT tft;

// ═══════════════════════════════════════════════════════════════════════════
//  CONFIGURACIÓN
// ═══════════════════════════════════════════════════════════════════════════
#define MAX_DEVICES     30      // tope de dispositivos mostrados
#define SCAN_TIME_S     3       // segundos por ciclo de scan (luego itera)
#define VISIBLE_ROWS    6       // filas visibles en la lista

// ═══════════════════════════════════════════════════════════════════════════
//  ESTRUCTURA DE DISPOSITIVO
// ═══════════════════════════════════════════════════════════════════════════
struct BLEDev {
    String   name;
    String   mac;
    int      rssi;
    int      addrType;
    bool     hasManufData;
    uint16_t vendorId;      // primeros 2 bytes de manufacturer data
    String   manufHex;      // manufacturer data en hex (máx 16 bytes mostrados)
    int      serviceCount;
    String   serviceSummary; // nombre del primer servicio o "Services: N"
};

static BLEDev devices[MAX_DEVICES];
static int deviceCount = 0;

// ═══════════════════════════════════════════════════════════════════════════
//  VENDOR LOOKUP · por los primeros 2 bytes del manufacturer data (Company ID)
//  Lista oficial Bluetooth SIG. Los más comunes.
// ═══════════════════════════════════════════════════════════════════════════
static const char* bleVendorName(uint16_t id) {
    switch (id) {
        case 0x004C: return "Apple";
        case 0x0075: return "Samsung";
        case 0x0006: return "Microsoft";
        case 0x00E0: return "Google";
        case 0x038F: return "Xiaomi";
        case 0x02E1: return "Amazon";
        case 0x0157: return "Fitbit";
        case 0x0059: return "Nordic";
        case 0x0499: return "Ruuvi";
        case 0x0131: return "Huawei";
        case 0x0001: return "Ericsson";
        case 0x000F: return "Broadcom";
        case 0x000D: return "Texas Instr";
        case 0x004F: return "Nokia";
        case 0x0087: return "Garmin";
        case 0x012D: return "Sony";
        case 0x0505: return "Bose";
        case 0x01DA: return "Logitech";
        case 0x0154: return "Withings";
        case 0x000A: return "Canon";
        case 0x0046: return "Marvell";
        case 0x022B: return "Tile";
        case 0x0310: return "SGL Italia";
        default:     return nullptr;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════════════════════════════

// Categoriza RSSI en descripción humana
static const char* rssiLabel(int rssi) {
    if (rssi >= -50) return "VERY CLOSE";
    if (rssi >= -65) return "CLOSE";
    if (rssi >= -80) return "NEAR";
    return "FAR";
}

// Convierte RSSI a 4 barras de señal
static int rssiBars(int rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -70) return 3;
    if (rssi >= -85) return 2;
    if (rssi >= -95) return 1;
    return 0;
}

// Formatea manufacturer data en hex (primeros N bytes)
static String formatHex(const uint8_t* data, size_t len, size_t maxBytes) {
    String out = "";
    size_t show = len < maxBytes ? len : maxBytes;
    for (size_t i = 0; i < show; i++) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", data[i]);
        out += buf;
    }
    if (len > maxBytes) out += "...";
    return out;
}

// Tipo de dirección BLE → string
static const char* addrTypeLabel(int t) {
    switch (t) {
        case 0:  return "Public";
        case 1:  return "Random";
        case 2:  return "Public-ID";
        case 3:  return "Random-ID";
        default: return "Unknown";
    }
}

// Busca si una MAC ya está en la lista. Devuelve índice o -1.
static int findDevice(const String& mac) {
    for (int i = 0; i < deviceCount; i++) {
        if (devices[i].mac == mac) return i;
    }
    return -1;
}

// Inserta o actualiza un dispositivo
static void upsertDevice(BLEAdvertisedDevice& ad) {
    String mac = String(ad.getAddress().toString().c_str());
    String name = ad.haveName() ? String(ad.getName().c_str()) : "";

    int idx = findDevice(mac);
    bool isNew = (idx < 0);

    if (isNew) {
        if (deviceCount >= MAX_DEVICES) return;
        idx = deviceCount++;
    }

    BLEDev& d = devices[idx];
    d.mac  = mac;
    d.rssi = ad.getRSSI();
    d.addrType = (int)ad.getAddressType();

    // Guardar nombre solo si llegó (los BLE devices a veces advertisean sin nombre)
    if (name.length() > 0) d.name = name;
    else if (d.name.length() == 0) d.name = "";

    // Servicios
    d.serviceCount = ad.getServiceUUIDCount();
    if (d.serviceCount > 0) {
        d.serviceSummary = String(d.serviceCount) + " service" +
                           (d.serviceCount > 1 ? "s" : "");
    } else {
        d.serviceSummary = "No services";
    }

    // Manufacturer data
    d.hasManufData = ad.haveManufacturerData();
    if (d.hasManufData) {
        std::string md = ad.getManufacturerData();
        if (md.size() >= 2) {
            d.vendorId = ((uint16_t)(uint8_t)md[1] << 8) | (uint8_t)md[0];
            d.manufHex = formatHex((const uint8_t*)md.data(), md.size(), 12);
        } else {
            d.vendorId = 0;
            d.manufHex = "";
        }
    } else {
        d.vendorId = 0;
        d.manufHex = "";
    }

    // Beep sutil cuando encontramos un dispositivo nuevo
    if (isNew) beep(2200, 20);
}

// Ordena por RSSI descendente (los más cercanos primero)
static void sortDevices() {
    // Bubble sort — deviceCount es máx 30, es suficiente
    for (int i = 0; i < deviceCount - 1; i++) {
        for (int j = 0; j < deviceCount - 1 - i; j++) {
            if (devices[j].rssi < devices[j + 1].rssi) {
                BLEDev tmp = devices[j];
                devices[j] = devices[j + 1];
                devices[j + 1] = tmp;
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  CALLBACK DE SCAN
// ═══════════════════════════════════════════════════════════════════════════
class BLEScanCallback : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice ad) override {
        upsertDevice(ad);
    }
};

// ═══════════════════════════════════════════════════════════════════════════
//  DIBUJO · LISTA PRINCIPAL
// ═══════════════════════════════════════════════════════════════════════════
static void drawListFrame() {
    btUiFrame("BLE SCANNER", "SCANNING", BT_UI_GLOW);
    btUiFooter("UP/DN: NAV", "OK: INFO  HOLD: EXIT", BT_UI_ACCENT);
}

static void drawScannerAnimation(uint8_t frame) {
    const int cx = 160;
    const int cy = 104;
    static const int8_t dx[8] = {34, 24, 0, -24, -34, -24, 0, 24};
    static const int8_t dy[8] = {0, 24, 34, 24, 0, -24, -34, -24};
    tft.fillRect(122, 66, 76, 76, BT_UI_PANEL);
    tft.drawCircle(cx, cy, 35, BT_UI_LINE);
    tft.drawCircle(cx, cy, 22, BT_UI_ACCENT);
    tft.fillCircle(cx, cy, 4, BT_UI_GLOW);
    uint8_t p = frame & 7;
    tft.drawLine(cx, cy, cx + dx[p], cy + dy[p], BT_UI_GLOW);
    tft.drawLine(cx + 1, cy, cx + dx[p] + 1, cy + dy[p], BT_UI_GLOW);
    tft.drawLine(cx - 1, cy, cx + dx[p] - 1, cy + dy[p], BT_UI_GLOW);
    tft.fillCircle(cx + dx[p], cy + dy[p], 5, BT_UI_OK);
}

static void drawListRow(int idx, int row, bool selected) {
    const int rowHeight = 26;
    const int y = 47 + row * rowHeight;
    tft.fillRect(8, y, 303, rowHeight - 2, BT_UI_BG);
    if (idx < 0 || idx >= deviceCount) return;

    if (selected) tft.fillRoundRect(9, y, 300, rowHeight - 2, 5, BT_UI_ACCENT);
    uint16_t colMain = selected ? BT_UI_BG : BT_UI_TEXT;
    uint16_t colSub  = selected ? BT_UI_BG : BT_UI_MUTED;
    BLEDev& d = devices[idx];

    String displayName = d.name.length() > 0 ? d.name : "<unnamed>";
    if (getTextWidth(displayName, 2) <= 190) {
        drawStringCustom(10, y + 4, displayName, colMain, 2);
    } else {
        drawStringFit(10, y + 8, displayName, colMain, 190, 1);
    }
    drawStringCustom(10, y + 18, d.mac, colSub, 1);
    drawStringCustom(210, y + 4, String(d.rssi) + "dBm", colMain, 2);

    int bars = rssiBars(d.rssi);
    int bx = 282, by = y + 22;
    for (int b = 0; b < 4; b++) {
        int bh = 3 + b * 2;
        uint16_t c = (b < bars)
            ? (selected ? BT_UI_BG : (bars >= 3 ? TFT_GREEN :
                                      bars >= 2 ? TFT_YELLOW : TFT_ORANGE))
            : (selected ? BT_UI_BG : BT_UI_ACCENT);
        if (b < bars) tft.fillRect(bx + b*5, by - bh, 3, bh, c);
        else          tft.drawRect(bx + b*5, by - bh, 3, bh, c);
    }
}

// Dibuja la lista de dispositivos + contador
static void drawList(int cursor, int scrollOffset, int totalSeen) {
    // Limpiar área de contador (sin redibujar todo el header)
    tft.fillRect(216, 14, 88, 12, BT_UI_PANEL);

    // Contador "FOUND: N"
    String hdr = "FOUND: " + String(deviceCount);
    drawStringRight(302, 17, hdr, BT_UI_GLOW, 1);

    if (deviceCount == 0) {
        tft.fillRect(8, 47, 304, 157, BT_UI_BG);
        btUiCard(28, 52, 264, 148, false, BT_UI_ACCENT);
        drawScannerAnimation(0);
        drawStringCentered(148, "SEARCHING BLE", BT_UI_GLOW, 1, FONT_BIG);
        drawStringCentered(166, "LISTENING FOR ADVERTISEMENTS",
                           BT_UI_MUTED, 1, FONT_SMALL);
        btUiProgress(48, 184, 224, 8, 55, BT_UI_ACCENT);
        return;
    }

    // Full redraws must erase the search card and any previous menu pixels.
    tft.fillRect(8, 44, 304, 161, BT_UI_BG);

    const int rowHeight = 26;
    const int listY = 47;

    for (int i = 0; i < VISIBLE_ROWS; i++) {
        int idx = i + scrollOffset;
        drawListRow(idx, i, idx == cursor);
    }

    // Scroll bar lateral si hay más que VISIBLE_ROWS
    tft.fillRect(312, 48, 3, 156, BT_UI_BG);
    if (deviceCount > VISIBLE_ROWS) {
        int total = deviceCount;
        int barH = (VISIBLE_ROWS * 156) / total;
        int barY = 48 + (scrollOffset * (156 - barH)) / (total - VISIBLE_ROWS);
        tft.fillRect(312, barY, 3, barH, BT_UI_ACCENT);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  DIBUJO · PANTALLA DE DETALLES
// ═══════════════════════════════════════════════════════════════════════════
static void drawDetails(const BLEDev& d) {
    btUiFrame("BLE DEVICE", "DETAILS", BT_UI_GLOW);

    int y = 50;
    const int lineH = 14;

    // Nombre
    String name = d.name.length() > 0 ? d.name : "<unnamed>";
    drawStringCustom(10, y, "Name:    " + name, UI_MAIN, 1);
    y += lineH;

    // MAC
    drawStringCustom(10, y, "MAC:     " + d.mac, UI_MAIN, 1);
    y += lineH;

    // RSSI con categoría
    String rssiLine = "RSSI:    " + String(d.rssi) + " dBm  (" +
                      String(rssiLabel(d.rssi)) + ")";
    drawStringCustom(10, y, rssiLine, UI_MAIN, 1);
    y += lineH;

    // Tipo de dirección
    drawStringCustom(10, y, "AddrType: " + String(addrTypeLabel(d.addrType)),
                     UI_MAIN, 1);
    y += lineH;

    // Vendor (si pudimos identificar)
    const char* vendor = nullptr;
    if (d.hasManufData && d.vendorId != 0) {
        vendor = bleVendorName(d.vendorId);
    }
    String vendorStr;
    if (vendor) {
        vendorStr = "Vendor:  " + String(vendor);
    } else if (d.hasManufData && d.vendorId != 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%04X", d.vendorId);
        vendorStr = "Vendor:  " + String(buf) + " (unknown)";
    } else {
        vendorStr = "Vendor:  --";
    }
    drawStringCustom(10, y, vendorStr, UI_SELECT, 1);
    y += lineH + 4;

    // Services
    tft.drawFastHLine(10, y, 300, UI_ACCENT);
    y += 4;
    drawStringCustom(10, y, "Services: " + String(d.serviceCount), UI_MAIN, 1);
    y += lineH;

    if (d.serviceCount > 0) {
        drawStringCustom(20, y, d.serviceSummary, UI_ACCENT, 1);
        y += lineH;
    } else {
        drawStringCustom(20, y, "(none advertised)", UI_ACCENT, 1);
        y += lineH;
    }

    y += 4;
    tft.drawFastHLine(10, y, 300, UI_ACCENT);
    y += 4;

    // Manufacturer data
    if (d.hasManufData) {
        drawStringCustom(10, y, "Mfg data:", UI_MAIN, 1);
        y += lineH;
        drawStringCustom(20, y, d.manufHex, UI_ACCENT, 1);
        y += lineH;
    } else {
        drawStringCustom(10, y, "Mfg data: --", UI_MAIN, 1);
        y += lineH;
    }

    // Footer
    btUiFooter("DEVICE INSPECTOR", "OK/BACK: LIST", BT_UI_ACCENT);
}

// ═══════════════════════════════════════════════════════════════════════════
//  MAIN · BLE SCANNER
// ═══════════════════════════════════════════════════════════════════════════
void runBLEScanner() {

    // Esperar que suelten OK del menú anterior
    while (isEnterPressed() || isBackPressed()) delay(5);
    delay(100);
    flushNavInput();

    // Reset estado
    deviceCount = 0;
    int cursor = 0;
    int scrollOffset = 0;

    // Pantalla inicial
    drawListFrame();
    drawList(cursor, scrollOffset, 0);

    // Beep de arranque
    beep(1500, 40);
    delay(20);
    beep(2100, 60);

    // Inicializar BLE
    BLEDevice::init("");
    BLEScan* scanner = BLEDevice::getScan();
    scanner->setAdvertisedDeviceCallbacks(new BLEScanCallback(), false);
    scanner->setActiveScan(true);    // pide scan response (más info)
    scanner->setInterval(100);
    scanner->setWindow(99);

    bool exitScreen = false;
    bool inDetails = false;
    int detailsIdx = 0;

    unsigned long lastScanStart = 0;
    unsigned long lastRedraw = 0;
    unsigned long lastAnimMs = 0;
    uint8_t scanFrame = 0;
    bool showingSearch = true;
    bool needsRedraw = false;
    unsigned long okPressStart = 0;
    bool okHeld = false;
    // Arrancar primer scan asíncrono
    scanner->start(SCAN_TIME_S, nullptr, false);
    lastScanStart = millis();

    while (!exitScreen) {

        // Re-iniciar scan periódicamente (continuo)
        if (millis() - lastScanStart > (SCAN_TIME_S * 1000UL + 200)) {
            scanner->clearResults();   // libera memoria del ciclo anterior
            scanner->start(SCAN_TIME_S, nullptr, false);
            lastScanStart = millis();
            sortDevices();
            // Asegurar que el cursor siga válido tras re-ordenar
            if (cursor >= deviceCount && deviceCount > 0) cursor = deviceCount - 1;
            showingSearch = (deviceCount == 0);
            needsRedraw = !inDetails;
        }

        // Redraw periódico si estamos en lista (cada 400 ms)
        if (!inDetails && needsRedraw) {
            drawList(cursor, scrollOffset, deviceCount);
            lastRedraw = millis();
            needsRedraw = false;
        }

        if (!inDetails && showingSearch && millis() - lastAnimMs >= 105) {
            drawScannerAnimation(scanFrame++);
            lastAnimMs = millis();
        }

        // ── Controles en modo LISTA ────────────────────────────────────
        if (!inDetails) {

            if (isBackPressed()) {
                exitScreen = true;
            }

            NavAction action = readNavAction(110);

            // UP
            if (action == NAV_UP) {
                if (deviceCount > 0) {
                    int oldCursor = cursor;
                    int oldScroll = scrollOffset;
                    cursor = (cursor - 1 + deviceCount) % deviceCount;
                    if (cursor < scrollOffset) scrollOffset = cursor;
                    if (cursor >= scrollOffset + VISIBLE_ROWS)
                        scrollOffset = cursor - VISIBLE_ROWS + 1;
                    beep(2200, 20);
                    if (scrollOffset != oldScroll) drawList(cursor, scrollOffset, deviceCount);
                    else {
                        tft.startWrite();
                        drawListRow(oldCursor, oldCursor - scrollOffset, false);
                        drawListRow(cursor, cursor - scrollOffset, true);
                        tft.endWrite();
                    }
                    lastRedraw = millis();
                }
            }

            // DOWN
            if (action == NAV_DOWN) {
                if (deviceCount > 0) {
                    int oldCursor = cursor;
                    int oldScroll = scrollOffset;
                    cursor = (cursor + 1) % deviceCount;
                    if (cursor < scrollOffset) scrollOffset = cursor;
                    if (cursor >= scrollOffset + VISIBLE_ROWS)
                        scrollOffset = cursor - VISIBLE_ROWS + 1;
                    beep(2200, 20);
                    if (scrollOffset != oldScroll) drawList(cursor, scrollOffset, deviceCount);
                    else {
                        tft.startWrite();
                        drawListRow(oldCursor, oldCursor - scrollOffset, false);
                        drawListRow(cursor, cursor - scrollOffset, true);
                        tft.endWrite();
                    }
                    lastRedraw = millis();
                }
            }

            // OK: press corto = entrar a detalles; press largo = salir
            if (navEnterPressed()) {
                if (!okHeld) {
                    okPressStart = millis();
                    okHeld = true;
                } else if (millis() - okPressStart > 400) {
                    // HOLD: salir
                    exitScreen = true;
                }
            } else {
                if (okHeld && deviceCount > 0) {
                    // Fue press corto: entrar a detalles
                    detailsIdx = cursor;
                    inDetails = true;
                    beep(1800, 40);
                    drawDetails(devices[detailsIdx]);
                }
                okHeld = false;
            }

        // ── Controles en modo DETAILS ──────────────────────────────────
        } else {
            if (isBackPressed()) {
                while (isBackPressed()) delay(5);
                beep(1400, 40);
                inDetails = false;
                drawListFrame();
                drawList(cursor, scrollOffset, deviceCount);
                lastRedraw = millis();
                flushNavInput();
            }

            // Cualquier OK vuelve a la lista
            if (navEnterPressed()) {
                delay(200);
                while (isEnterPressed() || isBackPressed()) delay(5);
                beep(1400, 40);
                inDetails = false;
                drawListFrame();
                drawList(cursor, scrollOffset, deviceCount);
                lastRedraw = millis();
                flushNavInput();
            }
        }

        delay(10);
    }

    // Cleanup
    scanner->stop();
    scanner->clearResults();
    BLEDevice::deinit(false);
    tft.fillScreen(TFT_BLACK);

    beep(1800, 40);
    delay(20);
    beep(1200, 60);

    // Esperar liberación
    while (isEnterPressed() || isBackPressed()) delay(5);
    delay(100);
    flushNavInput();
}
