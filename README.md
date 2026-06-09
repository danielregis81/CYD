# Relógio Digital - ESP32 CYD 2.8"

Relógio digital com portal de configuração WiFi para a placa **ESP32 CYD** (ESP32-2432S028R).

## Funcionalidades

- **Portal captive** para configuração WiFi (sem precisar editar código)
- Sincronização de horário via NTP
- Display grande com hora, minutos e segundos
- Data completa em português
- Fuso horário configurável pela interface web
- Toque curto na tela: alterna tema claro/escuro
- Toque longo (3s): reseta configuração e volta ao modo AP
- Configurações salvas na memória flash (sobrevive a reboot)

## Como usar

### 1. Gravar na placa

```bash
pio run -t upload
```

### 2. Configurar WiFi

Na primeira vez (ou após reset):

1. A placa cria uma rede WiFi chamada **Relogio-CYD** (senha: `12345678`)
2. Conecte seu celular/PC nessa rede
3. Abra o navegador em **http://192.168.4.1**
4. Preencha o nome da rede, senha e fuso horário
5. Clique "Salvar e Conectar"
6. O relógio reinicia e conecta na sua rede

### 3. Uso diário

- O relógio sincroniza a hora automaticamente pela internet
- **Toque rápido** na tela: alterna entre tema escuro e claro
- **Segure 3 segundos**: reseta WiFi e volta ao modo configuração

## Hardware

- ESP32 CYD 2.8" (ESP32-2432S028R)
- Display ILI9341 240x320 integrado
- Touch resistivo XPT2046 integrado

## Estrutura

```
CYD/
├── platformio.ini    # Config PlatformIO + pinos do display
├── src/
│   └── main.cpp      # Código principal
├── .gitignore
└── README.md
```

## Bibliotecas

| Biblioteca | Função |
|---|---|
| TFT_eSPI | Driver do display |
| XPT2046_Touchscreen | Driver do touch |
| WebServer | Servidor HTTP para configuração |
| DNSServer | Captive portal (redireciona automaticamente) |
| Preferences | Salvar config na flash |
