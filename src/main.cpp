/**
 * Relógio Digital + Previsão do Tempo + Alarmes - ESP32 CYD 2.8"
 * 
 * - Portal captive para config WiFi, API key, cidade e alarmes
 * - Relógio NTP com visual moderno
 * - Clima atual + previsão 3 dias (OpenWeatherMap)
 * - Até 5 alarmes configuráveis pela interface web
 * - Tela de alarmes (toque no ícone AL ou swipe)
 * - Botão para alternar tema claro/escuro
 * - IP visível no rodapé para acessar WebUI
 * - Toque longo (3s): reset config
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
    bool triggered;
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
enum AppState { STATE_AP_MODE, STATE_CONNECTING, STATE_CLOCK, STATE_ALARMS };
AppState currentState = STATE_AP_MODE;

// ========== CONFIG ==========
String savedSSID = "";
String savedPassword = "";
String apiKey = "";
String cityName = "Florianopolis";
String latitude = "-27.5954";
String longitude = "-48.5480";
int gmtOffset = -3;

// ========== TEMA ==========
bool darkTheme = true;
uint16_t bgColor, textColor, accentColor, dimColor, cardColor, cardBorder;

// ========== BRILHO ==========
// Níveis: 25%, 50%, 75%, 100%
const uint8_t brightnessLevels[] = {64, 128, 192, 255};
const char* brightnessLabels[] = {"25", "50", "75", "100"};
uint8_t brightnessIdx = 3;  // Padrão: 100%

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

// ========== TOUCH ZONES (botões na tela) ==========
// Botão tema: dentro do card, canto superior esquerdo
#define BTN_THEME_X 10
#define BTN_THEME_Y 10
#define BTN_THEME_W 36
#define BTN_THEME_H 22

// Botão alarmes: abaixo do botão tema, lado esquerdo
#define BTN_ALARM_X 10
#define BTN_ALARM_Y 36
#define BTN_ALARM_W 36
#define BTN_ALARM_H 22

// Botão brilho: abaixo do botão alarmes, lado esquerdo
#define BTN_BRIGHT_X 10
#define BTN_BRIGHT_Y 62
#define BTN_BRIGHT_W 36
#define BTN_BRIGHT_H 22

// Botão voltar (na tela de alarmes)
#define BTN_BACK_X 10
#define BTN_BACK_Y 10
#define BTN_BACK_W 50
#define BTN_BACK_H 26

// ========== STRINGS PT ==========
const char* diasSemana[] = {"Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sab"};
const char* diasCompletos[] = {"Domingo", "Segunda", "Terca", "Quarta", "Quinta", "Sexta", "Sabado"};
const char* meses[] = {"Jan", "Fev", "Mar", "Abr", "Mai", "Jun", "Jul", "Ago", "Set", "Out", "Nov", "Dez"};

// ========== CORES ==========
// MADCTL corrigido para RGB no setup - cores padrão funcionam
#define MY_YELLOW    0xFFE0
#define MY_CYAN      0x07FF
#define MY_ORANGE    0xFD20
#define MY_RED       0xF800
#define MY_GREEN     0x07E0
#define MY_BLUE      0x001F

// Cinzas são neutros (não dependem de RGB/BGR)
#define COLOR_DARK_BG      0x0000
#define COLOR_DARK_CARD    0x18E3
#define COLOR_DARK_BORDER  0x2945
#define COLOR_WARM_WHITE   0xFFDE
#define COLOR_LIGHT_BG     0xEF7D
#define COLOR_LIGHT_CARD   0xFFFF
#define COLOR_LIGHT_BORDER 0xD69A

// =====================================================================
// =====================================================================
// HTML
// =====================================================================
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
    latitude = preferences.getString("lat", "-27.5954");
    longitude = preferences.getString("lon", "-48.5480");
    gmtOffset = preferences.getInt("gmt", -3);
    darkTheme = preferences.getBool("theme", true);
    brightnessIdx = preferences.getUChar("bright", 3);
    preferences.end();
    loadAlarms();
}

void saveConfig(String ssid, String pass, String key, String city, String lat, String lon, int gmt) {
    preferences.begin("relogio", false);
    preferences.putString("ssid", ssid);
    preferences.putString("pass", pass);
    preferences.putString("apikey", key);
    preferences.putString("city", city);
    preferences.putString("lat", lat);
    preferences.putString("lon", lon);
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
        accentColor = MY_CYAN;
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

void setBrightness(uint8_t level) {
    ledcWrite(0, level);
}

void cycleBrightness() {
    brightnessIdx = (brightnessIdx + 1) % 4;
    setBrightness(brightnessLevels[brightnessIdx]);
    // Salva no NVS
    preferences.begin("relogio", false);
    preferences.putUChar("bright", brightnessIdx);
    preferences.end();
}

// =====================================================================
// UI HELPERS
// =====================================================================

void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
    tft.fillRoundRect(x, y, w, h, r, color);
    tft.drawRoundRect(x, y, w, h, r, cardBorder);
}

void drawButton(int x, int y, int w, int h, const char* text, uint16_t bg, uint16_t fg) {
    tft.fillRoundRect(x, y, w, h, 4, bg);
    tft.drawRoundRect(x, y, w, h, 4, cardBorder);
    tft.setTextColor(fg, bg);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(text, x + w/2, y + h/2, 1);
}

bool touchInZone(int tx, int ty, int zx, int zy, int zw, int zh) {
    return (tx >= zx && tx <= zx + zw && ty >= zy && ty <= zy + zh);
}

// =====================================================================
// REMOVER ACENTOS UTF-8
// =====================================================================

String removeAccents(String input) {
    String output = "";
    for (unsigned int i = 0; i < input.length(); i++) {
        uint8_t c = (uint8_t)input[i];
        if (c < 0x80) {
            // ASCII normal
            output += (char)c;
        } else if (c == 0xC3 && i + 1 < input.length()) {
            // Sequência UTF-8 de 2 bytes (0xC3 + próximo byte)
            i++;
            uint8_t next = (uint8_t)input[i];
            switch (next) {
                case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5:
                    output += 'a'; break;  // à á â ã ä å
                case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85:
                    output += 'A'; break;  // À Á Â Ã Ä Å
                case 0xA8: case 0xA9: case 0xAA: case 0xAB:
                    output += 'e'; break;  // è é ê ë
                case 0x88: case 0x89: case 0x8A: case 0x8B:
                    output += 'E'; break;  // È É Ê Ë
                case 0xAC: case 0xAD: case 0xAE: case 0xAF:
                    output += 'i'; break;  // ì í î ï
                case 0x8C: case 0x8D: case 0x8E: case 0x8F:
                    output += 'I'; break;  // Ì Í Î Ï
                case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6:
                    output += 'o'; break;  // ò ó ô õ ö
                case 0x92: case 0x93: case 0x94: case 0x95: case 0x96:
                    output += 'O'; break;  // Ò Ó Ô Õ Ö
                case 0xB9: case 0xBA: case 0xBB: case 0xBC:
                    output += 'u'; break;  // ù ú û ü
                case 0x99: case 0x9A: case 0x9B: case 0x9C:
                    output += 'U'; break;  // Ù Ú Û Ü
                case 0xA7: output += 'c'; break;  // ç
                case 0x87: output += 'C'; break;  // Ç
                case 0xB1: output += 'n'; break;  // ñ
                case 0x91: output += 'N'; break;  // Ñ
                default: output += '?'; break;
            }
        } else if (c >= 0xC0) {
            // Outros multi-byte UTF-8 - pula
            if (c >= 0xE0 && i + 2 < input.length()) i += 2;
            else if (c >= 0xC0 && i + 1 < input.length()) i += 1;
            output += '?';
        }
    }
    return output;
}

// =====================================================================
// WEATHER ICONS - cores corrigidas
// =====================================================================

void drawWeatherIcon(int cx, int cy, String icon, int size) {
    // Usar constantes da TFT_eSPI que já respeitam BGR/RGB do driver
    uint16_t sunColor = MY_YELLOW;
    uint16_t cloudColor = darkTheme ? 0xC618 : 0x8410;
    uint16_t rainColor = MY_BLUE;
    
    if (icon.startsWith("01")) {
        // Sol limpo - amarelo
        tft.fillCircle(cx, cy, size, sunColor);
        for (int i = 0; i < 8; i++) {
            float angle = i * PI / 4;
            int x1 = cx + cos(angle) * (size + 2);
            int y1 = cy + sin(angle) * (size + 2);
            int x2 = cx + cos(angle) * (size + 5);
            int y2 = cy + sin(angle) * (size + 5);
            tft.drawLine(x1, y1, x2, y2, sunColor);
        }
    } else if (icon.startsWith("02")) {
        // Sol parcial com nuvem
        tft.fillCircle(cx - size/2, cy - size/2, size*2/3, sunColor);
        // Raios do sol
        for (int i = 0; i < 5; i++) {
            float angle = -PI/2 + i * PI/5;
            int x1 = (cx - size/2) + cos(angle) * (size*2/3 + 2);
            int y1 = (cy - size/2) + sin(angle) * (size*2/3 + 2);
            int x2 = (cx - size/2) + cos(angle) * (size*2/3 + 4);
            int y2 = (cy - size/2) + sin(angle) * (size*2/3 + 4);
            tft.drawLine(x1, y1, x2, y2, sunColor);
        }
        tft.fillRoundRect(cx - size, cy - size/4, size*2, size, size/3, cloudColor);
    } else if (icon.startsWith("03") || icon.startsWith("04")) {
        // Nublado
        tft.fillCircle(cx - size/3, cy - size/4, size*2/3, cloudColor);
        tft.fillCircle(cx + size/3, cy - size/4, size/2, cloudColor);
        tft.fillRoundRect(cx - size, cy - size/6, size*2, size*2/3, size/3, cloudColor);
    } else if (icon.startsWith("09") || icon.startsWith("10")) {
        // Chuva
        tft.fillRoundRect(cx - size, cy - size/2, size*2, size*2/3, size/3, cloudColor);
        for (int i = 0; i < 3; i++) {
            int rx = cx - size/2 + i * (size/2);
            tft.drawLine(rx, cy + size/4, rx - 2, cy + size, rainColor);
        }
    } else if (icon.startsWith("11")) {
        // Tempestade
        tft.fillRoundRect(cx - size, cy - size/2, size*2, size*2/3, size/3, cloudColor);
        tft.fillTriangle(cx, cy+size/4, cx+4, cy+size/4, cx+2, cy+size, MY_YELLOW);
    } else if (icon.startsWith("13")) {
        // Neve
        tft.fillRoundRect(cx - size, cy - size/2, size*2, size*2/3, size/3, cloudColor);
        for (int i = 0; i < 3; i++) {
            int sx = cx - size/2 + i * (size/2);
            tft.fillCircle(sx, cy + size/2, 2, TFT_WHITE);
        }
    } else {
        // Névoa
        for (int i = 0; i < 3; i++) {
            int ly = cy - size/3 + i * (size/3);
            tft.drawFastHLine(cx - size, ly, size*2, dimColor);
        }
    }
}

// =====================================================================
// TELA PRINCIPAL DO RELÓGIO
// =====================================================================

void drawClockScreen() {
    tft.fillScreen(bgColor);
    
    // Card relógio (ocupa todo o topo)
    drawRoundRect(4, 4, SCREEN_WIDTH - 8, 126, 8, cardColor);
    
    // Botão TEMA (esquerda, em cima)
    tft.fillRoundRect(BTN_THEME_X, BTN_THEME_Y, BTN_THEME_W, BTN_THEME_H, 4, bgColor);
    tft.drawRoundRect(BTN_THEME_X, BTN_THEME_Y, BTN_THEME_W, BTN_THEME_H, 4, accentColor);
    tft.setTextColor(accentColor, bgColor);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(darkTheme ? "LUZ" : "ESC", BTN_THEME_X + BTN_THEME_W/2, BTN_THEME_Y + BTN_THEME_H/2 + 1, 1);
    
    // Botão ALARMES (esquerda, embaixo do tema)
    bool hasActive = false;
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (alarms[i].enabled) { hasActive = true; break; }
    }
    uint16_t alBtnColor = hasActive ? MY_ORANGE : dimColor;
    tft.fillRoundRect(BTN_ALARM_X, BTN_ALARM_Y, BTN_ALARM_W, BTN_ALARM_H, 4, bgColor);
    tft.drawRoundRect(BTN_ALARM_X, BTN_ALARM_Y, BTN_ALARM_W, BTN_ALARM_H, 4, alBtnColor);
    tft.setTextColor(alBtnColor, bgColor);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("AL", BTN_ALARM_X + BTN_ALARM_W/2, BTN_ALARM_Y + BTN_ALARM_H/2 + 1, 1);
    
    // Botão BRILHO (esquerda, embaixo do alarme)
    tft.fillRoundRect(BTN_BRIGHT_X, BTN_BRIGHT_Y, BTN_BRIGHT_W, BTN_BRIGHT_H, 4, bgColor);
    tft.drawRoundRect(BTN_BRIGHT_X, BTN_BRIGHT_Y, BTN_BRIGHT_W, BTN_BRIGHT_H, 4, dimColor);
    tft.setTextColor(dimColor, bgColor);
    tft.setTextDatum(MC_DATUM);
    String brLabel = String(brightnessLabels[brightnessIdx]) + "%";
    tft.drawString(brLabel, BTN_BRIGHT_X + BTN_BRIGHT_W/2, BTN_BRIGHT_Y + BTN_BRIGHT_H/2 + 1, 1);
    
    // Card clima (abaixo)
    drawRoundRect(4, 135, SCREEN_WIDTH - 8, SCREEN_HEIGHT - 152, 8, cardColor);
    
    // IP discreto no rodapé
    if (WiFi.status() == WL_CONNECTED) {
        tft.setTextColor(dimColor, bgColor);
        tft.setTextDatum(BC_DATUM);
        String ipStr = "http://" + WiFi.localIP().toString();
        tft.drawString(ipStr, SCREEN_WIDTH/2, SCREEN_HEIGHT - 2, 1);
    }
    
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
        // Limpa apenas a área central do relógio (não os botões)
        tft.fillRect(50, 30, SCREEN_WIDTH - 100, 70, cardColor);
        tft.setTextColor(textColor, cardColor);
        tft.setTextDatum(MR_DATUM);
        tft.drawString(hourStr, SCREEN_WIDTH/2 - 12, 65, 7);
        tft.setTextDatum(ML_DATUM);
        tft.drawString(minStr, SCREEN_WIDTH/2 + 12, 65, 7);
        lastTime = currentTime;
    }
    
    // Separador pulsante
    colonVisible = (timeinfo.tm_sec % 2 == 0);
    uint16_t colonColor = colonVisible ? accentColor : cardColor;
    tft.fillCircle(SCREEN_WIDTH/2, 55, 3, colonColor);
    tft.fillCircle(SCREEN_WIDTH/2, 72, 3, colonColor);
    
    // Segundos (abaixo do botão AL, à direita)
    String currentSec = String(secStr);
    if (currentSec != lastSec) {
        tft.fillRect(SCREEN_WIDTH - 46, 64, 38, 24, cardColor);
        tft.setTextColor(accentColor, cardColor);
        tft.setTextDatum(TR_DATUM);
        tft.drawString(secStr, SCREEN_WIDTH - 12, 66, 4);
        lastSec = currentSec;
    }
    
    // Data (parte inferior do card)
    String currentDate = String(dateStr);
    if (currentDate != lastDate) {
        tft.fillRect(10, 102, SCREEN_WIDTH - 20, 22, cardColor);
        tft.setTextColor(dimColor, cardColor);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(dateStr, SCREEN_WIDTH/2, 113, 2);
        lastDate = currentDate;
    }
}

// =====================================================================
// WEATHER DISPLAY
// =====================================================================

void displayWeather() {
    int cardY = 135;
    int cardH = SCREEN_HEIGHT - 152;
    
    tft.fillRect(6, cardY + 2, SCREEN_WIDTH - 12, cardH - 4, cardColor);
    
    if (!currentWeather.valid) {
        tft.setTextColor(dimColor, cardColor);
        tft.setTextDatum(MC_DATUM);
        if (apiKey.length() == 0) {
            tft.drawString("Acesse o IP abaixo para configurar", SCREEN_WIDTH/2, cardY + cardH/2 - 6, 1);
            tft.drawString("clima e alarmes via navegador", SCREEN_WIDTH/2, cardY + cardH/2 + 8, 1);
        } else {
            tft.drawString("Carregando clima...", SCREEN_WIDTH/2, cardY + cardH/2, 2);
        }
        return;
    }
    
    int leftX = 12;
    int topY = cardY + 6;
    
    // Ícone grande
    drawWeatherIcon(leftX + 22, topY + 20, currentWeather.icon, 12);
    
    // Temperatura
    char tempStr[8];
    snprintf(tempStr, sizeof(tempStr), "%.0f", currentWeather.temp);
    tft.setTextColor(textColor, cardColor);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(tempStr, leftX + 44, topY + 2, 6);
    
    int tempWidth = tft.textWidth(tempStr, 6);
    tft.setTextColor(accentColor, cardColor);
    tft.drawString("o", leftX + 46 + tempWidth, topY, 2);
    tft.setTextColor(textColor, cardColor);
    tft.drawString("C", leftX + 54 + tempWidth, topY + 6, 4);
    
    // Descrição
    tft.setTextColor(dimColor, cardColor);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(currentWeather.description, leftX + 2, topY + 44, 2);
    
    // Min/Max + Humidade
    char infoStr[30];
    snprintf(infoStr, sizeof(infoStr), "%.0f~%.0f C  %d%%", 
             currentWeather.tempMin, currentWeather.tempMax, currentWeather.humidity);
    tft.drawString(infoStr, leftX + 2, topY + 62, 1);
    
    // Cidade
    tft.setTextColor(accentColor, cardColor);
    tft.drawString(cityName, leftX + 2, topY + 75, 1);
    
    // Previsão lado direito
    int rightX = SCREEN_WIDTH/2 + 20;
    int foreY = topY + 2;
    
    tft.drawFastVLine(SCREEN_WIDTH/2 + 10, cardY + 6, cardH - 12, cardBorder);
    
    tft.setTextColor(accentColor, cardColor);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Proximos dias", rightX, foreY, 1);
    foreY += 12;
    
    for (int i = 0; i < 3; i++) {
        if (!forecast[i].valid) continue;
        int rowY = foreY + i * 24;
        
        tft.setTextColor(textColor, cardColor);
        tft.setTextDatum(TL_DATUM);
        tft.drawString(forecast[i].dayName, rightX, rowY + 3, 2);
        
        drawWeatherIcon(rightX + 40, rowY + 7, forecast[i].icon, 5);
        
        char foreStr[16];
        snprintf(foreStr, sizeof(foreStr), "%.0f/%.0f", forecast[i].tempMin, forecast[i].tempMax);
        tft.setTextColor(dimColor, cardColor);
        tft.drawString(foreStr, rightX + 55, rowY + 3, 2);
    }
}

// =====================================================================
// TELA DE ALARMES
// =====================================================================

void drawAlarmsScreen() {
    tft.fillScreen(bgColor);
    
    // Botão voltar
    drawButton(BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H, "<< VOL", accentColor, bgColor);
    
    // Título
    tft.setTextColor(accentColor, bgColor);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("ALARMES", SCREEN_WIDTH/2, 14, 4);
    
    // Linha
    tft.drawFastHLine(10, 42, SCREEN_WIDTH - 20, cardBorder);
    
    // Lista de alarmes
    int startY = 50;
    bool anyAlarm = false;
    
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (!alarms[i].enabled) continue;
        anyAlarm = true;
        
        int rowY = startY;
        startY += 36;
        
        // Card do alarme
        drawRoundRect(8, rowY, SCREEN_WIDTH - 16, 32, 6, cardColor);
        
        // Horário
        char timeStr[6];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", alarms[i].hour, alarms[i].minute);
        tft.setTextColor(textColor, cardColor);
        tft.setTextDatum(ML_DATUM);
        tft.drawString(timeStr, 20, rowY + 16, 4);
        
        // Label
        if (strlen(alarms[i].label) > 0) {
            tft.setTextColor(dimColor, cardColor);
            tft.setTextDatum(ML_DATUM);
            tft.drawString(alarms[i].label, 100, rowY + 16, 2);
        }
        
        // Indicador ativo (bolinha verde)
        tft.fillCircle(SCREEN_WIDTH - 24, rowY + 16, 5, MY_GREEN);
    }
    
    if (!anyAlarm) {
        tft.setTextColor(dimColor, bgColor);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Nenhum alarme ativo", SCREEN_WIDTH/2, 120, 2);
        tft.drawString("Configure pelo navegador:", SCREEN_WIDTH/2, 150, 1);
        if (WiFi.status() == WL_CONNECTED) {
            tft.setTextColor(accentColor, bgColor);
            tft.drawString("http://" + WiFi.localIP().toString(), SCREEN_WIDTH/2, 168, 2);
        }
    }
    
    // IP no rodapé
    if (WiFi.status() == WL_CONNECTED) {
        tft.setTextColor(dimColor, bgColor);
        tft.setTextDatum(BC_DATUM);
        tft.drawString("http://" + WiFi.localIP().toString(), SCREEN_WIDTH/2, SCREEN_HEIGHT - 4, 1);
    }
}

// =====================================================================
// FETCH WEATHER
// =====================================================================

void fetchWeather() {
    if (apiKey.length() == 0 || WiFi.status() != WL_CONNECTED) return;
    
    HTTPClient http;
    String url = "http://api.openweathermap.org/data/2.5/weather?lat=" + latitude 
                 + "&lon=" + longitude + "&appid=" + apiKey + "&units=metric&lang=pt_br";
    
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
            if (currentWeather.description.length() > 0) {
                currentWeather.description = removeAccents(currentWeather.description);
                currentWeather.description[0] = toupper(currentWeather.description[0]);
            }
        }
    }
    http.end();
    
    // Forecast
    url = "http://api.openweathermap.org/data/2.5/forecast?lat=" + latitude 
          + "&lon=" + longitude + "&appid=" + apiKey + "&units=metric&lang=pt_br";
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
    
    if (currentState == STATE_CLOCK) displayWeather();
}

// =====================================================================
// ALARMES
// =====================================================================

void checkAlarms() {
    if (alarmRinging) return;
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;
    
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (!alarms[i].enabled || alarms[i].triggered) continue;
        if (timeinfo.tm_hour == alarms[i].hour && timeinfo.tm_min == alarms[i].minute) {
            alarmRinging = true;
            alarmFlashTimer = millis();
            alarmFlashState = false;
            setBrightness(255);  // Brilho máximo no alarme
            alarms[i].triggered = true;
            Serial.printf("ALARME! %02d:%02d - %s\n", alarms[i].hour, alarms[i].minute, alarms[i].label);
            return;
        }
    }
    
    // Reset triggered
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
    if (millis() - alarmFlashTimer > 300) {
        alarmFlashTimer = millis();
        alarmFlashState = !alarmFlashState;
        
        if (alarmFlashState) {
            tft.fillScreen(MY_YELLOW);
            tft.setTextColor(MY_RED, MY_YELLOW);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("ALARME!", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 - 35, 4);
            for (int i = 0; i < MAX_ALARMS; i++) {
                if (alarms[i].triggered && alarms[i].enabled) {
                    char alStr[6];
                    snprintf(alStr, sizeof(alStr), "%02d:%02d", alarms[i].hour, alarms[i].minute);
                    tft.setTextColor(TFT_BLACK, MY_YELLOW);
                    tft.drawString(alStr, SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 15, 7);
                    if (strlen(alarms[i].label) > 0) {
                        tft.drawString(alarms[i].label, SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 60, 2);
                    }
                    break;
                }
            }
            tft.setTextColor(0x4208, MY_YELLOW);
            tft.drawString("Toque para desligar", SCREEN_WIDTH/2, SCREEN_HEIGHT - 25, 2);
        } else {
            tft.fillScreen(MY_RED);
            tft.setTextColor(TFT_WHITE, MY_RED);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("ALARME!", SCREEN_WIDTH/2, SCREEN_HEIGHT/2, 4);
        }
    }
}

void stopAlarm() {
    alarmRinging = false;
    currentState = STATE_CLOCK;
    setBrightness(brightnessLevels[brightnessIdx]);  // Restaura brilho
    applyTheme();
    drawClockScreen();
    displayWeather();
}

// =====================================================================
// AP MODE & WEB SERVER
// =====================================================================

void drawAPScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.fillRoundRect(10, 10, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 20, 12, COLOR_DARK_CARD);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(MY_CYAN, COLOR_DARK_CARD);
    tft.drawString("CONFIGURACAO", SCREEN_WIDTH/2, 45, 4);
    tft.setTextColor(TFT_WHITE, COLOR_DARK_CARD);
    tft.drawString("Conecte no WiFi:", SCREEN_WIDTH/2, 85, 2);
    tft.setTextColor(MY_ORANGE, COLOR_DARK_CARD);
    tft.drawString(AP_SSID, SCREEN_WIDTH/2, 115, 4);
    tft.setTextColor(TFT_WHITE, COLOR_DARK_CARD);
    tft.drawString("Senha: " + String(AP_PASS), SCREEN_WIDTH/2, 148, 2);
    tft.setTextColor(MY_CYAN, COLOR_DARK_CARD);
    tft.drawString("http://192.168.4.1", SCREEN_WIDTH/2, 180, 2);
    tft.setTextColor(0x4208, COLOR_DARK_CARD);
    tft.drawString("abra no navegador", SCREEN_WIDTH/2, 205, 1);
}

void handleRoot() {
    // Gera página com valores atuais
    String page = F("<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">"
    "<title>Relogio CYD</title><style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:-apple-system,sans-serif;background:#0f0f23;color:#eee;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px}"
    ".c{background:#1a1a3e;border-radius:16px;padding:24px;width:100%;max-width:460px;box-shadow:0 10px 40px rgba(0,0,0,0.4)}"
    "h1{text-align:center;margin-bottom:4px;font-size:1.3em;color:#0ff}"
    ".sub{text-align:center;color:#888;margin-bottom:18px;font-size:0.82em}"
    "h2{color:#0ff;font-size:1em;margin:18px 0 10px;padding-top:14px;border-top:1px solid #333}"
    "label{display:block;margin-bottom:3px;font-weight:500;color:#aaa;font-size:0.85em}"
    "input,select{width:100%;padding:10px;border:1px solid #333;border-radius:8px;background:#0a1628;color:#fff;font-size:14px;margin-bottom:12px;outline:none}"
    "input:focus,select:focus{border-color:#0ff}"
    ".row{display:flex;gap:10px}.row>div{flex:1}"
    "button{width:100%;padding:13px;background:linear-gradient(135deg,#0ff,#00bcd4);border:none;border-radius:8px;color:#0a1628;font-size:15px;font-weight:bold;cursor:pointer;margin-top:8px}"
    "button:active{transform:scale(0.98)}"
    ".alarm-row{display:flex;gap:8px;align-items:center;margin-bottom:8px;background:#0a1628;padding:8px 10px;border-radius:8px}"
    ".alarm-row input[type=time]{width:100px;margin:0;padding:8px;flex-shrink:0}"
    ".alarm-row input[type=text]{flex:1;margin:0;padding:8px}"
    ".alarm-row input[type=checkbox]{width:20px;height:20px;margin:0;flex-shrink:0;accent-color:#0ff}"
    ".alarm-label{color:#666;font-size:0.75em;display:flex;gap:8px;margin-bottom:4px}.alarm-label span{flex:1}"
    ".info{background:#0a1628;border-radius:8px;padding:10px;margin-top:14px;font-size:0.78em;color:#666;line-height:1.4}.info a{color:#0ff}"
    "</style></head><body><div class=\"c\">"
    "<h1>&#9201; Relogio CYD</h1><p class=\"sub\">WiFi + Clima + Alarmes</p>"
    "<form action=\"/save\" method=\"POST\">");
    
    page += "<label>Nome da Rede (SSID)</label>";
    page += "<input type=\"text\" name=\"ssid\" value=\"" + savedSSID + "\" required>";
    page += "<label>Senha do WiFi</label>";
    page += "<input type=\"password\" name=\"pass\" value=\"" + savedPassword + "\">";
    page += "<div class=\"row\"><div><label>Cidade (nome p/ display)</label>";
    page += "<input type=\"text\" name=\"city\" value=\"" + cityName + "\"></div>";
    page += "<div><label>Fuso (GMT)</label><select name=\"gmt\">";
    
    int gmtOptions[] = {-5, -4, -3, -2, 0, 1};
    const char* gmtLabels[] = {"-5 Acre", "-4 Manaus", "-3 Brasilia", "-2 Noronha", "+0 Londres", "+1 Europa"};
    for (int i = 0; i < 6; i++) {
        page += "<option value=\"" + String(gmtOptions[i]) + "\"";
        if (gmtOffset == gmtOptions[i]) page += " selected";
        page += ">" + String(gmtLabels[i]) + "</option>";
    }
    page += "</select></div></div>";
    
    page += "<div class=\"row\"><div><label>Latitude</label>";
    page += "<input type=\"text\" name=\"lat\" value=\"" + latitude + "\" placeholder=\"-27.5954\"></div>";
    page += "<div><label>Longitude</label>";
    page += "<input type=\"text\" name=\"lon\" value=\"" + longitude + "\" placeholder=\"-48.5480\"></div></div>";
    
    page += "<label>API Key OpenWeatherMap</label>";
    page += "<input type=\"text\" name=\"apikey\" value=\"" + apiKey + "\">";
    
    page += "<h2>&#9200; Alarmes</h2>";
    page += "<div class=\"alarm-label\"><span>Ativo</span><span>Hora</span><span>Descricao</span></div>";
    
    for (int i = 0; i < MAX_ALARMS; i++) {
        char timeStr[6];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", alarms[i].hour, alarms[i].minute);
        
        page += "<div class=\"alarm-row\">";
        page += "<input type=\"checkbox\" name=\"al" + String(i) + "_on\"";
        if (alarms[i].enabled) page += " checked";
        page += ">";
        page += "<input type=\"time\" name=\"al" + String(i) + "_time\" value=\"" + String(timeStr) + "\">";
        page += "<input type=\"text\" name=\"al" + String(i) + "_lbl\" value=\"" + String(alarms[i].label) + "\">";
        page += "</div>";
    }
    
    page += "<button type=\"submit\">Salvar e Conectar</button></form>";
    page += "<div class=\"info\">Clima: crie conta gratis em <a href=\"https://openweathermap.org/api\" target=\"_blank\">openweathermap.org</a> e cole a API key.<br>Alarmes: a tela pisca ao disparar. Toque para desligar.</div>";
    page += "</div></body></html>";
    
    server.send(200, "text/html", page);
}

void handleSave() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    String key = server.arg("apikey");
    String city = server.arg("city");
    String lat = server.arg("lat");
    String lon = server.arg("lon");
    int gmt = server.arg("gmt").toInt();
    
    if (ssid.length() == 0) {
        server.send(400, "text/plain", "SSID obrigatorio!");
        return;
    }
    if (city.length() == 0) city = "Florianopolis";
    if (lat.length() == 0) lat = "-27.5954";
    if (lon.length() == 0) lon = "-48.5480";
    saveConfig(ssid, pass, key, city, lat, lon, gmt);
    
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
}

// Mantém o WebServer ativo no modo station para acesso via IP local
void startStationWebServer() {
    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.begin();
    Serial.println("WebUI disponivel em: http://" + WiFi.localIP().toString());
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
// TOUCH HANDLING
// =====================================================================

void handleTouch() {
    if (touchscreen.tirqTouched() && touchscreen.touched()) {
        // Alarme tocando: qualquer toque desliga
        if (alarmRinging) {
            stopAlarm();
            delay(300);
            return;
        }
        
        if (!touchActive) {
            touchActive = true;
            touchStartTime = millis();
        }
        
        // Toque longo: reset config
        if (millis() - touchStartTime > 3000) {
            clearConfig();
            delay(300);
            ESP.restart();
        }
    } else {
        if (touchActive) {
            unsigned long dur = millis() - touchStartTime;
            touchActive = false;
            
            if (dur < 600 && millis() - lastTouchTime > 400) {
                lastTouchTime = millis();
                
                // Lê coordenadas do toque
                TS_Point p = touchscreen.getPoint();
                // Calibração ajustada para MADCTL 0xE0
                int tx = map(p.x, 3700, 200, 0, SCREEN_WIDTH);
                int ty = map(p.y, 3800, 200, 0, SCREEN_HEIGHT);
                
                Serial.printf("Touch: raw(%d,%d) mapped(%d,%d)\n", p.x, p.y, tx, ty);
                
                if (currentState == STATE_CLOCK) {
                    // Verifica botão TEMA
                    if (touchInZone(tx, ty, BTN_THEME_X, BTN_THEME_Y, BTN_THEME_W, BTN_THEME_H)) {
                        darkTheme = !darkTheme;
                        applyTheme();
                        saveTheme();
                        drawClockScreen();
                        displayWeather();
                        return;
                    }
                    // Verifica botão ALARMES
                    if (touchInZone(tx, ty, BTN_ALARM_X, BTN_ALARM_Y, BTN_ALARM_W, BTN_ALARM_H)) {
                        currentState = STATE_ALARMS;
                        drawAlarmsScreen();
                        return;
                    }
                    // Verifica botão BRILHO
                    if (touchInZone(tx, ty, BTN_BRIGHT_X, BTN_BRIGHT_Y, BTN_BRIGHT_W, BTN_BRIGHT_H)) {
                        cycleBrightness();
                        // Redesenha só o botão de brilho
                        tft.fillRoundRect(BTN_BRIGHT_X, BTN_BRIGHT_Y, BTN_BRIGHT_W, BTN_BRIGHT_H, 4, bgColor);
                        tft.drawRoundRect(BTN_BRIGHT_X, BTN_BRIGHT_Y, BTN_BRIGHT_W, BTN_BRIGHT_H, 4, dimColor);
                        tft.setTextColor(dimColor, bgColor);
                        tft.setTextDatum(MC_DATUM);
                        String brLabel = String(brightnessLabels[brightnessIdx]) + "%";
                        tft.drawString(brLabel, BTN_BRIGHT_X + BTN_BRIGHT_W/2, BTN_BRIGHT_Y + BTN_BRIGHT_H/2 + 1, 1);
                        return;
                    }
                } else if (currentState == STATE_ALARMS) {
                    // Botão voltar
                    if (touchInZone(tx, ty, BTN_BACK_X, BTN_BACK_Y, BTN_BACK_W, BTN_BACK_H)) {
                        currentState = STATE_CLOCK;
                        drawClockScreen();
                        displayWeather();
                        return;
                    }
                    // Toque em qualquer outra área também volta
                    currentState = STATE_CLOCK;
                    drawClockScreen();
                    displayWeather();
                }
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
    // Após setRotation, corrige RGB order no MADCTL
    // Rotation 1 com ILI9341_2 seta MADCTL=0x28 (BGR). Trocamos para 0x20 (RGB)
    tft.writecommand(0x36);
    tft.writedata(0xE0);  // MY=1, MX=1, MV=1, RGB order
    tft.fillScreen(TFT_BLACK);
    
    pinMode(TFT_BL, OUTPUT);
    // Configura PWM para controle de brilho (canal 0, 5kHz, 8 bits)
    ledcSetup(0, 5000, 8);
    ledcAttachPin(TFT_BL, 0);
    ledcWrite(0, brightnessLevels[brightnessIdx]);
    
    touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touchscreen.begin(touchSPI);
    touchscreen.setRotation(1);
    
    loadConfig();
    applyTheme();
    
    if (savedSSID.length() > 0) {
        if (connectWiFi()) {
            syncTime();
            currentState = STATE_CLOCK;
            startStationWebServer();  // WebUI acessível pelo IP
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
            // WebServer para reconfiguração pelo IP
            server.handleClient();
            
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
            
            static unsigned long lastAlarmCheck = 0;
            if (millis() - lastAlarmCheck >= 1000) {
                lastAlarmCheck = millis();
                checkAlarms();
            }
            
            if (millis() - lastWeatherUpdate >= WEATHER_INTERVAL) {
                lastWeatherUpdate = millis();
                fetchWeather();
            }
            
            handleTouch();
            
            static unsigned long lastReconnect = 0;
            if (WiFi.status() != WL_CONNECTED && millis() - lastReconnect > 30000) {
                lastReconnect = millis();
                WiFi.reconnect();
            }
            break;
        }
        
        case STATE_ALARMS:
            server.handleClient();
            handleTouch();
            
            // Continua checando alarmes mesmo na tela de alarmes
            static unsigned long lastAlarmCheckAl = 0;
            if (millis() - lastAlarmCheckAl >= 1000) {
                lastAlarmCheckAl = millis();
                checkAlarms();
            }
            if (alarmRinging) {
                handleAlarmDisplay();
            }
            break;
            
        default: break;
    }
    delay(10);
}
