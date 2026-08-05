# Pesquisa profunda: captura acima de Full HD no ESP32-P4 para extração de manuscrito

> Prompt preparado em 2026-08-05 para IAs de pesquisa (deep research). Objetivo:
> reunir evidência externa sobre o que funciona e o que não funciona antes de
> executar a fase F2B do firmware Professor Virtual.

## Contexto do projeto (fatos — não pesquisar, usar como premissas)

- Dispositivo embarcado na placa **Waveshare ESP32-P4-WIFI6-Touch-LCD-7B**
  (SoC ESP32-P4, 32 MB PSRAM, display MIPI-DSI 1024×600, USB 2.0 High-Speed
  OTG, MIPI-CSI 2 lanes com conector de 15 pinos padrão Raspberry Pi).
- Firmware derivado do projeto open-source **xiaozhi-esp32**
  (`github.com/78/xiaozhi-esp32`), ESP-IDF v6.0.2. Pilha de câmera:
  componente `espressif/esp_video` 2.x (API estilo V4L2) +
  `espressif/esp_cam_sensor` (driver do sensor) + ISP do ESP32-P4 +
  encoder/decoder JPEG por hardware do P4.
- Câmera atual: **OV5647** (5 MP, saída RAW8/RAW10) via MIPI-CSI. Modos
  expostos pelo driver `esp_cam_sensor`: 800×640, 800×800 e 800×1280
  RAW8@50fps; **1920×1080 RAW10@30fps (crop central do sensor)**;
  **1280×960 RAW10 binning 2×2@45fps (campo de visão cheio)** — em uso hoje.
- Caso de uso: fotografar **página A4 de caderno com manuscrito infantil** e
  enviar JPEG a um backend com LLM de visão que extrai o conteúdo.
  Evidência empírica nossa: em resoluções baixas a extração **não dá erro —
  devolve conteúdo incompleto**, sobretudo na escrita à mão. Queremos a
  maior resolução real possível na FOTO PARADA; **fps é irrelevante**
  (≥1 fps já atende; preview de enquadramento pode ficar em resolução menor).
- Premissa a **VERIFICAR** logo no início: acreditamos que o **ISP do
  ESP32-P4 tem entrada máxima de 1920×1080** (documentação do Camera
  Controller Driver do ESP-IDF), o que limitaria qualquer sensor RAW a
  1080p pelo CSI, tornando o modo 2592×1944 da OV5647 inutilizável.

## Perguntas de pesquisa (em ordem de importância)

1. **Confirmação do limite do ISP:** a entrada do ISP do ESP32-P4 é mesmo
   limitada a 1920×1080? Citar Technical Reference Manual e/ou documentação
   oficial. Existe contorno oficial ou anunciado (ex.: processamento em
   tiles/faixas, dual-pass)?
2. **CSI sem ISP (bypass):** o hardware/`esp_video` permite receber frames
   do controlador MIPI-CSI **sem passar pelo ISP**, acima de 1080p — por
   exemplo RAW10 2592×1944 direto para a PSRAM, com demosaic por SOFTWARE
   apenas para a foto parada (tempo de processamento não importa)? Qual é o
   limite do controlador CSI em si (separado do ISP)? Alguém já demonstrou
   isso no P4?
3. **Sensor MIPI com ISP interno:** existe sensor/módulo MIPI-CSI compatível
   (ou adaptável) com `esp_video` que entregue **YUV422 ou JPEG já
   processado acima de 1080p**, dispensando o ISP do P4? O `esp_video`
   suporta esse caminho hoje? Exemplos concretos de hardware + configuração.
4. **Rota UVC (câmera USB) no P4:** estado real do suporte a câmeras UVC no
   `esp_video` (`CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE`) e no
   `esp-iot-solution` (`usb_host_uvc`): resoluções máximas **comprovadas**,
   MJPEG vs YUY2 na banda do USB High-Speed (480 Mbps) na prática, câmeras
   que comprovadamente funcionam (webcams comuns e câmeras industriais),
   armadilhas conhecidas (alimentação da porta, enumeração, hubs, latência,
   tamanho de buffer). Relatos com câmeras industriais são especialmente
   valiosos.
5. **JPEG de alta resolução no P4:** o decoder JPEG por hardware do P4
   decodifica frames de 5 MP (ex.: 2592×1944)? Quais os limites documentados
   do codec (encode e decode)? (Para o nosso fluxo, o upload pode usar o
   MJPEG da câmera direto, sem reencode; o decode importa só para
   preview/conferência na tela.)
6. **Relatos de comunidade de >1080p no ESP32-P4 por QUALQUER método:** o
   que exatamente fizeram (pilha, sensor, configuração), o que funcionou e o
   que **não** funcionou — falhas documentadas valem tanto quanto sucessos.

## Fontes a consultar (obrigatórias — mas NÃO se limite a elas)

- Documentação oficial ESP-IDF para ESP32-P4: Camera Controller Driver
  (`esp_driver_cam`/`isp`), JPEG codec, USB Host; ESP32-P4 Technical
  Reference Manual (capítulos MIPI-CSI e ISP).
- Repositórios e issues/discussions: `espressif/esp-video-components`
  (componente `esp_video`), `espressif/esp_cam_sensor`,
  `espressif/esp-iot-solution` (UVC host), **`78/xiaozhi-esp32`**
  (issues/discussions sobre câmera e ESP32-P4).
- Fóruns e comunidades: fórum oficial Espressif (esp32.com), GitHub
  Discussions do xiaozhi-esp32, wiki e fórum da Waveshare para a
  ESP32-P4-WIFI6-Touch-LCD-7B, Reddit r/esp32.
- Além dessas: blogs, vídeos, datasheets (OV5647 e sensores alternativos) e
  projetos de terceiros que demonstrem captura acima de 1080p no P4.

## Formato da resposta esperado

- Para **cada** pergunta: veredito (**funciona / não funciona / incerto**),
  evidência com **links**, e nível de confiança (alto/médio/baixo).
- Tabela final consolidada: "comprovadamente funciona" vs "comprovadamente
  não funciona" vs "ninguém testou/registrou".
- Recomendação prática ordenada para o nosso caso de uso (foto parada de
  página A4 para extração de manuscrito, ≥1 fps, preview pode ser menor),
  incluindo hardware específico se aplicável.
