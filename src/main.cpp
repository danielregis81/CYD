/**
 * Relógio Digital + Previsão do Tempo + Alarmes - ESP32 CYD 2.8"
 * 
 * - Portal captive para config WiFi, API key, cidade e alarmes
 * - Relógio NTP com visual moderno
 * - Clima atual + previsão 3 dias (OpenWeatherMap)
 * - Até 5 alarmes configuráveis pela interface web
 * - Ao disparar: tela pisca até tocar no display
 * - Toque curto: alterna tema | Toque longo (3s): reset config
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// ========== AP CONFIG ==========
const char* AP_SSID = "Relogio-CYD";
const char* AP_PASS = "12345678";

// ========== NTP ==========
const char* NTP_SERVER = "pool.ntp.org";

// ========== PINOS TOUCH ==========
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

// ========== DISPLAY ==========
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

// ========== ALARMES ==========
#define MAX_ALARMS 5

struct Alarm {
    uint8_t hour;
    uint8_t minute;
    bool enabled;
    bool triggered;  // já disparou hoje?
    char label[20];
};

Alarm alarms[MAX_ALARMS];
bool alarmRinging = false;
unsigned long alarmFlashTimer = 0;
bool alarmFlashState = false;

// ========== OBJETOS ==========
TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

// ========== ESTADOS ==========
enum AppState { STATE_AP_MODE, STATE_CONNECTING, STATE_CLOCK };
AppState currentState = STATE_AP_MODE;

// ========== CONFIG ==========
String savedSSID = "";
String savedPassword = "";
String apiKey = "";
String cityName = "Florianopolis";
int gmtOffset = -3;

// ========== TEMA ==========
bool darkTheme = true;
uint16_t bgColor, textColor, accentColor, dimColor, cardColor, cardBorder;

// ========== WEATHER ==========
struct WeatherData {
    float temp;
    float tempMin;
    float tempMax;
    int humidity;
    String description;
    String icon;
    bool valid;
};

struct ForecastDay {
    float tempMin;
    float tempMax;
    String icon;
    String dayName;
    bool valid;
};

WeatherData currentWeather = {0, 0, 0, 0, "", "", false};
ForecastDay forecast[3] = {{0, 0, "", "", false}, {0, 0, "", "", false}, {0, 0, "", "", false}};

unsigned long lastWeatherUpdate = 0;
const unsigned long WEATHER_INTERVAL = 600000;

// ========== CLOCK STATE ==========
String lastTime = "";
String lastSec = "";
String lastDate = "";
bool colonVisible = true;

unsigned long lastTouchTime = 0;
unsigned long touchStartTime = 0;
bool touchActive = false;

// ========== STRINGS PT ==========
const char* diasSemana[] = {"Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sab"};
const char* diasCompletos[] = {"Domingo", "Segunda", "Terca", "Quarta", "Quinta", "Sexta", "Sabado"};
const char* meses[] = {"Jan", "Fev", "Mar", "Abr", "Mai", "Jun", "Jul", "Ago", "Set", "Out", "Nov", "Dez"};

// ========== CORES ==========
#define COLOR_DARK_BG      0x0000
#define COLOR_DARK_CARD    0x18E3
#define COLOR_DARK_BORDER  0x2945
#define COLOR_CYAN         0x07FF
#define COLOR_WARM_WHITE   0xFFDE
#define COLOR_ORANGE       0xFD20
#define COLOR_LIGHT_BLUE   0x867F
#define COLOR_LIGHT_BG     0xEF7D
#define COLOR_LIGHT_CARD   0xFFFF
#define COLOR_LIGHT_BORDER 0xD69A
#define COLOR_RED          0xF800
#define COLOR_ALARM_FLASH  0xFFE0  // Amarelo

// =====================================================================
// HTML DO PORTAL - COM ALARMES
// =====================================================================
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Relogio CYD</title>
    <style>
        *{box-sizing:border-box;margin:0;padding:0}
        body{font-family:-apple-system,sans-serif;background:#0f0f23;color:#eee;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px}
        .c{background:#1a1a3e;border-radius:16px;padding:24px;width:100%;max-width:460px;box-shadow:0 10px 40px rgba(0,0,0,0.4)}
        h1{text-align:center;margin-bottom:4px;font-size:1.3em;color:#0ff}
        .sub{text-align:center;color:#888;margin-bottom:18px;font-size:0.82em}
        h2{color:#0ff;font-size:1em;margin:18px 0 10px;padding-top:14px;border-top:1px solid #333}
        label{display:block;margin-bottom:3px;font-weight:500;color:#aaa;font-size:0.85em}
        input,select{width:100%;padding:10px;border:1px solid #333;border-radius:8px;background:#0a1628;color:#fff;font-size:14px;margin-bottom:12px;outline:none}
        input:focus,select:focus{border-color:#0ff}
        .row{display:flex;gap:10px}
        .row>div{flex:1}
        button{width:100%;padding:13px;background:linear-gradient(135deg,#0ff,#00bcd4);border:none;border-radius:8px;color:#0a1628;font-size:15px;font-weight:bold;cursor:pointer;margin-top:8px}
        button:active{transform:scale(0.98)}
        .alarm-row{display:flex;gap:8px;align-items:center;margin-bottom:8px;background:#0a1628;padding:8px 10px;border-radius:8px}
        .alarm-row input[type="time"]{width:100px;margin:0;padding:8px;flex-shrink:0}
        .alarm-row input[type="text"]{flex:1;margin:0;padding:8px}
        .alarm-row input[type="checkbox"]{width:20px;height:20px;margin:0;flex-shrink:0;accent-color:#0ff}
        .alarm-label{color:#666;font-size:0.75em;display:flex;gap:8px;margin-bottom:4px}
        .alarm-label span{flex:1}
        .info{background:#0a1628;border-radius:8px;padding:10px;margin-top:14px;font-size:0.78em;color:#666;line-height:1.4}
        .info a{color:#0ff}
    </style>
</head>
<body>
<div class="c">
    <h1>&#9201; Relogio CYD</h1>
    <p class="sub">WiFi + Clima + Alarmes</p>
    <form action="/save" method="POST">
        <label>Nome da Rede (SSID)</label>
        <input type="text" name="ssid" placeholder="Sua rede WiFi" required>
        <label>Senha do WiFi</label>
        <input type="password" name="pass" placeholder="Senha">
        <div class="row">
            <div>
                <label>Cidade</label>
                <input type="text" name="city" placeholder="Florianopolis" value="Florianopolis">
            </div>
            <div>
                <label>Fuso (GMT)</label>
                <select name="gmt">
                    <option value="-5">-5 Acre</option>
                    <option value="-4">-4 Manaus</option>
                    <option value="-3" selected>-3 Brasilia</option>
                    <option value="-2">-2 Noronha</option>
                    <option value="0">+0 Londres</option>
                    <option value="1">+1 Europa</option>
                </select>
            </div>
        </div>
        <label>API Key OpenWeatherMap</label>
        <input type="text" name="apikey" placeholder="Cole sua API key aqui">

        <h2>&#9200; Alarmes</h2>
        <div class="alarm-label"><span>Ativo</span><span>Hora</span><span>Descricao (opcional)</span></div>
        <div class="alarm-row">
            <input type="checkbox" name="al0_on">
            <input type="time" name="al0_time" value="07:00">
            <input type="text" name="al0_lbl" placeholder="Acordar">
        </div>
        <div class="alarm-row">
            <input type="checkbox" name="al1_on">
            <input type="time" name="al1_time" value="08:00">
            <input type="text" name="al1_lbl" placeholder="Reuniao">
        </div>
        <div class="alarm-row">
            <input type="checkbox" name="al2_on">
            <input type="time" name="al2_time" value="12:00">
            <input type="text" name="al2_lbl" placeholder="Almoco">
        </div>
        <div class="alarm-row">
            <input type="checkbox" name="al3_on">
            <input type="time" name="al3_time" value="18:00">
            <input type="text" name="al3_lbl" placeholder="Exercicio">
        </div>
        <div class="alarm-row">
            <input type="checkbox" name="al4_on">
            <input type="time" name="al4_time" value="22:00">
            <input type="text" name="al4_lbl" placeholder="Dormir">
        </div>

        <button type="submit">Salvar e Conectar</button>
    </form>
    <div class="info">
        Clima: crie conta gratis em <a href="https://openweathermap.org/api" target="_blank">openweathermap.org</a> e cole a API key.<br>
        Alarmes: a tela pisca ao disparar. Toque para desligar.
    </div>
</div>
</body>
</html>
)rawliteral";

const char HTML_SUCCESS[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OK</title><style>body{font-family:sans-serif;background:#0f0f23;color:#eee;display:flex;align-items:center;justify-content:center;min-height:100vh;text-align:center}.ok{color:#0f0;font-size:3em}</style>
</head><body><div><p class="ok">&#10004;</p><p>Salvo! Reiniciando...</p></div></body></html>
)rawliteral";

// =====================================================================
// CONFIG NVS
// =====================================================================

void loadAlarms() {
    preferences.begin("alarms", true);
    for (int i = 0; i < MAX_ALARMS; i++) {
        char keyH[8], keyM[8], keyE[8], keyL[12];
        snprintf(keyH, sizeof(keyH), "h%d", i);
        snprintf(keyM, sizeof(keyM), "m%d", i);
        snprintf(keyE, sizeof(keyE), "e%d", i);
        snprintf(keyL, sizeof(keyL), "l%d", i);
        
        alarms[i].hour = preferences.getUChar(keyH, 0);
        alarms[i].minute = preferences.getUChar(keyM, 0);
        alarms[i].enabled = preferences.getBool(keyE, false);
        String lbl = preferences.getString(keyL, "");
        strncpy(alarms[i].label, lbl.c_str(), sizeof(alarms[i].label) - 1);
        alarms[i].label[sizeof(alarms[i].label) - 1] = '\0';
        alarms[i].triggered = false;
    }
    preferences.end();
}

void saveAlarms() {
    preferences.begin("alarms", false);
    for (int i = 0; i < MAX_ALARMS; i++) {
        char keyH[8], keyM[8], keyE[8], keyL[12];
        snprintf(keyH, sizeof(keyH), "h%d", i);
        snprintf(keyM, sizeof(keyM), "m%d", i);
        snprintf(keyE, sizeof(keyE), "e%d", i);
        snprintf(keyL, sizeof(keyL), "l%d", i);
        
        preferences.putUChar(keyH, alarms[i].hour);
        preferences.putUChar(keyM, alarms[i].minute);
        preferences.putBool(keyE, alarms[i].enabled);
        preferences.putString(keyL, alarms[i].label);
    }
    preferences.end();
}

void loadConfig() {
    preferences.begin("relogio", true);
    savedSSID = preferences.getString("ssid", "");
    savedPassword = preferences.getString("pass", "");
    apiKey = preferences.getString("apikey", "");
    cityName = preferences.getString("city", "Florianopolis");
    gmtOffset = preferences.getInt("gmt", -3);
    darkTheme = preferences.getBool("theme", true);
    preferences.end();
    
    loadAlarms();
}

void saveConfig(String ssid, String pass, String key, String city, int gmt) {
    preferences.begin("relogio", false);
    preferences.putString("ssid", ssid);
    preferences.putString("pass", pass);
    preferences.putString("apikey", key);
    preferences.putString("city", city);
    preferences.putInt("gmt", gmt);
    preferences.end();
}

void saveTheme() {
    preferences.begin("relogio", false);
    preferences.putBool("theme", darkTheme);
    preferences.end();
}

void clearConfig() {
    preferences.begin("relogio", false);
    preferences.clear();
    preferences.end();
    preferences.begin("alarms", false);
    preferences.clear();
    preferences.end();
}

// =====================================================================
// TEMA
// =====================================================================

void applyTheme() {
    if (darkTheme) {
        bgColor    = COLOR_DARK_BG;
        cardColor  = COLOR_DARK_CARD;
        cardBorder = COLOR_DARK_BORDER;
        textColor  = COLOR_WARM_WHITE;
        accentColor = COLOR_CYAN;
        dimColor   = 0x4208;
    } else {
        bgColor    = COLOR_LIGHT_BG;
        cardColor  = COLOR_LIGHT_CARD;
        cardBorder = COLOR_LIGHT_BORDER;
        textColor  = 0x2104;
        accentColor = 0x01CF;
        dimColor   = 0x9CF3;
    }
}

// =====================================================================
// UI DRAWING
// =====================================================================

void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
    tft.fillRoundRect(x, y, w, h, r, color);
    tft.drawRoundRect(x, y, w, h, r, cardBorder);
}

void drawWeatherIcon(int cx, int cy, String icon, int size) {
    uint16_t sunColor = COLOR_ORANGE;
    uint16_t cloudColor = darkTheme ? 0xC618 : 0x8410;
    uint16_t rainColor = COLOR_LIGHT_BLUE;
    
    if (icon.startsWith("01")) {
        tft.fillCircle(cx, cy, size, sunColor);
        for (int i = 0; i < 8; i++) {
            float angle = i * PI / 4;
            int x1 = cx + cos(angle) * (size + 3);
            int y1 = cy + sin(angle) * (size + 3);
            int x2 = cx + cos(angle) * (size + 6);
            int y2 = cy + sin(angle) * (size + 6);
            tft.drawLine(x1, y1, x2, y2, sunColor);
        }
    } else if (icon.startsWith("02")) {
        tft.fillCircle(cx - size/2, cy - size/3, size*2/3, sunColor);
        tft.fillRoundRect(cx - size, cy - size/3, size*2, size, size/2, cloudColor);
    } else if (icon.startsWith("03") || icon.startsWith("04")) {
        tft.fillCircle(cx - size/3, cy - size/4, size*2/3, cloudColor);
        tft.fillCircle(cx + size/3, cy - size/4, size/2, cloudColor);
        tft.fillRoundRect(cx - size, cy - size/6, size*2, size*2/3, size/3, cloudColor);
    } else if (icon.startsWith("09") || icon.startsWith("10")) {
        tft.fillRoundRect(cx - size, cy - size/2, size*2, size*2/3, size/3, cloudColor);
        for (int i = 0; i < 3; i++) {
            int rx = cx - size/2 + i * (size/2);
            tft.drawLine(rx, cy + size/3, rx - 2, cy + size, rainColor);
        }
    } else if (icon.startsWith("11")) {
        tft.fillRoundRect(cx - size, cy - size/2, size*2, size*2/3, size/3, cloudColor);
        tft.fillTriangle(cx-2, cy+size/4, cx+4, cy+size/4, cx+1, cy+size, TFT_YELLOW);
    } else if (icon.startsWith("13")) {
        tft.fillRoundRect(cx - size, cy - size/2, size*2, size*2/3, size/3, cloudColor);
        for (int i = 0; i < 3; i++) {
            int sx = cx - size/2 + i * (size/2);
            tft.fillCircle(sx, cy + size/2, 2, TFT_WHITE);
        }
    } else {
        for (int i = 0; i < 3; i++) {
            int ly = cy - size/3 + i * (size/3);
            tft.drawFastHLine(cx - size, ly, size*2, dimColor);
        }
    }
}

// =====================================================================
// CLOCK SCREEN
// =====================================================================

void drawClockScreen() {
    tft.fillScreen(bgColor);
    drawRoundRect(4, 4, SCREEN_WIDTH - 8, 105, 8, cardColor);
    drawRoundRect(4, 114, SCREEN_WIDTH - 8, SCREEN_HEIGHT - 118, 8, cardColor);
    lastTime = "";
    lastDate = "";
    lastSec = "";
}

void displayClock() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;
    
    char hourStr[3], minStr[3], secStr[3];
    snprintf(hourStr, sizeof(hourStr), "%02d", timeinfo.tm_hour);
    snprintf(minStr, sizeof(minStr), "%02d", timeinfo.tm_min);
    snprintf(secStr, sizeof(secStr), "%02d", timeinfo.tm_sec);
    
    char dateStr[50];
    snprintf(dateStr, sizeof(dateStr), "%s, %02d de %s %04d",
             diasCompletos[timeinfo.tm_wday], timeinfo.tm_mday,
             meses[timeinfo.tm_mon], timeinfo.tm_year + 1900);
    
    String currentTime = String(hourStr) + String(minStr);
    if (currentTime != lastTime) {
        tft.fillRect(10, 14, SCREEN_WIDTH - 20, 62, cardColor);
        tft.setTextColor(textColor, cardColor);
        tft.setTextDatum(MR_DATUM);
        tft.drawString(hourStr, SCREEN_WIDTH/2 - 12, 45, 7);
        tft.setTextDatum(ML_DATUM);
        tft.drawString(minStr, SCREEN_WIDTH/2 + 12, 45, 7);
        lastTime = currentTime;
    }
    
    // Separador pulsante
    colonVisible = (timeinfo.tm_sec % 2 == 0);
    uint16_t colonColor = colonVisible ? accentColor : cardColor;
    tft.fillCircle(SCREEN_WIDTH/2, 35, 3, colonColor);
    tft.fillCircle(SCREEN_WIDTH/2, 52, 3, colonColor);
    
    // Segundos
    String currentSec = String(secStr);
    if (currentSec != lastSec) {
        tft.fillRect(SCREEN_WIDTH - 45, 15, 35, 20, cardColor);
        tft.setTextColor(accentColor, cardColor);
        tft.setTextDatum(TR_DATUM);
        tft.drawString(secStr, SCREEN_WIDTH - 12, 18, 4);
        lastSec = currentSec;
    }
    
    // Data
    String currentDate = String(dateStr);
    if (currentDate != lastDate) {
        tft.fillRect(10, 78, SCREEN_WIDTH - 20, 22, cardColor);
        tft.setTextColor(dimColor, cardColor);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(dateStr, SCREEN_WIDTH/2, 89, 2);
        lastDate = currentDate;
    }
    
    // Indicador de alarme ativo (ícone pequeno)
    bool hasActiveAlarm = false;
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (alarms[i].enabled) { hasActiveAlarm = true; break; }
    }
    if (hasActiveAlarm) {
        tft.setTextColor(COLOR_ORANGE, cardColor);
        tft.setTextDatum(TL_DATUM);
        tft.drawString("AL", 12, 18, 1);
    }
}

// =====================================================================
// WEATHER DISPLAY
// =====================================================================

void displayWeather() {
    int cardY = 114;
    int cardH = SCREEN_HEIGHT - 118;
    
    tft.fillRect(6, cardY + 2, SCREEN_WIDTH - 12, cardH - 4, cardColor);
    
    if (!currentWeather.valid) {
        tft.setTextColor(dimColor, cardColor);
        tft.setTextDatum(MC_DATUM);
        if (apiKey.length() == 0) {
            tft.drawString("Configure API key no portal", SCREEN_WIDTH/2, cardY + cardH/2 - 8, 2);
            tft.drawString("(segure 3s para reconfigurar)", SCREEN_WIDTH/2, cardY + cardH/2 + 10, 1);
        } else {
            tft.drawString("Carregando clima...", SCREEN_WIDTH/2, cardY + cardH/2, 2);
        }
        return;
    }
    
    int leftX = 12;
    int topY = cardY + 8;
    
    drawWeatherIcon(leftX + 22, topY + 22, currentWeather.icon, 12);
    
    char tempStr[8];
    snprintf(tempStr, sizeof(tempStr), "%.0f", currentWeather.temp);
    tft.setTextColor(textColor, cardColor);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(tempStr, leftX + 45, topY + 4, 6);
    
    int tempWidth = tft.textWidth(tempStr, 6);
    tft.setTextColor(accentColor, cardColor);
    tft.drawString("o", leftX + 47 + tempWidth, topY + 2, 2);
    tft.setTextColor(textColor, cardColor);
    tft.drawString("C", leftX + 55 + tempWidth, topY + 8, 4);
    
    tft.setTextColor(dimColor, cardColor);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(currentWeather.description, leftX + 2, topY + 48, 2);
    
    char infoStr[30];
    snprintf(infoStr, sizeof(infoStr), "%.0f~%.0f C  %d%%", 
             currentWeather.tempMin, currentWeather.tempMax, currentWeather.humidity);
    tft.setTextColor(dimColor, cardColor);
    tft.drawString(infoStr, leftX + 2, topY + 66, 1);
    
    tft.setTextColor(accentColor, cardColor);
    tft.drawString(cityName, leftX + 2, topY + 80, 1);
    
    // Previsão 3 dias
    int rightX = SCREEN_WIDTH/2 + 20;
    int foreY = topY + 4;
    
    tft.drawFastVLine(SCREEN_WIDTH/2 + 10, cardY + 8, cardH - 16, cardBorder);
    
    tft.setTextColor(accentColor, cardColor);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Proximos dias", rightX, foreY, 1);
    foreY += 14;
    
    for (int i = 0; i < 3; i++) {
        if (!forecast[i].valid) continue;
        int rowY = foreY + i * 26;
        
        tft.setTextColor(textColor, cardColor);
        tft.setTextDatum(TL_DATUM);
        tft.drawString(forecast[i].dayName, rightX, rowY + 4, 2);
        
        drawWeatherIcon(rightX + 42, rowY + 8, forecast[i].icon, 6);
        
        char foreStr[16];
        snprintf(foreStr, sizeof(foreStr), "%.0f/%.0f", forecast[i].tempMin, forecast[i].tempMax);
        tft.setTextColor(dimColor, cardColor);
        tft.setTextDatum(TL_DATUM);
        tft.drawString(foreStr, rightX + 58, rowY + 4, 2);
    }
}

// =====================================================================
// FETCH WEATHER
// =====================================================================

void fetchWeather() {
    if (apiKey.length() == 0 || WiFi.status() != WL_CONNECTED) return;
    
    HTTPClient http;
    
    String url = "http://api.openweathermap.org/data/2.5/weather?q=" + cityName 
                 + "&appid=" + apiKey + "&units=metric&lang=pt_br";
    
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == 200) {
        String payload = http.getString();
        JsonDocument doc;
        if (!deserializeJson(doc, payload)) {
            currentWeather.temp = doc["main"]["temp"];
            currentWeather.tempMin = doc["main"]["temp_min"];
            currentWeather.tempMax = doc["main"]["temp_max"];
            currentWeather.humidity = doc["main"]["humidity"];
            currentWeather.description = doc["weather"][0]["description"].as<String>();
            currentWeather.icon = doc["weather"][0]["icon"].as<String>();
            currentWeather.valid = true;
            if (currentWeather.description.length() > 0)
                currentWeather.description[0] = toupper(currentWeather.description[0]);
        }
    }
    http.end();
    
    // Forecast
    url = "http://api.openweathermap.org/data/2.5/forecast?q=" + cityName 
          + "&appid=" + apiKey + "&units=metric&lang=pt_br";
    
    http.begin(url);
    httpCode = http.GET();
    
    if (httpCode == 200) {
        String payload = http.getString();
        JsonDocument doc;
        if (!deserializeJson(doc, payload)) {
            JsonArray list = doc["list"];
            struct tm timeinfo;
            getLocalTime(&timeinfo);
            int today = timeinfo.tm_mday;
            int forecastIdx = 0;
            int lastDay = -1;
            
            for (JsonObject item : list) {
                if (forecastIdx >= 3) break;
                long dt = item["dt"];
                struct tm foreTime;
                time_t rawTime = dt + (gmtOffset * 3600);
                gmtime_r(&rawTime, &foreTime);
                int fDay = foreTime.tm_mday;
                if (fDay == today || fDay == lastDay) continue;
                int fHour = foreTime.tm_hour;
                if (fHour >= 11 && fHour <= 14) {
                    forecast[forecastIdx].tempMin = item["main"]["temp_min"];
                    forecast[forecastIdx].tempMax = item["main"]["temp_max"];
                    forecast[forecastIdx].icon = item["weather"][0]["icon"].as<String>();
                    forecast[forecastIdx].dayName = String(diasSemana[foreTime.tm_wday]);
                    forecast[forecastIdx].valid = true;
                    lastDay = fDay;
                    forecastIdx++;
                }
            }
        }
    }
    http.end();
    
    displayWeather();
}

// =====================================================================
// ALARMES
// =====================================================================

void checkAlarms() {
    if (alarmRinging) return;  // Já tocando, não checa novos
    
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;
    
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (!alarms[i].enabled) continue;
        if (alarms[i].triggered) continue;
        
        if (timeinfo.tm_hour == alarms[i].hour && timeinfo.tm_min == alarms[i].minute) {
            // DISPARA!
            alarmRinging = true;
            alarmFlashTimer = millis();
            alarmFlashState = false;
            Serial.printf("ALARME! %02d:%02d - %s\n", alarms[i].hour, alarms[i].minute, alarms[i].label);
            alarms[i].triggered = true;
            return;
        }
    }
    
    // Reset triggered flag quando minuto muda (para disparar novamente amanhã)
    static int lastMinute = -1;
    if (timeinfo.tm_min != lastMinute) {
        lastMinute = timeinfo.tm_min;
        for (int i = 0; i < MAX_ALARMS; i++) {
            if (alarms[i].triggered && 
                !(timeinfo.tm_hour == alarms[i].hour && timeinfo.tm_min == alarms[i].minute)) {
                alarms[i].triggered = false;
            }
        }
    }
}

void handleAlarmDisplay() {
    if (!alarmRinging) return;
    
    // Pisca a tela a cada 300ms
    if (millis() - alarmFlashTimer > 300) {
        alarmFlashTimer = millis();
        alarmFlashState = !alarmFlashState;
        
        if (alarmFlashState) {
            tft.fillScreen(COLOR_ALARM_FLASH);
            tft.setTextColor(COLOR_RED, COLOR_ALARM_FLASH);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("ALARME!", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 - 30, 4);
            
            // Mostra label do alarme que disparou
            for (int i = 0; i < MAX_ALARMS; i++) {
                if (alarms[i].triggered && alarms[i].enabled) {
                    char alStr[30];
                    snprintf(alStr, sizeof(alStr), "%02d:%02d", alarms[i].hour, alarms[i].minute);
                    tft.setTextColor(TFT_BLACK, COLOR_ALARM_FLASH);
                    tft.drawString(alStr, SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 10, 6);
                    if (strlen(alarms[i].label) > 0) {
                        tft.drawString(alarms[i].label, SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 55, 2);
                    }
                    break;
                }
            }
            
            tft.setTextColor(0x4208, COLOR_ALARM_FLASH);
            tft.drawString("Toque para desligar", SCREEN_WIDTH/2, SCREEN_HEIGHT - 30, 2);
        } else {
            tft.fillScreen(COLOR_RED);
            tft.setTextColor(TFT_WHITE, COLOR_RED);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("ALARME!", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 - 10, 4);
        }
    }
}

void stopAlarm() {
    alarmRinging = false;
    // Redesenha tela normal
    applyTheme();
    drawClockScreen();
    displayWeather();
    Serial.println("Alarme desligado pelo toque");
}

// =====================================================================
// AP MODE & WEB SERVER
// =====================================================================

void drawAPScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.fillRoundRect(10, 10, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 20, 12, COLOR_DARK_CARD);
    
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COLOR_CYAN, COLOR_DARK_CARD);
    tft.drawString("CONFIGURACAO", SCREEN_WIDTH/2, 45, 4);
    
    tft.setTextColor(TFT_WHITE, COLOR_DARK_CARD);
    tft.drawString("Conecte no WiFi:", SCREEN_WIDTH/2, 85, 2);
    
    tft.setTextColor(COLOR_ORANGE, COLOR_DARK_CARD);
    tft.drawString(AP_SSID, SCREEN_WIDTH/2, 115, 4);
    
    tft.setTextColor(TFT_WHITE, COLOR_DARK_CARD);
    tft.drawString("Senha: " + String(AP_PASS), SCREEN_WIDTH/2, 148, 2);
    
    tft.setTextColor(COLOR_CYAN, COLOR_DARK_CARD);
    tft.drawString("http://192.168.4.1", SCREEN_WIDTH/2, 180, 2);
    
    tft.setTextColor(0x4208, COLOR_DARK_CARD);
    tft.drawString("abra no navegador", SCREEN_WIDTH/2, 205, 1);
}

void handleRoot() { server.send(200, "text/html", HTML_PAGE); }

void handleSave() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    String key = server.arg("apikey");
    String city = server.arg("city");
    int gmt = server.arg("gmt").toInt();
    
    if (ssid.length() == 0) {
        server.send(400, "text/plain", "SSID obrigatorio!");
        return;
    }
    
    if (city.length() == 0) city = "Florianopolis";
    saveConfig(ssid, pass, key, city, gmt);
    
    // Salva alarmes
    for (int i = 0; i < MAX_ALARMS; i++) {
        char keyOn[10], keyTime[12], keyLbl[12];
        snprintf(keyOn, sizeof(keyOn), "al%d_on", i);
        snprintf(keyTime, sizeof(keyTime), "al%d_time", i);
        snprintf(keyLbl, sizeof(keyLbl), "al%d_lbl", i);
        
        alarms[i].enabled = server.hasArg(keyOn);
        
        String timeVal = server.arg(keyTime);
        if (timeVal.length() >= 5) {
            alarms[i].hour = timeVal.substring(0, 2).toInt();
            alarms[i].minute = timeVal.substring(3, 5).toInt();
        }
        
        String lbl = server.arg(keyLbl);
        strncpy(alarms[i].label, lbl.c_str(), sizeof(alarms[i].label) - 1);
        alarms[i].label[sizeof(alarms[i].label) - 1] = '\0';
        alarms[i].triggered = false;
    }
    saveAlarms();
    
    server.send(200, "text/html", HTML_SUCCESS);
    delay(2000);
    ESP.restart();
}

void handleNotFound() {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
}

void startAPMode() {
    currentState = STATE_AP_MODE;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    dnsServer.start(53, "*", WiFi.softAPIP());
    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleNotFound);
    server.begin();
    drawAPScreen();
    Serial.println("AP Mode: " + WiFi.softAPIP().toString());
}

// =====================================================================
// WIFI
// =====================================================================

bool connectWiFi() {
    currentState = STATE_CONNECTING;
    tft.fillScreen(bgColor);
    drawRoundRect(30, 80, SCREEN_WIDTH - 60, 80, 12, cardColor);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(textColor, cardColor);
    tft.drawString("Conectando...", SCREEN_WIDTH/2, 110, 2);
    tft.setTextColor(accentColor, cardColor);
    tft.drawString(savedSSID, SCREEN_WIDTH/2, 135, 2);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
        int barW = (attempts * (SCREEN_WIDTH - 100)) / 20;
        tft.fillRoundRect(50, 148, barW, 6, 3, accentColor);
    }
    return WiFi.status() == WL_CONNECTED;
}

void syncTime() {
    configTime(gmtOffset * 3600, 0, NTP_SERVER, "time.nist.gov");
    struct tm t;
    int r = 0;
    while (!getLocalTime(&t) && r < 10) { delay(500); r++; }
}

// =====================================================================
// TOUCH
// =====================================================================

void handleTouch() {
    if (touchscreen.tirqTouched() && touchscreen.touched()) {
        // Se alarme tocando, qualquer toque desliga
        if (alarmRinging) {
            stopAlarm();
            delay(300);  // debounce
            return;
        }
        
        if (!touchActive) {
            touchActive = true;
            touchStartTime = millis();
        }
        
        // Toque longo: reset
        if (millis() - touchStartTime > 3000) {
            clearConfig();
            delay(300);
            ESP.restart();
        }
    } else {
        if (touchActive) {
            unsigned long dur = millis() - touchStartTime;
            touchActive = false;
            
            if (dur < 800 && millis() - lastTouchTime > 300) {
                lastTouchTime = millis();
                darkTheme = !darkTheme;
                applyTheme();
                saveTheme();
                drawClockScreen();
                displayWeather();
            }
        }
    }
}

// =====================================================================
// SETUP
// =====================================================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Relogio CYD ===");
    
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    
    touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touchscreen.begin(touchSPI);
    touchscreen.setRotation(1);
    
    loadConfig();
    applyTheme();
    
    if (savedSSID.length() > 0) {
        if (connectWiFi()) {
            syncTime();
            currentState = STATE_CLOCK;
            drawClockScreen();
            fetchWeather();
        } else {
            startAPMode();
        }
    } else {
        startAPMode();
    }
}

// =====================================================================
// LOOP
// =====================================================================

void loop() {
    switch (currentState) {
        case STATE_AP_MODE:
            dnsServer.processNextRequest();
            server.handleClient();
            break;
            
        case STATE_CLOCK: {
            // Se alarme está tocando, prioriza o flash
            if (alarmRinging) {
                handleAlarmDisplay();
                handleTouch();
                break;
            }
            
            static unsigned long lastClockUpdate = 0;
            if (millis() - lastClockUpdate >= 500) {
                lastClockUpdate = millis();
                displayClock();
            }
            
            // Checa alarmes a cada segundo
            static unsigned long lastAlarmCheck = 0;
            if (millis() - lastAlarmCheck >= 1000) {
                lastAlarmCheck = millis();
                checkAlarms();
            }
            
            // Atualiza clima a cada 10 min
            if (millis() - lastWeatherUpdate >= WEATHER_INTERVAL) {
                lastWeatherUpdate = millis();
                fetchWeather();
            }
            
            handleTouch();
            
            // Reconecta WiFi
            static unsigned long lastReconnect = 0;
            if (WiFi.status() != WL_CONNECTED && millis() - lastReconnect > 30000) {
                lastReconnect = millis();
                WiFi.reconnect();
            }
            break;
        }
        default: break;
    }
    delay(10);
}
