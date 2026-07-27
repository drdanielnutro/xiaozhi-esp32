# Dossiê da placa — Waveshare ESP32-P4-WIFI6-Touch-LCD-7B

> Documento de referência do hardware alvo do firmware Professor Virtual.
> Objetivo: concentrar em um só lugar identidade da placa, periféricos,
> drivers, arquivos do repositório, comandos de build, referências externas
> e perguntas abertas de bring-up. Toda afirmação indica a fonte
> (arquivo do repo ou URL). Especificação do produto: `DOCUMENTACAO-APP.md`
> (raiz do repo).

## 1. Identidade e variantes de build

A placa **já é suportada por este repositório**. Diretório da família:
`main/boards/waveshare/esp32-p4-wifi6-touch-lcd/` (classe `WaveshareEsp32p4`,
`DECLARE_BOARD` em `esp32-p4-wifi6-touch-lcd.cc:484`).

Variantes de build da 7B declaradas em
`main/boards/waveshare/esp32-p4-wifi6-touch-lcd/config.json`:

| Variante | Para qual chip | Flags distintivas |
|---|---|---|
| `esp32-p4-wifi6-touch-lcd-7b` | ESP32-P4 revisões v1.x–v2.x | `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`, `CONFIG_ESP32P4_REV_MIN_100=y` |
| `esp32-p4-wifi6-touch-lcd-7b-p4x` | Revisões mais novas do silício (sem restrição `<v3`) | Sem as duas flags de revisão |

Ambas definem: `CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_7B=y`,
câmera `CONFIG_CAMERA_OV5647=y` (MIPI RAW8 800×800@50fps, auto-detect),
`CONFIG_USE_DEVICE_AEC=y`, flash de 32 MB
(`CONFIG_ESPTOOLPY_FLASHSIZE_32MB=y`) e tabela de partições
`partitions/v2/32m.csv`.

**Como descobrir a revisão do chip da unidade física** (define a variante):
ler a linha `chip revision: vX.Y` no log de boot pela serial, ou rodar
`esptool.py --port <porta> chip_id` com a placa em mãos. Regra: revisão
v1.x/v2.x → variante `7b`; revisão v3.x ou superior → `7b-p4x`.

Elos da cadeia de seleção no repo:
- Kconfig: `main/Kconfig.projbuild:411`
  (`BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_7B`).
- CMake: `main/CMakeLists.txt:556` (define `MANUFACTURER "waveshare"`,
  `BOARD_TYPE "esp32-p4-wifi6-touch-lcd"`, fontes
  `font_noto_sans_basic_30_4` + `font_material_symbols_30_4`, emojis
  `noto-color-emoji_64`).

## 2. Inventário de hardware

Fontes: código do repo (indicado por arquivo) e páginas Waveshare
(seção 6). A documentação Waveshare confirma: módulo **ESP32-P4NRW32**
(ESP32-P4 dual-core RISC-V até 360 MHz no sistema HP + núcleo LP), com
**32 MB de PSRAM empilhada no encapsulamento + 32 MB de NOR flash**, e
co-processador **ESP32-C6-MINI-1 via SDIO** para Wi-Fi 6/BLE 5.

| Periférico | Chip/controlador | Driver/componente | Pinos/config (fonte) |
|---|---|---|---|
| Display 7" 1024×600 | EK79007, MIPI-DSI 2 lanes | `espressif/esp_lcd_ek79007` (`main/idf_component.yml:102`); painel criado em `esp32-p4-wifi6-touch-lcd.cc:246` | Reset GPIO33, backlight GPIO32 (PWM, invertido), DPI 52 MHz, RGB565, 1 framebuffer, bitrate 900 Mbps/lane (`config.h:46-52`, `.cc:216-246`) |
| Touch capacitivo 5 pontos | GT911, I2C | `espressif/esp_lcd_touch_gt911` (`main/idf_component.yml:58`); init em `.cc:359-411` com probe de endereço + fallback | Barramento I2C_NUM_1 compartilhado (SDA GPIO7, SCL GPIO8), sem pinos de reset/int (`config.h:18-19`, `.cc:365-366`) |
| Codec de áudio (saída) | ES8311 | Classe `BoxAudioCodec` (`main/audio/codecs/box_audio_codec.*`), instanciada em `.cc:452-467` | I2S: MCLK 13, WS 10, BCLK 12, DIN 11, DOUT 9; PA GPIO53; 24 kHz in/out (`config.h:6-21`) |
| Captura/AEC (entrada) | ES7210 (dual mic onboard, com referência de eco) | Mesmo `BoxAudioCodec`, `AUDIO_INPUT_REFERENCE=true` (`config.h:9`); AFE/AEC do projeto habilitado para P4 (`main/Kconfig.projbuild:880-896`) | Mesmo barramento I2S/I2C |
| Câmera | OV5647, MIPI-CSI 2 lanes (conector 15 pinos, padrão RPi) | Wrapper `EspVideo` (`main/boards/common/esp_video.cc`, 1052 linhas) sobre `espressif/esp_video` (`main/idf_component.yml:47`, alvo esp32p4/s3); init em `.cc:412-428`. Obs.: `espressif/esp32-camera`/`Esp32Camera` é o caminho **restrito ao S3** (`main/idf_component.yml:43-46`, `main/CMakeLists.txt:1030-1037`) — não participa do build P4 | SCCB no I2C_NUM_1, sem pinos reset/pwdn; sensor definido por `CONFIG_CAMERA_OV5647` (config.json) |
| Wi-Fi / BLE | ESP32-C6-MINI-1 via SDIO | Classe base `WifiBoard` (`main/boards/common/wifi_board.*`) + `espressif/esp_hosted` (`main/idf_component.yml:106`) + `espressif/esp_wifi_remote` (`:110`) | PSRAM p/ Wi-Fi: `sdkconfig.defaults.esp32p4` (SPIRAM 200 MHz, XIP) |
| Botão BOOT | GPIO35 | `Button` (`.cc:429-439`): em `kDeviceStateStarting` entra em modo de configuração Wi-Fi; depois, alterna chat | `config.h:23` |
| Alto-falante | Conector PH2.0 (não é onboard) | — | Wiki Waveshare (seção 6) |
| USB / microSD / RTC | USB 2.0 OTG HS (Type-A); slot TF SDIO 3.0; suporte de bateria RTC 1220; bateria via MX1.25; chave ON/OFF | Não usados pelo firmware atual da placa | Wiki Waveshare (seção 6) |

## 3. Mapa de arquivos do repositório

Placa (tudo em `main/boards/waveshare/esp32-p4-wifi6-touch-lcd/`):
- `esp32-p4-wifi6-touch-lcd.cc` — classe da placa, blocos condicionais por
  variante (`CONFIG_BOARD_TYPE_..._7B` nas linhas 22, 46, 215, 246).
- `config.h` — pinos e parâmetros por variante (bloco 7B: linhas 46–52).
- `config.json` — as 18 variantes de build da família (7B: `7b` e `7b-p4x`).
- `lcd_init_cmds.h` (1123 linhas) — comandos de init de LCD para os painéis
  ST7701, JD9365 e ILI9881C (o EK79007 da 7B não os usa).
- `README.md` — lista dos modelos da família.

Helpers comuns usados pela placa (`main/boards/common/`):
- `wifi_board.*` — rede Wi-Fi (via esp_hosted/esp_wifi_remote no P4).
- `lcd_display.*` — `MipiLcdDisplay` + LVGL (`esp_lvgl_port`,
  `main/idf_component.yml:63`).
- `../audio/codecs/box_audio_codec.*` — ES8311+ES7210.
- `esp_video.*` — câmera MIPI-CSI: captura, conversão JPEG (software e
  hardware) e **upload HTTP multipart de um único JPEG** (`EspVideo::Explain()`,
  `esp_video.cc:911`, com `Content-Type: multipart/form-data`, chunked,
  um campo fixo `file`/`camera.jpg` — linhas 956–1034; cada captura
  substitui o único frame retido, `esp_video.cc:410`).
- `esp32_camera.cc` — segundo exemplo de multipart (linhas 238–305), mas
  **compilado apenas para S3** (`main/CMakeLists.txt:1037`); serve só como
  referência do formato.
- `camera.h` — interface `Camera` do core.

Configuração e build:
- `main/Kconfig.projbuild` — tipos de placa (linhas 402–431 para a família);
  AFE/AEC (`USE_AUDIO_PROCESSOR`/`USE_DEVICE_AEC`, linhas 880–896, incluem
  a 7B).
- `main/CMakeLists.txt:556-561` — mapeamento da 7B para o diretório.
- `partitions/v2/32m.csv` — nvsfactory 200K, nvs 840K, otadata, phy_init,
  **ota_0 4M @0x200000, ota_1 4M @0x600000, assets (spiffs) 16M @0xA00000**.
- `sdkconfig.defaults.esp32p4` — PSRAM 200 MHz com XIP, reservas de heap
  interno, Wi-Fi/LWIP preferindo PSRAM.
- `sdkconfig.defaults:56` — `CONFIG_LV_USE_LODEPNG=y` (PNG no LVGL).
- `main/idf_component.yml` — componentes citados acima, mais
  `espressif/esp_audio_codec ~2.5.0` (linha 25),
  `espressif/esp_audio_effects ~1.3.0` (linha 24),
  `espressif/esp_new_jpeg ^0.6.1` (linha 73) e `espressif/esp-sr ~2.4.7`
  (linha 36, wake word/AFE).

## 4. Build e gravação

```sh
# Ambiente (ESP-IDF v6.0.2 — política do repo)
source /caminho/do/esp-idf/export.sh

# Listar variantes exatas
python3 scripts/release.py --list-boards | grep 7b

# Build canônico da variante (chip v1.x/v2.x)
python3 scripts/release.py waveshare/esp32-p4-wifi6-touch-lcd --name esp32-p4-wifi6-touch-lcd-7b

# Ou, para silício novo:
python3 scripts/release.py waveshare/esp32-p4-wifi6-touch-lcd --name esp32-p4-wifi6-touch-lcd-7b-p4x
```

Avisos:
- O `scripts/release.py` **altera o `sdkconfig` local** e o estado do
  `build/`; não presuma que o diretório de build ainda representa um alvo
  anterior (regra do repo em `CLAUDE.md`).
- O `release.py` **pula a variante se o ZIP já existir** em `releases/`
  (`scripts/release.py:467-470`, "Skipping ... already exists"): confirme
  no log que o build realmente executou; para rebuildar, remova antes o
  ZIP correspondente.
- **Gravar a placa (`idf.py flash`, `esptool`) é ação exclusiva do
  proprietário** — os denies de `.claude/settings.json` refletem isso.
- Build que compila **não** é validação de hardware.

## 5. Capacidades vs necessidades do Professor Virtual

Mapeamento entre o que a spec exige do cliente (`DOCUMENTACAO-APP.md`,
seções 7–9 e Apêndice B) e o que o repo já oferece:

| Necessidade PV | O que já existe (onde) | Lacuna/risco a tratar na Etapa 2 |
|---|---|---|
| Foto JPEG da página p/ `POST /api/prepare` e `/api/turn` | Câmera OV5647 + conversão JPEG com opção de encoder por hardware (`esp_video.cc:202`, `CONFIG_XIAOZHI_ENABLE_HARDWARE_JPEG_ENCODER`; componente `esp_new_jpeg`) | Enquadrar página A4 em retrato, foco/nitidez e tamanho final — validação física (spec B.4). Sensor configurado hoje para 800×800 RAW8; avaliar modo/resolução adequados p/ legibilidade |
| Upload HTTP `multipart/form-data` | **Existe para um único arquivo**: `EspVideo::Explain()` (`esp_video.cc:911-1034`) monta multipart com boundary, chunked, um campo fixo `file` com o único frame retido (`esp_video.cc:410`) | Adaptar para os campos do contrato PV (`image`/`audio` + `session_id` no turn); **lacuna própria para `/api/prepare`**: manter o lote de até 20 páginas na ordem, com excluir/refazer, e enviá-lo numa única requisição com o campo `files` repetido (spec §7.4 e §9.8) — o código atual descarta cada frame na captura seguinte. **Risco de viabilidade a medir**: o contrato admite até 20×10 MB (spec §7.4), muito acima dos 32 MB de PSRAM compartilhados com framebuffer, câmera e AFE; o firmware precisa gerar JPEGs pequenos o bastante (spec §8.1 indica centenas de KB/página) e a estratégia de retenção (PSRAM, compressão, limite por página) exige orçamento agregado medido — não está definida. Timeouts longos (≥120 s no turn; spec §8.1); **proibido retry automático de turno** |
| Gravar áudio da criança (≤30 s) p/ o turno | Captura I2S/codec a 24 kHz em **2 canais** (mic + referência de eco: `box_audio_codec.cc:15-16`, `config.h:6-9`), reamostrada para 16 kHz **preservando os canais** (`main/audio/audio_service.cc:79-82`) antes do AFE (formato `M+R`, `main/audio/engines/afe_audio_engine.cc:116`); saída do AFE e encoder Opus em **16 kHz mono** (`main/audio/audio_service.h:65-66`); AFE/AEC/VAD via ES7210 + `esp-sr` (`main/Kconfig.projbuild:880-896`) | Backend hoje recebe WebM/Opus do navegador e repassa bytes+MIME à API multimodal (spec §8): definir formato do firmware (ex.: WAV/PCM ou Opus em contêiner) e **validar formalmente com o backend** antes de adotar |
| Tocar a voz do tutor (MP3 base64, dezenas–centenas de KB) | Componente `espressif/esp_audio_codec ~2.5.0` no manifest (`main/idf_component.yml:25`) — fornece decoders de áudio, incluindo MP3, conforme documentação do componente (seção 6) | **Nenhum uso de MP3 no código hoje** (grep sem resultados em `main/`): decodificação MP3→PCM e reamostragem p/ 24 kHz são trabalho novo; watchdog de 10 s do lado da UI (spec §9.3) |
| Exibir imagem PNG do tutor (o campo base64 no JSON pode passar de 1 MB — spec §8.1) | LVGL com `CONFIG_LV_USE_LODEPNG=y` (`sdkconfig.defaults:56`); display RGB565 1024×600 | Orçamento de RAM: JSON bruto + base64 + PNG decodificado simultâneos (spec §8.1); o tamanho decodificado depende das dimensões/formato de cor. **O contrato não limita o tamanho da resposta** (o backend devolve os bytes gerados sem redimensionar; a evolução de contrato da spec §8 que resolveria isso não existe hoje): exigir medição real no hardware e tratamento seguro de resposta excessiva/falha de alocação |
| Base64 decode (MP3 e PNG chegam embutidos no JSON) | mbedTLS no ESP-IDF fornece base64 | Streaming/parse do JSON grande do turno sem duplicações desnecessárias |
| Telas de preparação/tutoria/failsafe + toque | LVGL 9 (`esp_lvgl_port ~2.8.0`) + GT911 5 pontos + fontes/emoji já configurados no CMake | UI inteira do PV é nova (máquina de fases da spec §9.2); assets de som locais devem ser copiados de `/home/deniellmed/licao_casa/frontend/public/sounds/` (spec §9.3) |
| Rede local com o backend (`http://IP:8000`) | Wi-Fi 6 via C6 (`esp_hosted`/`esp_wifi_remote`), provisão de Wi-Fi pelo botão BOOT (`.cc:429-439`) | Configurar/persistir endereço do backend (NVS própria do PV); health-check periódico de 10 s (spec §9.3) |
| Persistência mínima no dispositivo (rede + endereço do backend) | NVS disponível; partições `nvs` 840K + `nvsfactory` 200K (`partitions/v2/32m.csv`) | Chaves NVS novas do PV são API persistente — planejar nomes/migração desde o início (regra do repo) |
| Espaço p/ app e assets | OTA dupla de 4 MB + partição de assets de 16 MB (`partitions/v2/32m.csv`) | Sons locais do PV cabem com folga na partição de assets |

## 6. Referências externas

Waveshare (7B):
- Wiki: https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-7B
- Documentação: https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-7B
- Produto: https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7b.htm
  (SKU com câmera OV5647 inclusa: 32511)
- **Cópia local da documentação** (offline, usada nesta revisão):
  `/home/deniellmed/projetos/jarvis/documentos/ESP32-P4-7-inch-LCD-Display-TouchScreen-WIFI6-ESP32-C6.md`
  (confirma: módulo **ESP32-P4NRW32** com 32 MB PSRAM no encapsulamento +
  32 MB NOR flash; alto-falante PH2.0 "polarity independent"; porta
  MIPI-CSI onboard com ISP e encoder H.264/JPEG 1080p) e
  `/home/deniellmed/projetos/jarvis/documentos/Development-Environment-Setup-IDF.md`

Espressif (já citadas na spec, Apêndice B.3, mais as específicas daqui):
- Driver de câmera (esp32p4): https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/camera_driver.html
- Codec JPEG por hardware: https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/jpeg.html
- Cliente HTTP: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/esp_http_client.html
- Expansão Wi-Fi do P4: https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-guides/wifi-expansion.html
- Componente esp_audio_codec (decoders, incl. MP3): https://components.espressif.com/components/espressif/esp_audio_codec
- Componente esp_video: https://components.espressif.com/components/espressif/esp_video
- Painel EK79007: https://components.espressif.com/components/espressif/esp_lcd_ek79007
- Touch GT911: https://components.espressif.com/components/espressif/esp_lcd_touch_gt911

Documentação interna do repo: `docs/custom-board.md`,
`main/audio/README.md`, `docs/esp-idf-6-migration.md`.

## 7. Perguntas abertas de bring-up (exigem a placa em mãos / o proprietário)

1. **Revisão do chip P4 da unidade** → decide `7b` vs `7b-p4x`
   (ver seção 1). Como: log de boot ou `esptool.py chip_id`.
2. **O módulo de câmera OV5647 veio incluso e está conectado?** O módulo é
   opcional por SKU (o 32511 inclui; a versão standard não); o conector
   MIPI-CSI existe em todas. Sem o módulo não há preparação de lição nem
   turnos por foto.
3. **Alto-falante conectado ao PH2.0?** Não é onboard; sem ele não há voz
   do tutor. O conector é não polarizado (fonte: doc Waveshare); conferir
   impedância/potência recomendadas.
4. **Teste físico dos dois microfones + AEC** (captura física a 24 kHz;
   AFE/AEC processa a 16 kHz — a validação é física, não de build).
5. **Fonte de alimentação adequada** (display 7" + P4 + C6 + câmera juntos)
   e comportamento da chave ON/OFF durante uso prolongado.
6. Divergência menor a observar no bring-up: o código configura o sensor em
   RAW8 800×800@50fps (config.json) — confirmar se este modo atende a
   legibilidade de página exigida pela spec (B.4) ou se outro modo do
   OV5647 será necessário.
