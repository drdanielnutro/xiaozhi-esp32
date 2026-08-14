# Prompt de pesquisa — OV5647 5 MP no ESP32-P4 (captura de foto para OCR)

> Prompt para deep research por outra IA. Data de corte: 13 de agosto de 2026.

---

Você é um pesquisador técnico sênior especializado em ESP32-P4, MIPI-CSI,
drivers de sensor de imagem e no ecossistema ESP-IDF/esp-video da Espressif.
Uma pesquisa anterior já triou as hipóteses de alto nível; **não repita essa
triagem**. Sua missão agora é encontrar **evidência primária e verificável**
para uma única rota já escolhida como candidata, e mapear com precisão o que
funciona, o que não funciona e o que ninguém nunca testou.

## Disciplina de evidência (obrigatória)

Este é o requisito mais importante do trabalho. Uma pesquisa anterior sobre o
mesmo hardware falhou exatamente por afirmar coisas plausíveis sem fonte.

- Marque cada afirmação técnica como **[VERIFICADO]** (com fonte primária
  citada), **[INFERÊNCIA]** (com o raciocínio e o grau de confiança) ou
  **[NÃO ENCONTRADO]** (diga o que procurou e onde).
- Para afirmações sobre código, cite **repositório + arquivo + linha + versão
  ou hash do commit** (ex.: `esp-idf v6.0.2,
  components/hal/esp32p4/include/hal/isp_ll.h:123`). Para hardware, cite o
  capítulo/seção do Technical Reference Manual (TRM) do ESP32-P4 ou o
  datasheet.
- Hierarquia de fontes: TRM/datasheet > código-fonte do ESP-IDF e dos
  componentes > documentação oficial Espressif > issues/PRs no GitHub >
  fóruns. Uma issue relata sintoma; só o código ou o TRM comprovam causa.
- É melhor um "[NÃO ENCONTRADO] — procurei em X, Y, Z" honesto do que uma
  afirmação plausível sem fonte. Lacunas explícitas viram testes de bancada;
  afirmações falsas viram semanas perdidas.

## Contexto fixo (não re-derive, não questione)

- Placa: Waveshare ESP32-P4-WIFI6-Touch-LCD-7B; ESP32-P4 rev. 1.3; 32 MB de
  PSRAM; MIPI-CSI de 2 lanes; sensor OmniVision OV5647 (5 MP, 2592×1944,
  RAW8/RAW10, sem scaler fracionário interno — só crop, skipping e binning).
- Software: ESP-IDF 6.0.2 preferido; componentes `esp_video`,
  `esp_cam_sensor`, `esp_ipa` (baseline testada: 2.0.1/2.0.1/1.3.1; versões
  mais novas podem ser adotadas).
- Bancada estável hoje: modo 1280×960 RAW10 full-FOV (binning 2×2) → ISP →
  UYVY → JPEG por **software** (~550 ms). O caminho de JPEG por **hardware**
  rejeitou entrada UYVY nesse teste. Densidade obtida: ~3,5–4 px/mm sobre a
  página; insuficiente. Meta: ≥ 6,2 px/mm sobre página A4 inteira.
- Produto: preview de enquadramento ~5 fps em resolução baixa + **uma** foto
  de alta qualidade por vez; latência de alguns segundos é aceitável; o
  backend só aceita JPEG por HTTP (contrato imutável).
- Decisão já tomada (não reabra): a rota é capturar a matriz **completa**
  do OV5647 (2592×1944, RAW8 ou RAW10) no ESP32-P4, contornando o limite do
  ISP, e converter localmente para JPEG. Alternativas (elevar a câmera com o
  modo 1080p, modos full-FOV customizados ≤1920 de largura) já foram
  descartadas ou ficam como plano B fora do escopo desta pesquisa.

## Bloco A — Limites reais do caminho CSI → memória sem ISP (o elo crítico)

A pesquisa anterior afirmou, sem fonte primária, que o receptor MIPI-CSI do
P4 não tem limite de dimensão e que o teto 1920×1080 é só do ISP. Comprove ou
refute com precisão de registrador:

1. No ESP-IDF (componentes `esp_cam_ctlr` / `esp_cam_ctlr_csi`, `hal`,
   `soc`), quais são os limites máximos de largura e altura aceitos pelo
   controlador CSI e pelo bridge (`csi_brg`)? Procure macros, campos de
   registrador (largura em bits dos contadores de linha/coluna), asserts e
   validações no driver. Cite arquivo:linha.
2. A flag `bypass_isp` (ou equivalente) existe em qual estrutura/driver do
   IDF 6.0.2? O que ela faz exatamente no código? O ISP fica fora do caminho
   de dados ou continua no caminho em modo "passthrough" (e nesse caso os
   limites de line buffer ainda se aplicam)?
3. O `esp_video` (versões atuais, ≥2.1) expõe um device V4L2 que entrega RAW
   direto do CSI para a aplicação, sem ISP? Qual Kconfig/formatos
   (`V4L2_PIX_FMT_SBGGR10` etc.)? Há exemplo oficial capturando RAW no P4?
4. O TRM do ESP32-P4 documenta limites do CSI host controller
   (Synopsys DWC MIPI CSI-2?) e do 2D-DMA (DW-GDMA) relevantes: tamanho
   máximo de transferência, largura de linha, alinhamento? Cite capítulo.
5. A issue `espressif/esp-video-components#38` (AEGHB-1215, "OV5647 having
   noise in the image with higher resolution") indica que alguém já rodou o
   OV5647 acima de 1080p no P4. Leia a issue inteira: qual resolução, qual
   caminho (ISP ou bypass), qual era o ruído, houve correção/commit?
6. Existe qualquer projeto público (Espressif examples, esp-iot-solution,
   esp-who, Tasmota, M5Stack, fórum esp32.com) que capture um still de
   2592×1944 — ou qualquer coisa >1920 de largura — no ESP32-P4 via CSI?
   Distinga: captura RAW comprovada com imagem publicada vs. tentativa
   relatada vs. menção teórica.

## Bloco B — Modo 2592×1944 no `esp_cam_sensor`

1. As versões atuais do `esp_cam_sensor` (verifique a mais recente no
   Component Registry, ≥2.3.x) já incluem um modo OV5647 2592×1944 (RAW8 ou
   RAW10)? Liste TODOS os modos da tabela atual de
   `sensors/ov5647/ov5647_settings.h` (ou equivalente) com resolução,
   formato, fps e binning, citando o arquivo na versão exata.
2. Se não existe, qual tabela de registradores validada serve de referência?
   Verifique especificamente o driver mainline do Linux
   (`drivers/media/i2c/ov5647.c`) — confirme se existe o modo
   `2592x1944_10bpp` (~15 fps, 2 lanes), cite os valores-chave (PLL,
   HTS/VTS, janela 0x3800–0x3807, binning 0x3820/0x3821) e a licença.
   Compare com o driver do Raspberry Pi (libcamera/rpicam) se divergirem.
3. Com 2 lanes MIPI, qual link frequency/pixel clock o modo 5 MP exige e o
   D-PHY do P4 comporta (limite por lane do P4: cite TRM/datasheet)? É
   possível reduzir para 5–10 fps via VTS/PLL para aliviar banda e memória,
   e há precedente de tabela assim?
4. Mecânica de integração: o `esp_cam_sensor` aceita adicionar um modo
   customizado sem fork (API pública, weak symbols, Kconfig), ou exige
   patch/fork do componente? Como outros projetos adicionaram modos?
5. RAW8 vs RAW10 no OV5647: o sensor suporta saída RAW8 nativa em full
   frame? Quanto se perde de faixa dinâmica na prática para documento em
   ambiente interno? (RAW8 pouparia 20% de memória e simplificaria o
   demosaic — vale a pena?)

## Bloco C — Do RAW ao JPEG sem quebrar o contrato

1. **Encoder JPEG de hardware do P4**: liste os formatos de entrada
   EXATAMENTE como o driver (`esp_driver_jpeg`, IDF 6.0.2) e o TRM os
   definem (enums `jpeg_enc_input_format_t` ou equivalente): RGB888?
   RGB565? YUV422 — em qual ordenação de bytes (YUYV? UYVY?)? GRAY? Qual a
   resolução máxima? Isso explica a rejeição de UYVY observada na nossa
   bancada — era formato errado, ordenação errada ou limitação real?
2. **ISP offline (memória → ISP → memória)**: o driver de ISP do IDF aceita
   fonte de dados `DWGDMA`/memória (procure
   `ISP_INPUT_DATA_SOURCE_*` em `isp_types.h`)? Se sim: é viável processar o
   RAW 5 MP da PSRAM em **faixas/tiles ≤1920×1080 por passada** (ex.: 4
   tiles de 1296×972) e costurar o resultado, obtendo demosaic por hardware
   sem estourar o limite? Há exemplo, teste ou issue sobre esse modo M2M?
   Quais armadilhas (bordas de tile, LSC/AWB por tile, alinhamento)?
3. **Demosaic por software** (fallback): benchmarks reais de demosaic
   bilinear em RISC-V dual-core 400 MHz ou classe similar (ciclos/pixel,
   uso das extensões SIMD/PIE do P4). Estimativa fundamentada para 5 MP:
   segundos, não adjetivos. Alguma biblioteca C embarcada pronta e
   licenciável?
4. **Atalho tons de cinza**: para OCR, cor é dispensável. Avalie extrair
   luminância direto do Bayer em resolução plena (interpolando só o plano
   verde + correção) e codificar JPEG grayscale. O encoder de hardware
   aceita GRAY? O JPEG por software (qual biblioteca o `esp_video`/exemplos
   usam?) aceita? Quanto tempo/memória isso pouparia vs. demosaic completo?
5. Sem ISP não há AWB/AE/LSC do pipeline. O OV5647 tem AEC/AGC internos
   utilizáveis em RAW (registradores 0x3503 etc.)? Para foto de documento
   com iluminação própria do dispositivo, o que a prática (Linux/RPi, que
   sempre usam ISP externo) diz sobre qualidade de RAW com AEC on-sensor?
   Alguma correção mínima de software (ganho global, gamma) é suficiente
   para OCR?

## Bloco D — Convivência com o preview e ciclo de operação

1. Alternância dinâmica de modo em runtime: parar o stream 1280×960 (preview
   via ISP), reprogramar o sensor para 2592×1944 bypass, capturar 1 frame,
   voltar ao preview. O `esp_video`/V4L2 do P4 suporta trocar formato com
   `S_FMT` sem reboot do pipeline? Há issues sobre reconfiguração dinâmica
   sensor+CSI+ISP (deadlocks, DMA em voo, ordem de teardown)? Quanto tempo
   leva um mode switch do OV5647 (I²C + estabilização de AEC)?
2. Captura de frame único: como garantir exatamente 1 frame válido (descartar
   os primeiros N frames pós-switch enquanto AEC converge — qual N a prática
   recomenda)? O driver CSI permite capturar e parar, sem streaming contínuo?
3. Memória: RAW10 packed 2592×1944 ≈ 6,3 MB; unpacked 16-bit ≈ 10,1 MB;
   intermediário (gray 8-bit ≈ 5 MB ou RGB888 ≈ 15 MB) + JPEG de saída.
   Confirme os requisitos de alinhamento de buffer do driver CSI/DMA
   (cache line de 64 B? `esp_dma_capable`?) e se 1 único buffer de frame é
   aceito pelo driver (mínimo de buffers exigido pelo `esp_video`/CSI).

## Bloco E — Riscos conhecidos e sinais de campo

1. Varra issues abertas/fechadas de `esp-video-components`, `esp-idf` e
   fórum esp32.com sobre: OV5647 no P4 acima de 1080p, CSI bypass, captura
   RAW, JPEG encoder com YUV, ISP com fonte de memória. Liste cada achado
   com número, status e o que comprova.
2. O ruído da issue #38 (AEGHB-1215) é do sensor (ganho/PLL da tabela) ou do
   caminho CSI/DMA? Isso afeta a rota bypass?
3. Errata/limitações da revisão de silício do P4 (rev. 1.3) relevantes a
   CSI/DMA/PSRAM sob transferências grandes?

## Formato da resposta

1. **Resposta executiva** (≤1 página): a rota 2592×1944-bypass é viável no
   P4 com evidência primária? Qual sub-rota de conversão RAW→JPEG tem mais
   evidência (ISP offline em tiles / demosaic SW + JPEG HW / gray + JPEG)?
   O que segue sem prova e só a bancada resolve?
2. **Blocos A–E respondidos ponto a ponto**, com a marcação
   [VERIFICADO]/[INFERÊNCIA]/[NÃO ENCONTRADO] em cada item.
3. **Plano de bancada ordenado** (T0, T1, T2…): do teste mais barato e mais
   discriminante para o mais caro, cada um com critério objetivo de
   PASS/FAIL e o que cada resultado decide.
4. **Tabela de fontes**: URL + o que comprova + confiabilidade.

Não use adjetivos no lugar de números. Não recomende trocar de hardware.
Não proponha mudar o contrato do backend (JPEG por HTTP é fixo).
