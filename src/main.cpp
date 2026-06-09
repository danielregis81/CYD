/**
 * Relógio Digital - ESP32 CYD 2.8" (ESP32-2432S028R)
 * 
 * Funcionalidades:
 * - Conecta ao WiFi e sincroniza horário via NTP
 * - Exibe hora, minutos e segundos em formato grande
 * - Exibe a data completa abaixo do horário
 * - Atualiza automaticamente a cada segundo
 * - Toque na tela alterna entre tema claro e escuro
 */

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// ========== CONFIGURAÇÕES WiFi ==========
// Altere para sua rede WiFi
const char* WIFI_SSID     = "SEU_WIFI_AQUI";
const char* WIFI_PASSWORD = "SUA_SENHA_AQUI";

// ========== CONFIGURAÇÕES NTP ==========
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET_SEC = -3 * 3600;  // UTC-3 (Brasília)
const int   DAYLIGHT_OFFSET_SEC = 0;      // Sem horário de verão

// ========== PINOS DO TOUCHSCREEN ==========
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

// ========== CONSTANTES DO DISPLAY ==========
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

// ========== OBJETOS GLOBAIS ==========
TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

// ========== VARIÁVEIS DE ESTADO ==========
bool darkTheme = true;
uint16_t bgColor;
uint16_t textColor;
uint16_t accentColor;
uint16_t dimColor;

String lastTime = "";
String lastDate = "";
String lastSec  = "";

unsigned long lastTouchTime = 0;

// ========== NOMES DOS DIAS E MESES EM PORTUGUÊS ==========
const char* diasSemana[] = {
    "Domingo", "Segunda", "Terca", "Quarta", "Quinta", "Sexta", "Sabado"
};
const char* meses[] = {
    "Jan", "Fev", "Mar", "Abr", "Mai", "Jun",
    "Jul", "Ago", "Set", "Out", "Nov", "Dez"
};

// ========== FUNÇÕES ==========

void applyTheme() {
    if (darkTheme) {
        bgColor     = TFT_BLACK;
        textColor   = TFT_WHITE;
        accentColor = TFT_CYAN;
        dimColor    = TFT_DARKGREY;
    } else {
        bgColor     = TFT_WHITE;
        textColor   = TFT_BLACK;
        accentColor = TFT_NAVY;
        dimColor    = TFT_LIGHTGREY;
    }
}

void drawFullScreen() {
    tft.fillScreen(bgColor);
    
    // Título
    tft.setTextColor(dimColor, bgColor);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("RELOGIO NTP", SCREEN_WIDTH / 2, 8, 2);
    
    // Linha separadora
    tft.drawFastHLine(20, 28, SCREEN_WIDTH - 40, dimColor);
    
    // Força redesenho completo
    lastTime = "";
    lastDate = "";
    lastSec  = "";
}

void connectWiFi() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Conectando WiFi...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 20, 2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(WIFI_SSID, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 10, 2);
    
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
        
        // Animação de loading
        int dotX = 60 + (attempts % 10) * 20;
        tft.fillCircle(dotX, SCREEN_HEIGHT / 2 + 40, 4, TFT_CYAN);
        if (attempts > 1) {
            int prevX = 60 + ((attempts - 1) % 10) * 20;
            tft.fillCircle(prevX, SCREEN_HEIGHT / 2 + 40, 4, TFT_DARKGREY);
        }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi conectado!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.drawString("WiFi Conectado!", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 10, 2);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString(WiFi.localIP().toString(), SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 15, 2);
        delay(1500);
    } else {
        Serial.println("\nFalha ao conectar WiFi!");
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("Falha WiFi!", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 10, 2);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.drawString("Usando hora local", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 15, 2);
        delay(2000);
    }
}

void syncTime() {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    
    Serial.println("Sincronizando horario NTP...");
    
    struct tm timeinfo;
    int retries = 0;
    while (!getLocalTime(&timeinfo) && retries < 10) {
        delay(500);
        retries++;
    }
    
    if (retries < 10) {
        Serial.println("Horario sincronizado!");
        Serial.printf("Hora: %02d:%02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        Serial.println("Falha ao sincronizar NTP");
    }
}

void displayClock() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return;
    }
    
    // Formata hora e minutos
    char timeStr[6];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    
    // Formata segundos
    char secStr[4];
    snprintf(secStr, sizeof(secStr), "%02d", timeinfo.tm_sec);
    
    // Formata data
    char dateStr[40];
    snprintf(dateStr, sizeof(dateStr), "%s, %02d %s %04d",
             diasSemana[timeinfo.tm_wday],
             timeinfo.tm_mday,
             meses[timeinfo.tm_mon],
             timeinfo.tm_year + 1900);
    
    // Atualiza hora/minutos somente se mudou
    String currentTime = String(timeStr);
    if (currentTime != lastTime) {
        // Limpa área do horário
        tft.fillRect(0, 50, SCREEN_WIDTH - 60, 90, bgColor);
        
        tft.setTextColor(textColor, bgColor);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(timeStr, SCREEN_WIDTH / 2 - 25, 100, 7);
        
        lastTime = currentTime;
    }
    
    // Atualiza segundos
    String currentSec = String(secStr);
    if (currentSec != lastSec) {
        // Limpa área dos segundos
        tft.fillRect(SCREEN_WIDTH - 55, 70, 55, 40, bgColor);
        
        tft.setTextColor(accentColor, bgColor);
        tft.setTextDatum(TL_DATUM);
        tft.drawString(secStr, SCREEN_WIDTH - 50, 80, 4);
        
        lastSec = currentSec;
    }
    
    // Atualiza data somente se mudou
    String currentDate = String(dateStr);
    if (currentDate != lastDate) {
        tft.fillRect(0, 150, SCREEN_WIDTH, 30, bgColor);
        
        tft.setTextColor(accentColor, bgColor);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(dateStr, SCREEN_WIDTH / 2, 165, 2);
        
        lastDate = currentDate;
        
        // Linha decorativa abaixo da data
        tft.drawFastHLine(60, 185, SCREEN_WIDTH - 120, dimColor);
    }
    
    // Indicador WiFi no rodapé
    tft.setTextDatum(BC_DATUM);
    if (WiFi.status() == WL_CONNECTED) {
        tft.setTextColor(TFT_GREEN, bgColor);
        tft.drawString("WiFi OK", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 5, 1);
    } else {
        tft.setTextColor(TFT_RED, bgColor);
        tft.drawString("WiFi OFF", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 5, 1);
    }
}

void checkTouch() {
    if (touchscreen.tirqTouched() && touchscreen.touched()) {
        // Debounce
        if (millis() - lastTouchTime > 500) {
            lastTouchTime = millis();
            
            // Alterna tema
            darkTheme = !darkTheme;
            applyTheme();
            drawFullScreen();
            
            Serial.println(darkTheme ? "Tema: Escuro" : "Tema: Claro");
        }
    }
}

// ========== SETUP ==========
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Relogio ESP32 CYD ===");
    
    // Inicializa o display
    tft.init();
    tft.setRotation(1);  // Paisagem
    tft.fillScreen(TFT_BLACK);
    
    // Backlight ON
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    
    // Inicializa o touchscreen
    touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touchscreen.begin(touchSPI);
    touchscreen.setRotation(1);
    
    // Conecta WiFi
    connectWiFi();
    
    // Sincroniza horário
    syncTime();
    
    // Aplica tema e desenha tela
    applyTheme();
    drawFullScreen();
    
    Serial.println("Setup concluido!");
}

// ========== LOOP ==========
void loop() {
    // Atualiza relógio a cada segundo
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate >= 1000) {
        lastUpdate = millis();
        displayClock();
    }
    
    // Verifica toque para trocar tema
    checkTouch();
    
    // Reconecta WiFi se desconectado
    static unsigned long lastReconnect = 0;
    if (WiFi.status() != WL_CONNECTED && millis() - lastReconnect > 30000) {
        lastReconnect = millis();
        Serial.println("Reconectando WiFi...");
        WiFi.reconnect();
    }
    
    delay(10);
}
