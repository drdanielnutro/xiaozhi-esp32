

---

Você é um pesquisador técnico sênior de sistemas embarcados. Preciso de uma
pesquisa profunda, com fontes citadas e verificáveis, sobre um problema
específico de USB host no ESP32-P4 — e, principalmente, sobre **soluções que
já tenham sido implementadas com sucesso** por terceiros. Rigor: distinga
sempre fato comprovado (com link) de inferência sua; quando a evidência for
fraca, diga explicitamente.

## Contexto do produto (para calibrar as recomendações)

Dispositivo educacional embarcado (ESP32-P4, placa Waveshare
ESP32-P4-WIFI6-Touch-LCD-7B) que precisa capturar UMA foto JPEG de alta
resolução (mínimo 2592×1944, ideal 3264×2448) de uma página de caderno por
vez, com uma webcam USB (UVC, MJPEG), mais um preview de baixa resolução
(~800×600, ~5 fps). NÃO é streaming de vídeo contínuo em alta resolução. A
webcam atual é uma NE-HD362 (sensor Sony IMX362 12 MP, MJPEG interno,
autofoco, USB 2.0 high-speed, endpoint isócrono high-bandwidth). Trocar de
câmera é aceitável; trocar de plataforma é o último recurso.

## O problema, com os fatos que JÁ estabelecemos em bancada (não re-derive)

1. Stack: ESP-IDF 6.0.2 + componente `espressif/usb_host_uvc` (esp-usb) +
   componente `espressif/usb` (host stack) no ESP32-P4 (USB-OTG HS, DWC2).
2. A câmera enumera perfeitamente; Probe/Commit UVC funciona; o
   `dwMaxPayloadTransferSize` committed é **3072 = 3×1024** (high-bandwidth
   ISOC, 3 transações/microframe).
3. **A/B decisivo (mesmo dia, mesma placa/câmera/cabo)**:
   - `usb_host_uvc` **2.4.2** (que tem um bug oficial que capa URBs
     isócronas em 1 pacote): canal VIVO — dezenas de milhares de callbacks
     ISOC, frames MJPEG chegando com o bit ERR do payload header na maioria
     (raro frame limpo escapa — 1 foto íntegra 800×600 já foi capturada
     assim).
   - `usb_host_uvc` **2.5.1** (URBs corrigidas para 4 pacotes/URB): silêncio
     ABSOLUTO — zero callbacks, zero erros, em 3 execuções limpas,
     inclusive falando direto com a API pública do driver, sem camadas
     intermediárias.
4. Issue pública que bate com o quadro: **espressif/esp-usb #538**
   (IEC-580, aberta em 2026-08-11) — no P4, `MAX_MPS_IN=4096` hard-coded
   seleciona alt setting high-bandwidth; o autor mediu `missed EoF`
   contínuo e mostrou que:
   - a regressão de seleção de alt entrou na **2.4.2 via PR #424** (antes,
     o seletor preferia alts de transação única — MULT=0);
   - `usb_dwc_ll_hcchar_init()` **nunca programa o campo HCCHAR.MC**
     (multi-count) do canal host DWC2 — o hardware fica em 1
     transação/microframe enquanto o device manda 3;
   - a câmera dele exige 3072 B/µframe em TODA resolução
     (`dwMaxPayloadTransferSize` é constante do device, read-only pela
     spec UVC 1.5, Tabela 4-75), e alts menores levam o device a STALL
     (§4.3.1.1.2.1) / `ESP_ERR_INVALID_STATE`.

## O que pesquisar (em ordem de valor)

### A. Correções e workarounds JÁ IMPLEMENTADOS com sucesso

1. Existe fix oficial ou em andamento para o **HCCHAR.MC não programado**
   no host DWC2 do ESP-IDF/esp-usb (P4 ou S3)? Procurar: commits/PRs em
   `espressif/esp-idf` (hal `usb_dwc_ll.h`, `hcd_dwc.c`), em
   `espressif/esp-usb`, tickets IEC/AEGHB citados em release notes,
   roadmap público. Versões novas de `usb_host_uvc` (>2.5.1), `usb`
   (>1.4.x) ou IDF (6.0.x/6.1) que mencionem high-bandwidth ISOC.
2. Alguém já fez **patch/fork funcional** programando o MC no DWC2 do
   ESP32 (P4/S3)? Procurar forks no GitHub, gists, posts de fórum
   (esp32.com, Reddit r/esp32, blogs chineses — o ecossistema P4 é forte
   na China) com evidência de funcionamento.
3. O driver Linux `dwc2` programa multi-count para ISOC high-bandwidth
   (campo MC do HCCHAR / "EC_MC")? Como? Isso serve de referência de
   implementação — citar arquivo/linha do kernel.
4. Workaround de **1 pacote por URB** (equivalente ao comportamento da
   2.4.2): há relato de uso estável em produção? Qual o custo real (CPU,
   perda de microframes, frames com ERR)? O bit ERR nos payload headers
   que observamos é consequência conhecida do host perder 2 de 3
   transações do microframe?
5. O componente alternativo `espressif/usb_stream` (esp-iot-solution) —
   implementação UVC independente — programa o canal DWC2 de forma
   diferente? Alguém streama câmera high-bandwidth ISOC com ele no
   P4/S3? Status de manutenção dele em 2026.

### B. Contornos por hardware/dispositivo

6. **Câmeras UVC com streaming BULK** (em vez de isócrono): o transporte
   bulk não tem o problema de multi-count. Listar câmeras UVC USB 2.0 de
   8–12 MP, MJPEG, autofoco, que usem bulk streaming (muitas câmeras de
   "document scanner"/industriais usam). Modelos concretos compráveis
   (IMX179/IMX258/IMX335 etc., módulos ELP/Arducam/InnoMaker/HBV), com
   evidência de que são bulk (descritor, datasheet, relato).
7. Câmeras UVC que anunciam e ACEITAM alts de transação única (MULT=0,
   ≤1024 B/µframe) em resoluções altas com MJPEG — o quadro do #538
   mostra que isso depende do firmware da câmera. Como identificar antes
   de comprar (lsusb -v no Linux: wMaxPacketSize dos alts).
8. Um **hub USB 2.0 intermediário** muda algo para ISOC high-bandwidth
   host↔device? (Resposta esperada: não para HS nativo — confirmar; e
   notar que hub exige `CONFIG_USB_HOST_HUBS_SUPPORTED`.)

### C. Enquadramento

9. Dado o produto (foto única de alta resolução + preview leve), qual
   caminho tem MENOR risco/tempo: (i) esperar/aplicar fix do MC, (ii)
   regime de 1 pacote/URB com câmera atual, (iii) trocar para câmera
   bulk, (iv) trocar de plataforma (SBC Linux — o backend é HTTP e fica
   intacto)? Recomende uma ordem, com critérios de decisão objetivos.

## Formato da resposta

- Sumário executivo (≤15 linhas) com o veredito prático.
- Seções A/B/C com fontes por item (links diretos; commit hashes quando
  houver).
- Tabela final: solução → evidência de sucesso → esforço → risco.
- Liste explicitamente o que você NÃO encontrou (ausência de evidência é
  informação valiosa aqui).
