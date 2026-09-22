#include "ClockWeather.h"
#include "WifiConfig.h"
#include "DisplayTFT.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "PepeDraw.h"
#include "Pins.h"
#include "SoundUtils.h"
#include "SystemUi.h"

extern DisplayTFT tft;

// ═══════════════════════════════════════════════════════════════════════════
//  CONFIG
// ═══════════════════════════════════════════════════════════════════════════
#define WEATHER_REFRESH_MS  600000UL    // 10 minutos

// NTP servers
static const char* NTP_SERVER_1 = "pool.ntp.org";
static const char* NTP_SERVER_2 = "time.google.com";

// Default fallback (Los Mochis, Sinaloa) si ip-api falla
#define FALLBACK_LAT       25.7894
#define FALLBACK_LON       -108.9956
#define FALLBACK_CITY      "Los Mochis"
#define FALLBACK_TZ_OFFSET (-7 * 3600)   // UTC-7 sin DST (Sinaloa)

// ═══════════════════════════════════════════════════════════════════════════
//  ESTADO
// ═══════════════════════════════════════════════════════════════════════════
static float    g_lat = FALLBACK_LAT;
static float    g_lon = FALLBACK_LON;
static String   g_city = FALLBACK_CITY;
static String   g_country = "MX";
static int      g_tzOffset = FALLBACK_TZ_OFFSET;
static String   g_timezone = "America/Mazatlan";   // IANA TZ

static float    g_tempC = 0;
static float    g_feelsLikeC = 0;
static int      g_humidity = 0;
static float    g_windKmh = 0;
static int      g_weatherCode = 0;
static String   g_sunrise = "";
static String   g_sunset = "";
static bool     g_isDay = true;

static unsigned long g_lastWeatherFetch = 0;
static unsigned long g_lastSecondTick = 0;
static bool     g_loadingFrameReady = false;
static String   g_lastClockMinute = "";
static String   g_lastClockSecond = "";
static String   g_lastClockDate = "";



// ═══════════════════════════════════════════════════════════════════════════
//  IANA TIMEZONE → POSIX TZ STRING
//  Mapea los timezones más comunes (especialmente de Mexico) a sus
//  reglas POSIX correspondientes para configTzTime().
//  Si no encuentra match, retorna un genérico basado en offset.
// ═══════════════════════════════════════════════════════════════════════════
static String ianaToPosix(const String& iana, int offsetSec) {
    // Mexico (con DST sí/no según zona)
    if (iana == "America/Mexico_City")    return "CST6CDT,M4.1.0,M10.5.0";
    if (iana == "America/Cancun")         return "EST5";              // sin DST
    if (iana == "America/Merida")         return "CST6CDT,M4.1.0,M10.5.0";
    if (iana == "America/Monterrey")      return "CST6CDT,M4.1.0,M10.5.0";
    if (iana == "America/Matamoros")      return "CST6CDT,M3.2.0,M11.1.0";
    if (iana == "America/Mazatlan")       return "MST7";              // Sinaloa, sin DST
    if (iana == "America/Chihuahua")      return "MST7MDT,M4.1.0,M10.5.0";
    if (iana == "America/Ojinaga")        return "MST7MDT,M3.2.0,M11.1.0";
    if (iana == "America/Hermosillo")     return "MST7";              // Sonora, sin DST
    if (iana == "America/Tijuana")        return "PST8PDT,M3.2.0,M11.1.0";
    if (iana == "America/Bahia_Banderas") return "CST6CDT,M4.1.0,M10.5.0";

    // USA comunes
    if (iana == "America/Los_Angeles")    return "PST8PDT,M3.2.0,M11.1.0";
    if (iana == "America/Denver")         return "MST7MDT,M3.2.0,M11.1.0";
    if (iana == "America/Phoenix")        return "MST7";              // Arizona, sin DST
    if (iana == "America/Chicago")        return "CST6CDT,M3.2.0,M11.1.0";
    if (iana == "America/New_York")       return "EST5EDT,M3.2.0,M11.1.0";

    // Otros comunes en habla hispana
    if (iana == "America/Bogota")         return "COT5";
    if (iana == "America/Lima")           return "PET5";
    if (iana == "America/Santiago")       return "CLT4CLST,M9.1.6/24,M4.1.6/24";
    if (iana == "America/Buenos_Aires")   return "ART3";
    if (iana == "Europe/Madrid")          return "CET-1CEST,M3.5.0,M10.5.0/3";

    // Fallback: armar string genérico desde el offset (sin DST)
    int hours = -offsetSec / 3600;   // signo invertido en POSIX
    char buf[16];
    if (hours >= 0) snprintf(buf, sizeof(buf), "UTC%d", hours);
    else            snprintf(buf, sizeof(buf), "UTC+%d", -hours);
    return String(buf);
}


// ═══════════════════════════════════════════════════════════════════════════
//  HELPERS DE TIEMPO
// ═══════════════════════════════════════════════════════════════════════════

static const char* DAYS_ES[] = {
    "Domingo", "Lunes", "Martes", "Miercoles",
    "Jueves", "Viernes", "Sabado"
};

static const char* MONTHS_ES[] = {
    "Enero", "Febrero", "Marzo", "Abril", "Mayo", "Junio",
    "Julio", "Agosto", "Septiembre", "Octubre", "Noviembre", "Diciembre"
};

static String formatHHMMSS(struct tm* t) {
    int h = t->tm_hour;
    int displayH;
    if (h == 0)        displayH = 12;       // medianoche
    else if (h > 12)   displayH = h - 12;
    else               displayH = h;

    char buf[12];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             displayH, t->tm_min, t->tm_sec);
    return String(buf);
}

// Helper que retorna "AM" o "PM" según la hora
static String getAmPm(struct tm* t) {
    return (t->tm_hour < 12) ? "AM" : "PM";
}

static String formatHHMM(struct tm* t) {
    int h = t->tm_hour;
    int displayH;
    if (h == 0)        displayH = 12;
    else if (h > 12)   displayH = h - 12;
    else               displayH = h;

    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", displayH, t->tm_min);
    return String(buf);
}

static String formatDate(struct tm* t) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%s, %d de %s",
             DAYS_ES[t->tm_wday], t->tm_mday, MONTHS_ES[t->tm_mon]);
    return String(buf);
}

// ═══════════════════════════════════════════════════════════════════════════
//  ICONOS DEL CLIMA (pixel art 32x32)
//  Códigos WMO de Open-Meteo:
//    0       = clear sky
//    1,2,3   = mainly clear, partly cloudy, overcast
//    45,48   = fog
//    51-67   = drizzle, rain
//    71-77   = snow
//    80-82   = rain showers
//    95-99   = thunderstorm
// ═══════════════════════════════════════════════════════════════════════════

// Categorización del weather code
enum WeatherIcon {
    ICON_SUN,
    ICON_PARTLY_CLOUDY,
    ICON_CLOUDY,
    ICON_RAIN,
    ICON_THUNDER,
    ICON_SNOW,
    ICON_FOG
};

static WeatherIcon weatherCodeToIcon(int code) {
    if (code == 0) return ICON_SUN;
    if (code <= 2) return ICON_PARTLY_CLOUDY;
    if (code == 3) return ICON_CLOUDY;
    if (code == 45 || code == 48) return ICON_FOG;
    if (code >= 51 && code <= 67) return ICON_RAIN;
    if (code >= 71 && code <= 77) return ICON_SNOW;
    if (code >= 80 && code <= 82) return ICON_RAIN;
    if (code >= 95) return ICON_THUNDER;
    return ICON_CLOUDY;
}

static String weatherCodeToDescES(int code) {
    if (code == 0) return "Despejado";
    if (code == 1) return "Mayormente despejado";
    if (code == 2) return "Parcialmente nublado";
    if (code == 3) return "Nublado";
    if (code == 45 || code == 48) return "Niebla";
    if (code == 51 || code == 53 || code == 55) return "Llovizna";
    if (code == 61 || code == 63) return "Lluvia ligera";
    if (code == 65) return "Lluvia fuerte";
    if (code == 71 || code == 73 || code == 75) return "Nieve";
    if (code == 80 || code == 81) return "Chubascos";
    if (code == 82) return "Chubascos fuertes";
    if (code == 95) return "Tormenta";
    if (code >= 96) return "Tormenta granizo";
    return "Desconocido";
}

// Dibuja sol
static void drawSunIcon(int cx, int cy, int size, uint16_t color) {
    int r = size / 4;
    tft.fillCircle(cx, cy, r, color);
    // Rayos
    for (int a = 0; a < 8; a++) {
        float ang = a * 45.0 * PI / 180.0;
        int x1 = cx + (int)(cos(ang) * (r + 3));
        int y1 = cy + (int)(sin(ang) * (r + 3));
        int x2 = cx + (int)(cos(ang) * (r + 8));
        int y2 = cy + (int)(sin(ang) * (r + 8));
        tft.drawLine(x1, y1, x2, y2, color);
    }
}

// Dibuja nube
static void drawCloudIcon(int cx, int cy, uint16_t color) {
    tft.fillCircle(cx - 8, cy + 2, 7, color);
    tft.fillCircle(cx + 6, cy + 2, 8, color);
    tft.fillCircle(cx - 2, cy - 4, 9, color);
    tft.fillRect(cx - 12, cy + 2, 22, 6, color);
}

// Dibuja gotas de lluvia
static void drawRainDrops(int cx, int cy, uint16_t color) {
    for (int i = -1; i <= 1; i++) {
        int x = cx + i * 6;
        int y = cy + 12;
        tft.drawLine(x, y, x - 2, y + 6, color);
        tft.drawPixel(x - 1, y + 7, color);
    }
}

// Dibuja rayo
static void drawLightning(int cx, int cy, uint16_t color) {
    tft.drawLine(cx - 2, cy + 5, cx, cy + 12, color);
    tft.drawLine(cx, cy + 12, cx - 3, cy + 12, color);
    tft.drawLine(cx - 3, cy + 12, cx + 1, cy + 18, color);
    tft.drawLine(cx + 1, cy + 18, cx + 3, cy + 14, color);
}

// Dibuja copos de nieve
static void drawSnowflakes(int cx, int cy, uint16_t color) {
    for (int i = -1; i <= 1; i++) {
        int x = cx + i * 8;
        int y = cy + 14;
        tft.drawPixel(x, y, color);
        tft.drawPixel(x - 1, y, color);
        tft.drawPixel(x + 1, y, color);
        tft.drawPixel(x, y - 1, color);
        tft.drawPixel(x, y + 1, color);
    }
}

static void drawWeatherIcon(int cx, int cy, WeatherIcon icon) {
    switch (icon) {
        case ICON_SUN:
            drawSunIcon(cx, cy, 32, TFT_YELLOW);
            break;
        case ICON_PARTLY_CLOUDY:
            drawSunIcon(cx - 8, cy - 4, 26, TFT_YELLOW);
            drawCloudIcon(cx + 6, cy + 4, UI_MAIN);
            break;
        case ICON_CLOUDY:
            drawCloudIcon(cx, cy, UI_MAIN);
            break;
        case ICON_RAIN:
            drawCloudIcon(cx, cy - 5, UI_ACCENT);
            drawRainDrops(cx, cy, TFT_CYAN);
            break;
        case ICON_THUNDER:
            drawCloudIcon(cx, cy - 5, UI_ACCENT);
            drawLightning(cx, cy, TFT_YELLOW);
            break;
        case ICON_SNOW:
            drawCloudIcon(cx, cy - 5, UI_MAIN);
            drawSnowflakes(cx, cy, TFT_WHITE);
            break;
        case ICON_FOG:
            tft.drawFastHLine(cx - 14, cy - 4, 28, UI_ACCENT);
            tft.drawFastHLine(cx - 16, cy, 32, UI_ACCENT);
            tft.drawFastHLine(cx - 12, cy + 4, 24, UI_ACCENT);
            tft.drawFastHLine(cx - 14, cy + 8, 28, UI_ACCENT);
            break;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  IP GEOLOCATION (ip-api.com)
// ═══════════════════════════════════════════════════════════════════════════

static bool fetchGeolocation() {
    HTTPClient http;
    http.setTimeout(8000);
    if (!http.begin("http://ip-api.com/json/?fields=status,country,city,lat,lon,timezone,offset")) {
        return false;
    }

    int code = http.GET();
    if (code != 200) {
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) return false;

    if (doc["status"] != "success") return false;

    g_lat = doc["lat"].as<float>();
    g_lon = doc["lon"].as<float>();
    g_city = doc["city"].as<String>();
    g_country = doc["country"].as<String>();

    // offset viene en segundos
    if (!doc["offset"].isNull()) {
        g_tzOffset = doc["offset"].as<int>();
    }

    // IANA timezone (ej: "America/Mazatlan")
    if (!doc["timezone"].isNull()) {
        g_timezone = doc["timezone"].as<String>();
    }

    return true;
}
// ═══════════════════════════════════════════════════════════════════════════
//  WEATHER FETCH (Open-Meteo)
// ═══════════════════════════════════════════════════════════════════════════

static bool fetchWeather() {
    HTTPClient http;
    http.setTimeout(10000);

    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast"
             "?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
             "is_day,weather_code,wind_speed_10m"
             "&daily=sunrise,sunset"
             "&timezone=auto&forecast_days=1",
             g_lat, g_lon);

    if (!http.begin(url)) return false;

    int code = http.GET();
    if (code != 200) {
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) return false;

    JsonObject current = doc["current"];
    g_tempC        = current["temperature_2m"].as<float>();
    g_feelsLikeC   = current["apparent_temperature"].as<float>();
    g_humidity     = current["relative_humidity_2m"].as<int>();
    g_windKmh      = current["wind_speed_10m"].as<float>();
    g_weatherCode  = current["weather_code"].as<int>();
    g_isDay        = current["is_day"].as<int>() == 1;

    // Sunrise/sunset format: "2026-04-25T06:23"
    String sr = doc["daily"]["sunrise"][0].as<String>();
    String ss = doc["daily"]["sunset"][0].as<String>();
    if (sr.length() >= 16) g_sunrise = sr.substring(11, 16);
    if (ss.length() >= 16) g_sunset = ss.substring(11, 16);

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  NTP SYNC
// ═══════════════════════════════════════════════════════════════════════════

static bool syncNTP() {
    // Mapear IANA timezone a POSIX TZ string para manejar DST correctamente
    String posixTz = ianaToPosix(g_timezone, g_tzOffset);

    // configTzTime usa POSIX TZ → respeta DST automáticamente según las reglas
    configTzTime(posixTz.c_str(), NTP_SERVER_1, NTP_SERVER_2);

    struct tm timeinfo;
    int retries = 0;
    while (!getLocalTime(&timeinfo, 1500) && retries < 10) {
        retries++;
        delay(500);
    }
    return retries < 10;
}

// ═══════════════════════════════════════════════════════════════════════════
//  PANTALLA DE LOADING
// ═══════════════════════════════════════════════════════════════════════════

static void drawLoadingStep(const String& step, int progress) {
    if (!g_loadingFrameReady) {
        systemUiFrame("CLOCK & WEATHER", "ONLINE SERVICE");
        systemUiCard(16, 64, 288, 116);
        drawStringCustom(28, 78, "PREPARANDO SERVICIOS", SYS_UI_ACCENT, 1);
        systemUiFooter("WIFI + NTP + WEATHER", "PLEASE WAIT");
        g_loadingFrameReady = true;
    }

    tft.fillRect(28, 103, 264, 38, SYS_UI_PANEL);
    drawStringFit(28, 111, step, SYS_UI_TEXT, 264, 2);
    tft.fillRect(27, 150, 266, 15, SYS_UI_PANEL);
    systemUiProgress(28, 151, 264, 12, progress, SYS_UI_ACCENT);
    tft.fillRect(264, 79, 28, 12, SYS_UI_PANEL);
    drawStringRight(291, 80, String(progress) + "%", SYS_UI_AMBER, 1);
}

// ═══════════════════════════════════════════════════════════════════════════
//  PANTALLA PRINCIPAL: RELOJ + CLIMA
// ═══════════════════════════════════════════════════════════════════════════

static void drawMainScreenFrame() {
    systemUiFrame("CLOCK & WEATHER", g_isDay ? "DAY" : "NIGHT");
    systemUiCard(10, 47, 300, 78);
    systemUiCard(10, 130, 300, 76);
    drawStringFit(19, 49, g_city + ", " + g_country,
                  SYS_UI_ACCENT, 180, 1);
    systemUiFooter("LIVE TIME + WEATHER", "BACK/HOLD: EXIT");
    g_lastClockMinute = "";
    g_lastClockSecond = "";
    g_lastClockDate = "";
}

static void drawClock(struct tm* t) {
    String minuteStr = formatHHMM(t);
    char secBuf[4];
    snprintf(secBuf, sizeof(secBuf), "%02d", t->tm_sec);
    String secondStr(secBuf);
    String ampmStr = getAmPm(t);
    String dateStr = formatDate(t);

    // HH:MM cambia una vez por minuto; no se borra con cada segundo.
    if (minuteStr != g_lastClockMinute) {
        tft.fillRect(20, 62, 176, 37, SYS_UI_PANEL);
        drawStringBig(25, 66, minuteStr, SYS_UI_TEXT, 3);
        g_lastClockMinute = minuteStr;
    }

    // Solo este pequeño bloque se actualiza cada segundo.
    if (secondStr != g_lastClockSecond) {
        tft.fillRoundRect(207, 61, 84, 34, 6, SYS_UI_PANEL_2);
        tft.drawRoundRect(207, 61, 84, 34, 6, SYS_UI_ACCENT);
        drawStringBig(216, 67, secondStr, SYS_UI_ACCENT, 2);
        drawStringCustom(261, 76, ampmStr,
                         t->tm_hour < 12 ? SYS_UI_OK : SYS_UI_AMBER, 1);
        g_lastClockSecond = secondStr;
    }

    if (dateStr != g_lastClockDate) {
        tft.fillRect(20, 105, 280, 12, SYS_UI_PANEL);
        String fullDate = dateStr + "  " + String(t->tm_year + 1900);
        int dateW = getTextWidth(fullDate, 1, FONT_SMALL);
        drawStringCustom(max(20, (320 - dateW) / 2), 107, fullDate,
                         SYS_UI_MUTED, 1);
        g_lastClockDate = dateStr;
    }
}

static void drawWeather() {
    tft.fillRoundRect(11, 131, 298, 74, 7, SYS_UI_PANEL);
    tft.drawRoundRect(10, 130, 300, 76, 7, SYS_UI_ACCENT);

    // Icono del clima a la izquierda
    WeatherIcon icon = weatherCodeToIcon(g_weatherCode);
    drawWeatherIcon(42, 165, icon);

    // Temperatura grande al centro-derecha
    char tempBuf[16];
    snprintf(tempBuf, sizeof(tempBuf), "%.0fC", g_tempC);
    drawStringBig(76, 143, String(tempBuf), SYS_UI_ACCENT, 3);

    // Sensación térmica
    char feelsBuf[24];
    snprintf(feelsBuf, sizeof(feelsBuf), "Sensacion: %.0fC", g_feelsLikeC);
    drawStringCustom(78, 176, String(feelsBuf), SYS_UI_TEXT, 1);

    // Descripción del clima
    String desc = weatherCodeToDescES(g_weatherCode);
    drawStringFit(78, 190, desc, SYS_UI_MUTED, 150, 1);

    // Humedad y viento (lado derecho)
    char humBuf[16];
    snprintf(humBuf, sizeof(humBuf), "%d%% hum", g_humidity);
    drawStringCustom(232, 145, String(humBuf), SYS_UI_OK, 1);

    char windBuf[20];
    snprintf(windBuf, sizeof(windBuf), "%.0f km/h", g_windKmh);
    drawStringCustom(232, 159, String(windBuf), SYS_UI_OK, 1);

    // Sunrise / sunset
    if (g_sunrise.length() > 0) {
        drawStringCustom(232, 178, "^ " + g_sunrise, SYS_UI_AMBER, 1);
    }
    if (g_sunset.length() > 0) {
        drawStringCustom(232, 192, "v " + g_sunset, SYS_UI_ACCENT, 1);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  LOOP PRINCIPAL
// ═══════════════════════════════════════════════════════════════════════════

static void mainLoop() {
    drawMainScreenFrame();

    // Initial draws
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        drawClock(&timeinfo);
    }
    drawWeather();

    g_lastWeatherFetch = millis();
    g_lastSecondTick = millis();

    bool stop = false;
    unsigned long okPressStart = 0;
    bool okHeld = false;

    while (!stop) {
        if (navBackPressed() || isBackPressed()) {
            stop = true;
            while (isBackPressed()) delay(5);
            flushNavInput(60);
            continue;
        }

        // Update clock cada segundo
        if (millis() - g_lastSecondTick >= 1000) {
            if (getLocalTime(&timeinfo)) {
                drawClock(&timeinfo);
            }
            g_lastSecondTick = millis();
        }

        // Update weather cada 10 minutos
        if (millis() - g_lastWeatherFetch >= WEATHER_REFRESH_MS) {
            // Mini indicator
            tft.fillRect(305, 8, 10, 10, TFT_YELLOW);
            if (fetchWeather()) {
                drawWeather();
                tft.fillRect(305, 8, 10, 10, TFT_GREEN);
                delay(500);
                tft.fillRect(305, 8, 10, 10, TFT_BLACK);
            } else {
                tft.fillRect(305, 8, 10, 10, TFT_RED);
            }
            g_lastWeatherFetch = millis();
        }

        // OK hold para salir
        if (navEnterPressed()) {
            if (!okHeld) {
                okPressStart = millis();
                okHeld = true;
            } else if (millis() - okPressStart > 500) {
                stop = true;
            }
        } else {
            okHeld = false;
        }

        delay(50);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  ENTRY POINT
// ═══════════════════════════════════════════════════════════════════════════

void runClockWeather() {
    while (navEnterPressed() || navBackPressed()) delay(5);
    delay(100);

    g_loadingFrameReady = false;

    // 1. Conectar WiFi (módulo reusable)
    drawLoadingStep("Conectando WiFi...", 5);
    delay(500);

    if (!wifiConfigConnect()) {
        // Usuario canceló o falló
        return;
    }

    // El selector/teclado WiFi ocupa la pantalla completa. Fuerza la
    // reconstrucción del loading al volver, sin alterar sus credenciales.
    g_loadingFrameReady = false;

    // 2. IP geolocation
    drawLoadingStep("Detectando ubicacion...", 30);
    bool geoOk = fetchGeolocation();
    if (!geoOk) {
        // Usar fallback
        g_lat = FALLBACK_LAT;
        g_lon = FALLBACK_LON;
        g_city = FALLBACK_CITY;
        g_country = "MX";
        g_tzOffset = FALLBACK_TZ_OFFSET;
    }

    // 3. NTP sync
    drawLoadingStep("Sincronizando hora...", 55);
    if (!syncNTP()) {
        systemUiFrame("CLOCK & WEATHER", "NTP ERROR");
        systemUiCard(18, 76, 284, 88, false, SYS_UI_DANGER);
        drawStringBig(70, 91, "NTP FALLO", SYS_UI_DANGER, 2);
        drawStringCustom(42, 132, "No se pudo sincronizar la hora.",
                         SYS_UI_TEXT, 1);
        systemUiFooter("REVISA TU CONEXION", "OK/BACK: EXIT");
        beep(800, 100);
        while (!navEnterPressed() && !navBackPressed()) delay(20);
        while (navEnterPressed() || navBackPressed()) delay(5);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return;
    }

    // 4. Weather
    drawLoadingStep("Obteniendo clima...", 80);
    if (!fetchWeather()) {
        // Default values si falla
        g_tempC = 0;
        g_humidity = 0;
        g_windKmh = 0;
        g_weatherCode = 0;
        g_sunrise = "06:00";
        g_sunset = "19:00";
    }

    drawLoadingStep("Listo!", 100);
    beep(2400, 50); delay(30);
    beep(3000, 50); delay(30);
    beep(3600, 80);
    delay(500);

    // 5. Loop principal
    mainLoop();

    // Cleanup
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);

    beep(1800, 40); delay(20);
    beep(1200, 60);

    while (navEnterPressed() || navBackPressed()) delay(5);
    delay(150);
}
