# Relogio Digital - ESP32 CYD 2.8"

Relogio digital com previsao do tempo e alarmes para a placa **ESP32 CYD** (ESP32-2432S028R).

## Funcionalidades

- **Portal captive** para configuracao WiFi (sem editar codigo)
- Sincronizacao de horario via NTP
- Display com hora grande (7-segment), segundos, data completa
- **Previsao do tempo** via OpenWeatherMap (clima atual + 3 dias)
- **Ate 5 alarmes** configuraveis pela interface web
- Alarme visual: tela pisca amarelo/vermelho ate tocar no display
- Alarme sempre dispara com brilho 100%
- **Controle de brilho** com 4 niveis (25%, 50%, 75%, 100%)
- **Dois temas**: escuro e claro (alternavel por botao)
- Tela de visualizacao dos alarmes ativos
- WebUI acessivel pelo IP na rede local (exibe configuracoes atuais)
- Coordenadas geograficas (lat/lon) para precisao na previsao
- Tratamento de acentos UTF-8 (exibe sem acentos no display)
- Configuracoes salvas na flash (sobrevive a reboot)
- Toque longo (3s): reseta tudo e volta ao modo configuracao

## Como usar

### 1. Gravar na placa

```bash
pio run -t upload --upload-port COM6
```

### 2. Configurar (primeira vez)

1. A placa cria a rede WiFi **Relogio-CYD** (senha: `12345678`)
2. Conecte seu celular/PC nessa rede
3. Abra **http://192.168.4.1** no navegador
4. Preencha: SSID, senha, cidade, latitude, longitude, fuso horario, API key e alarmes
5. Clique "Salvar e Conectar"

### 3. Reconfigurar (ja conectado)

Acesse o IP exibido no rodape do display (ex: `http://192.168.0.17`) pelo navegador na mesma rede. A pagina mostra as configuracoes atuais ja preenchidas.

### 4. Interacao pelo display

| Botao | Acao |
|---|---|
| **ESC/LUZ** | Alterna tema escuro/claro |
| **AL** | Mostra tela com alarmes ativos |
| **75%** | Cicla brilho: 25% → 50% → 75% → 100% |
| **Segura 3s** | Reseta config, volta ao modo AP |

### 5. Previsao do tempo

Crie uma conta gratuita em [openweathermap.org](https://openweathermap.org/api) e cole a API key na configuracao. Gratis: 1000 chamadas/dia. O relogio consulta a cada 10 minutos.

Para coordenadas da sua cidade, use [Google Maps](https://maps.google.com) (clique direito → "O que ha aqui?" mostra lat/lon).

## Hardware

- ESP32 CYD 2.8" (ESP32-2432S028R)
- Display ILI9341 240x320 integrado
- Touch resistivo XPT2046 integrado

## Estrutura

```
CYD/
├── platformio.ini    # Config PlatformIO + pinos do display
├── src/
│   └── main.cpp      # Codigo principal
├── .gitignore
└── README.md
```

## Bibliotecas

| Biblioteca | Funcao |
|---|---|
| TFT_eSPI | Driver do display ILI9341 |
| XPT2046_Touchscreen | Driver do touch |
| ArduinoJson | Parse da API OpenWeatherMap |
| WebServer | Interface web de configuracao |
| DNSServer | Captive portal |
| Preferences | Salvar config na flash |

## Compilacao

```bash
pio run              # Compilar
pio run -t upload    # Gravar na placa
pio device monitor   # Monitor serial (debug)
```
