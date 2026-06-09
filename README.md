# Relógio Digital - ESP32 CYD 2.8"

Relógio digital com sincronização NTP para a placa **ESP32 CYD** (Cheap Yellow Display - ESP32-2432S028R).

## Funcionalidades

- Sincronização de horário via NTP (WiFi)
- Display grande com hora, minutos e segundos
- Data completa em português (dia da semana, dia, mês, ano)
- Toque na tela para alternar entre tema claro e escuro
- Reconexão automática do WiFi
- Fuso horário configurável (padrão: UTC-3 Brasília)

## Hardware

- ESP32 CYD 2.8" (ESP32-2432S028R)
- Display ILI9341 240x320 integrado
- Touch resistivo XPT2046 integrado

## Como usar

### 1. Configurar WiFi

Edite o arquivo `src/main.cpp` e altere as credenciais:

```cpp
const char* WIFI_SSID     = "SEU_WIFI_AQUI";
const char* WIFI_PASSWORD = "SUA_SENHA_AQUI";
```

### 2. Configurar Fuso Horário

O padrão é UTC-3 (Brasília). Para alterar:

```cpp
const long GMT_OFFSET_SEC = -3 * 3600;  // Altere o -3 para seu fuso
```

### 3. Compilar e Enviar

```bash
pio run            # Compilar
pio run -t upload  # Enviar para a placa
pio device monitor # Monitor serial (115200 baud)
```

## Estrutura do Projeto

```
CYD/
├── platformio.ini    # Configuração PlatformIO + pinos do display
├── src/
│   └── main.cpp      # Código principal do relógio
├── .gitignore
└── README.md
```

## Bibliotecas utilizadas

| Biblioteca | Versão | Função |
|---|---|---|
| TFT_eSPI | 2.5.43 | Driver do display ILI9341 |
| XPT2046_Touchscreen | 1.4.0 | Driver do touch |
| WiFi (built-in) | - | Conexão WiFi |

## Interação

- **Toque na tela**: Alterna entre tema escuro e claro
