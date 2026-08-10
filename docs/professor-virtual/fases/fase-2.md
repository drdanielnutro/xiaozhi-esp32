# Fase F2 — Câmera

> Regra de ouro: cada checkbox é marcado **no mesmo commit** que conclui a
> task — o estado nunca pode divergir do código. No encerramento da fase,
> atualizar a tabela "Status das fases" do `plano-firmware.md` no commit
> final, após a revisão independente do Codex.

## Objetivo

Câmera do Professor Virtual (plano F2): preview no LVGL (frame →
`LvglAllocatedImage` raw/RGB565), captura JPEG (encoder por hardware quando
disponível — `CONFIG_XIAOZHI_ENABLE_HARDWARE_JPEG_ENCODER` já default `y` no
P4), ajuste de qualidade/resolução para página A4 legível (dossiê §7 item 6).
Módulo novo `pv_camera.cc/.h` em `main/professor_virtual/` conforme a
arquitetura aprovada.

Fatos herdados da exploração de abertura:

- `EspVideo` (`main/boards/common/esp_video.cc`) já roda na 7B (V4L2 sobre
  `espressif/esp_video`, OV5647 RAW8 800×800@50fps via ISP), mas **não expõe
  o frame** (membro privado) e seu preview usa
  `LcdDisplay::SetPreviewImage`, que depende do `preview_image_` criado só em
  `SetupUI()` — nunca chamado pelo PV.
- A placa instancia `EspVideo` incondicionalmente
  (`esp32-p4-wifi6-touch-lcd.cc:458`); segundo open do device CSI não é
  viável — qualquer pipeline próprio exige gate no board file.
- JPEG: `image_to_jpeg_cb` (`main/display/lvgl_display/jpg/`) com encoder HW
  (`esp_driver_jpeg`) e fallback SW (`esp_new_jpeg`), quality 80 no uso atual.
- Formatos OV5647 disponíveis no driver: 800×640/800×800/800×1280 RAW8 50fps,
  1920×1080 RAW10 30fps, 1280×960 RAW10 binning 45fps.
- Contrato v1.1: turno envia `image_max_px=1280` (backend redimensiona o lado
  maior para 1280); alvo de tamanho por página: centenas de KB.

## Pronto quando

Foto de caderno legível salva/exibida; tamanho típico por página medido
(alvo: centenas de KB). Builds PV + 7b original verdes; testes host passam;
decisões registradas no decision-log. **Exige placa em mãos (flash — ação do
dono).** Conforme combinado com o proprietário (2026-08-04): bring-up e
diagnóstico com flat de 10 cm; a fase **só fecha validada na configuração de
produção** — flat de 50 cm ou, se houver ruído/artefato, adaptador CSI→HDMI
(flat de 40 cm existe como degrau intermediário).

## Pendências físicas (hardware)

- Degrau 1 — bring-up com flat de 10 cm: preview estável, captura JPEG,
  legibilidade avaliada, tamanho medido.
- Degrau 2 — configuração de produção (flat 50 cm; plano B: CSI→HDMI):
  repetir a validação; a fase só fecha aqui.

## Tasks

- [x] T1 — Decisões estruturais da F2 via Codex (acesso ao frame:
      extensão aditiva do `EspVideo` vs pipeline V4L2 próprio; estratégia de
      preview: contínuo vs snapshot, task e fps; formato/resolução do sensor
      para A4 nas variantes PV; qualidade JPEG e caminho de validação da
      legibilidade off-device) · pronto quando: decisões registradas no
      decision-log.
- [x] T2 — Infra de captura conforme T1 + `pv_camera.cc/.h` (captura de
      frame RGB565 + codificação JPEG com encoder HW/fallback SW; buffers em
      PSRAM com posse clara) · pronto quando: build PV verde e API do
      `pv_camera` consumível pelo `PvApp` sem tocar LVGL fora da task do LVGL.
- [x] T3 — Preview no LVGL em tela do PV (frame → imagem LVGL RGB565;
      start/stop ligado ao ciclo de vida da tela; sem vazamento nem
      uso de buffer após liberação) · pronto quando: build verde; preview
      integrado ao fluxo de telas do PV.
- [x] T4 — Captura JPEG + exibição da foto capturada + medição de tamanho +
      caminho de validação off-device da legibilidade · pronto quando: build
      verde; tamanho típico por página registrado nas notas (instrumentação
      pronta; o número em si sai na validação física da T6).
- [x] T5 — Regressão e medição: build
      `esp32-p4-wifi6-touch-lcd-7b-professor-virtual` + build
      `esp32-p4-wifi6-touch-lcd-7b` original + testes host
      (`python3 -m unittest discover -s scripts/tests`) + tamanho do binário
      vs OTA 4 MB · pronto quando: tudo verde e medição nas notas.
- [ ] T6 — Validação física (flash pelo dono): degrau 1 (flat 10 cm) e
      degrau 2 (produção: flat 50 cm ou CSI→HDMI); foto de caderno A4 legível
      · pronto quando: confirmado pelo proprietário na configuração de
      produção.

## Contexto de retomada

> Preencha ao interromper o trabalho no meio de uma task (fim de sessão,
> pausa pedida pelo proprietário, bloqueio temporário). Substitua todo o
> conteúdo por "(vazio)" no commit que concluir a task retomada. O hook de
> SessionStart injeta esta seção integralmente na próxima sessão.

- Task em andamento: T6 — validação física. Degrau 1 CONCLUÍDO
  (2026-08-04/05 flat 10 cm + A/B de 2026-08-10). Degrau 2 (produção) EM
  ESPERA pela F2B — não executar antes da decisão de rota.
- Último passo concluído: A/B físico de 2026-08-10 na estação Windows
  (fotos em `docs/professor-virtual/evidencias/f2b/`): a CSI 1280×960 é
  insuficiente para a extração (veredito do proprietário com base no
  piloto); a rota UVC virou necessária e a F2B foi aberta
  (`fase-2b.md`, decisão `F2B-RouteUvc` no decision-log). A rotação 180°
  da UI para o gabinete foi commitada com validação física pendente (o
  binário 46db44b flashado não a contém).
- Próximo passo exato: aguardar o resultado do spike UVC da F2B. O degrau
  2 (flat 50 cm ou CSI→HDMI) só se define depois: se a captura migrar para
  UVC, o flat CSI provavelmente vira só preview/enquadramento e o roteiro
  do degrau 2 muda de papel.
- Decisões já tomadas: F2-FrameAccess/Preview/SensorFormat/
  LegibilityValidation no decision-log (2026-08-04). Fallback físico do
  sensor: voltar a 800×800 por config (nunca 1920×1080 sem novo ensaio).
- Estado do worktree / armadilhas: Wi-Fi da placa "quarto_2.4GHz"; backend
  `http://192.168.15.9:8001`; porta serial no Mac
  `/dev/cu.usbmodem5B3E0883401` (no Windows: COM3 via interop); builds
  fora do export.sh oficial exigem `ESP_IDF_VERSION=6.0`; board dir do
  release.py é `waveshare/esp32-p4-wifi6-touch-lcd`; zip existente em
  `releases/` faz o release.py PULAR o build (exit 0).

## Notas da fase

- Estado Git inicial: `3fe622f` (worktree limpo, 2026-08-04).

### T6 degrau 1 — validação física parcial (2026-08-04/05, flat 10 cm)

- **Bug real encontrado e corrigido — cores verde/magenta:** o pipeline
  negocia **UYVY** (FOURCC 0x59565955, visto no log físico), e o
  `CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP=y` da 7B aplicava a troca de
  16 bits (correção exclusiva de RGB565) ao UYVY — produzindo YUYV com
  rótulo UYVY (croma/luma invertidos). Diagnóstico confirmado com o dump
  reconstruído (cena nítida, cores trocadas). Correção nos métodos aditivos:
  swap restrito a RGB565/RGB565X; YUV passa intacto (preview ainda perdeu
  uma cópia de 2,4 MB/frame — o intermediário de swap foi removido).
  **Cores confirmadas corretas pelo proprietário na placa (2026-08-04).**
  **Dívida upstream registrada:** o `Capture()` legado tem o mesmo bug
  latente na 7B (câmera nunca usada de verdade; mesmo padrão do toque 180°
  da F1) — recomendar issue no 78/xiaozhi-esp32 sem expor o PV.
- **Medições reais do degrau 1** (log físico): boot com sensor no modo novo
  (226 frames/5 s ≈ 45 fps de init); `CaptureRaw` 1280×960 UYVY 2.457.600 B;
  JPEG q85 = **202.103 bytes** (dentro do alvo de centenas de KB);
  codificação 555 ms; decodificação 395 ms; PSRAM livre 12,78 MB (maior
  bloco 10,2 MB); dump serial 24 s com CRC íntegro; extração no Mac OK na
  primeira tentativa real.
- Preview em 992×378 px úteis (escala 100/256). Fluidez percebida OK no
  degrau 1 (medição formal de fps pendente).
- **F2B criada (2026-08-05, decisão direta do proprietário):** risco de
  resolução insuficiente para extração completa do manuscrito (piloto
  Windows: resolução baixa → conteúdo incompleto SEM erro). Ver
  plano-firmware.md §F2B e decision-log `F2B-ExtractionResolution`. O teste
  de legibilidade do degrau atual prossegue como linha de base.
- Pendente no degrau 2 (flat 50 cm/produção): repetir preview + foto de
  página A4 real no suporte + exportação + legibilidade a 100%.

### T1 — Decisões estruturais (2026-08-04, Codex thread 019fcc8b)

- **F2-FrameAccess (Q1a):** `EspVideo` segue dono único do CSI; método
  opcional `CaptureRaw(FrameOut&)` (default `false`) em `camera.h`,
  implementado no `EspVideo`; sem `dynamic_cast` no PV. Lista fechada de
  arquivos compartilhados expandida com justificativa: `camera.h` +
  `esp_video.{h,cc}` (aditivo, upstream intacto).
- **F2-Preview (Q2c):** operação dedicada de preview no `EspVideo` — um
  DQBUF, conversão para buffer reutilizável (double-buffer, sem alloc/free
  de 2,4 MB por frame), QBUF imediato. 5 fps inicial (máx. 10 se medido
  estável); último-frame-vence; preview e captura JPEG mutuamente exclusivos
  na task de câmera do PV. `Capture()` legado fora do fluxo PV.
- **F2-SensorFormat (Q3b):** variantes PV passam a
  `CONFIG_CAMERA_OV5647_MIPI_RAW10_1280X960_BINNING_45FPS` com seleção
  explícita do default (RAW8 desabilitado explicitamente); upstream fica em
  800×800. JPEG q85 inicial. Fallback físico: voltar a 800×800.
- **F2-LegibilityValidation (Q4a+c):** dump base64 do JPEG codificado no
  console serial (marcadores + comprimento + CRC32, streaming), acionado só
  por gesto explícito e removível após a F2; conferência complementar na
  tela decodificando o mesmo JPEG. `/api/prepare/*` rejeitado para teste.

### T2 — Infra de captura + pv_camera (2026-08-04, subagente opus)

- `camera.h`: extensões opcionais aditivas com default `false` —
  `GetSensorResolution` (dimensiona buffers do chamador antes do 1º frame),
  `CaptureRaw(CameraRawFrame&)` (frame fresco full-res no formato nativo,
  posse do buffer PSRAM transferida ao chamador) e
  `AcquirePreviewFrame(CameraPreviewFrame&)` (um DQBUF → RGB565 LE no buffer
  reutilizável do chamador → QBUF imediato). `Esp32Camera`/`SscmaCamera`
  intocadas; sem `dynamic_cast` no PV.
- `esp_video.{h,cc}`: implementação dos três métodos; `Capture()`/`Explain()`
  intactos. Endianness sob a mesma Kconfig do legado (uma troca só; RGB565X
  vira RGB565 LE); clamp `MIN(bytesused, mmap length)` (o legado mistura os
  dois tamanhos); conversor esp_imgfx e intermediário de swap são membros
  reutilizáveis (zero alocação por frame); `capture_width_/height_` guardam a
  geometria real do driver; recusa educada durante o warm-up de 5 s do ISP.
- `pv_camera.{h,cc}`: task própria (`pv_camera`, 6 KB, prio 3) é a ÚNICA a
  falar com a câmera (serialização por construção); preview ~5 fps
  (`pdMS_TO_TICKS(200)`, atraso acumulado descartado) em double-buffer PSRAM
  alinhado a 64 (2× w·h·2, alocado uma vez); sincronização por três índices
  sob mutex leve (writing/ready/display), invalidação do ready ANTES de
  reusar o buffer, conversão fora do mutex; empréstimo
  `AcquireDisplayFrame/ReleaseDisplayFrame`; captura JPEG q85 coalescida
  (`atomic`), exclusão mútua com preview por rodarem na mesma task,
  resultado retirado com `TakeJpeg` (posse transferida; last-wins sem vazar).
  Sem câmera/sem extensões → no-op logado. Nunca toca LVGL/DSM/HTTP.
  *(Atualizado na revisão rodada 1: pilha 8 KB; `TakeJpeg` → `TakeCapture`,
  que entrega JPEG + RGB565 decodificado na própria task da câmera.)*
- `config.json`: só as 2 variantes PV → RAW8 800×800 `=n`, RAW10 1280×960
  binning `=y` + default fmt explícito (sdkconfig gerado confirma, índice 4).
- Validação: release.py PV verde; `pv_camera.cc.obj` no mapa; testes host
  release OK + PV 155 verificações OK; clang-format limpo nos arquivos novos
  (violações pré-existentes do esp_video não tocadas; hunks novos limpos);
  `xiaozhi.bin` 2.785.056 bytes (34% livres na OTA 4 MB).
- Pendências para T3/T4: tela LVGL consumindo o empréstimo de frame, fiação
  dos eventos no PvApp (handler roda na task da câmera e só posta), gesto do
  dump base64 e medições físicas (5 fps sustentado, PSRAM, tearing).

### T3 — Tela de câmera com preview LVGL (2026-08-04, subagente opus)

- `ui/pv_camera_screen.{h,cc}`: tela "Câmera" — área de preview flex_grow,
  `lv_image` com `LV_IMAGE_ALIGN_CONTAIN` (escala calculada só quando a
  geometria muda; nunca amplia), antialias off, rótulo "Preparando a
  câmera..." até o 1º frame, barra de ações 68 px com o MEIO reservado ao
  botão de captura da T4, Voltar + versão com `AttachConfigGesture` + badge.
  A tela é a DONA da `lv_image_dsc_t` e do empréstimo (`frame_borrowed_`) —
  um único dono elimina a janela LVGL-referencia-buffer-devolvido. Guarda de
  premissa: `#error` se o cache de imagem do LVGL for ligado
  (`LV_CACHE_DEF_SIZE>0`), pois o preview troca só o ponteiro da mesma dsc.
- Swap por frame TODO sob um único `DisplayLockGuard`: Detach (src nula) →
  `ReleaseDisplayFrame` → `AcquireDisplayFrame` → preencher dsc →
  `lv_image_set_src` + invalidate. A task do LVGL só desenha segurando o
  mesmo lock ⇒ nunca renderiza buffer já devolvido ao escritor.
- `pv_app`: membro `PvCamera` + `PvCameraScreen`; eventos
  `CameraScreenRequested/Closed/PreviewFrame`; coalescência com
  `atomic<bool> preview_frame_pending_` (máx. 1 slot da fila de 8 para o
  preview; flag limpa se o `xQueueSend` falhar, senão congela);
  `LeaveCameraScreen()` é o ponto ÚNICO de saída (Voltar, config por gesto,
  `LoadStatusScreen`, rota nova pós-hidratação); `ShowCameraScreen` recusa
  sem câmera ou com config aberta; `ReturnFromCameraScreen` →
  `PvRouteScreens::Reload()` (novo) ou tela de status.
- `pv_route_screens`: botão provisório "Ver a câmera" SÓ na rota Preparação
  (comentário: F3/F7 substituem); `Reload()` recarrega a rota corrente.
- Validação: build PV verde (objs novos confirmados); testes host release OK
  + PV 155 OK; clang-format limpo; `xiaozhi.bin` 2.794.304 bytes (+9.248;
  33% livres na OTA).
- Nota de ambiente: o board dir do release.py é
  `waveshare/esp32-p4-wifi6-touch-lcd` (sem o prefixo o script pula
  silenciosamente com exit 0).

### T4 — Captura, revisão da foto e dump de diagnóstico (2026-08-04, subagente opus)

- Tela de câmera ganhou modo REVISÃO: botão "Tirar foto" (meio da barra, cor
  de destaque) → `RequestCaptureJpeg` (coalescido) → "Processando..." →
  `JpegReady` → resultado retirado na task principal → preview PARADO →
  foto exibida no lugar do preview (decode `jpeg_to_image` com decoder SW do
  esp_new_jpeg já linkado — decoder HW desligado, nenhum Kconfig novo;
  *desde a revisão rodada 1 o decode roda na task da câmera, não na
  principal*). Barra em revisão: "Nova foto",
  "100%"/"Ajustar" (1:1 com pan pelo scroll nativo do LVGL, abre pelo centro)
  e "Exportar (diagnóstico)". Rótulo único de estado/medições
  ("1280×960 · N KB") — altura da área nunca muda.
- POSSE: `PvCameraScreen` é a dona única também do JPEG (`photo_`) e do
  RGB565 decodificado (`decoded_`); `EnterReview` toma a posse e libera em
  TODO caminho de falha; liberação garantida por `Hide()`/`ExitReview()`
  (todos os caminhos de saída passam por `LeaveCameraScreen`). Foto pronta
  com a tela já fechada → liberada no `HandleJpegReady`.
- `pv_photo_dump.{h,cc}` (PROVISÓRIO da F2, decisão F2-LegibilityValidation):
  task one-shot prio 1; CÓPIA de posse do JPEG em PSRAM (desacopla do ciclo
  da tela); formato `PV-JPEG-BEGIN len= crc32= w= h=` + base64 em linhas de
  120 (blocos de 2880 B, sem padding intermediário) + `PV-JPEG-END`;
  `printf`/`fwrite` (imune a log level); `vTaskDelay(1)` por bloco;
  coalescido; CRC32 = `esp_rom_crc32_le(0,...)` (= zlib). Só por toque
  explícito no botão — nunca automático.
- `scripts/pv/extract_jpeg_dump.py` (PROVISÓRIO): pega o ÚLTIMO bloco
  completo, tolera linhas de log intercaladas, confere len+CRC32, salva
  `foto_<w>x<h>.jpg`. Validado pelo orquestrador com CRC bom (rc=0) e
  corrompido (rc=1, mensagem clara).
- Medições por captura no `pv_camera`: tamanho, qualidade, tempo de
  codificação (ms), PSRAM livre e maior bloco livre.
- 7 eventos novos no PvApp (raros, sem coalescência — a coalescência real
  está no PvCamera/PvPhotoDump); handlers só na task principal.
- Validação: build PV verde (bin regravado); testes host release 8 OK + PV
  155 OK; clang-format limpo; `xiaozhi.bin` 2.879.696 bytes (+85.392; 31%
  livres na OTA). Nota: o subagente rodou com um aviso de segurança do
  harness (ação bloqueada); diff inteiro revisado pelo orquestrador —
  escopo, conteúdo e validações conferidos manualmente.
- Para a T6 física: tamanho típico/p95 do JPEG por página, tempos de
  encode/decode/dump reais, PSRAM com preview+foto+decodificado coexistindo,
  glifos `×`/`·`, pan 1:1, botões desabilitados durante processos.

### Revisão independente — rodada 1 (2026-08-04, Codex thread 019fcd21)

4×P1 + 2×P2, nenhum P0. Todas as alegações verificadas no código antes da
correção. **P1 corrigidos:**

1. Decode do JPEG (centenas de ms, ~2,4 MB) rodava na task principal do
   PvApp: movido para a task da câmera (`RunCaptureJpeg` codifica E
   decodifica; `TakeJpeg` virou `TakeCapture` com posse dos DOIS buffers;
   `EnterReview` agora só troca a tela sob o lock; pilha da task da câmera
   6→8 KB por prudência).
2. Conclusões (`JpegReady/Failed/ExportDone`) eram best-effort — fila cheia
   deixava a tela presa em "Processando...": `ReconcileCameraState()` no
   batimento de 1 s do loop compara ESTADO da tela com
   `capture_in_flight()`/`PvPhotoDump::busy()` e recupera pelos fluxos
   normais (eventos seguem sendo o caminho rápido).
3. `ShowCameraScreen` sem guarda de rota — pedido obsoleto pós-re-hidratação
   podia abrir a câmera por cima de Failsafe/Tutoria: agora exige
   `IsLoaded() && route()==Preparation`.
4. `CaptureRaw` aceitava frame curto do driver e o encoder lê w×h×bpp
   ignorando `src_len` (leitura fora da alocação): `ExpectedFrameBytes` por
   FOURCC + rejeição de `V4L2_BUF_FLAG_ERROR` + alocação/cópia do tamanho
   EXATO esperado; preview RGB565 recusa cópia parcial.

**P2 corrigidos/parciais:**

5. Dump copiava o JPEG inteiro (contra a decisão de streaming): agora é
   EMPRÉSTIMO com transferência condicional de posse (`TryHandOff` sob o
   mesmo mutex — ou o dump libera, ou o dono libera; nunca os dois).
6. Cobertura host da F2: `scripts/tests/test_pv_f2.py` (7 testes — script de
   extração com bloco íntegro/CRC corrompido/truncado/ruído/dois blocos +
   exclusividade RAW10 das variantes PV no config.json). **Cobertura C++ com
   fakes de Camera/fila: registrada e adiada para a F8** (fora do escopo da
   F2; exigiria harness novo de host para FreeRTOS/LVGL).

Validação pós-correção: build PV verde (`xiaozhi.bin` 2.881.488 bytes,
11:51); testes host **15 OK** (8+7); PV host_test 155 OK; clang-format limpo
(hunks novos).

### Revisão independente — rodada 2 (2026-08-04, Codex thread 019fcd45)

2×P1 + 1×P2 sobre as correções da rodada 1; decode/guarda de rota/frames
curtos/empréstimo do dump confirmados corretos. **Corrigidos:**

1. **P1** Reconciliação só rodava no timeout de 1 s — com o preview a 5 fps
   a fila nunca esvazia e ela podia nunca executar: `ReconcileCameraState()`
   agora roda a CADA volta do laço (depois de cada evento E no timeout).
2. **P1** Resultado órfão (tela fechada durante a captura + aviso perdido)
   vazava no slot e podia ser exibido como se fosse de captura posterior:
   (a) `RunCaptureJpeg` limpa o slot ANTES de iniciar captura nova;
   (b) a reconciliação drena e libera o órfão quando `!IsCapturing() &&
   !capture_in_flight()` (nunca drena resultado que a tela ainda espera).
3. **P2** `PvPhotoDump::Request` com falha de `xTaskCreate` após um
   `TryHandOff` concorrente vazaria o JPEG: o ramo de falha agora libera o
   buffer se `g_owns` já tiver sido transferido.

Validação pós-correção: build PV verde (`xiaozhi.bin` 2.881.808 bytes);
testes host 15 OK; PV host_test 155 OK; clang-format limpo.

### Revisão independente — rodada final de verificação (2026-08-04, Codex thread 019fcd66)

Reconciliação por volta e posse no ramo de falha do dump: **confirmadas
corretas**. 1×P1 residual encontrado e corrigido: os handlers de conclusão
aceitavam `IsActive()` — uma tela fechada durante a captura e REABERTA antes
da conclusão receberia a foto antiga (ou a falha antiga) numa sessão nova.
`HandleJpegReady`/`HandleJpegFailed` agora exigem `IsCapturing()`; resultado
sem tela esperando é liberado no ato (consistente com a drenagem da
reconciliação — `TakeCapture` move e esvazia, sem dupla liberação).
Confirmação da correção pedida por `codex-reply` na mesma thread da rodada.

Validação: build PV verde (`xiaozhi.bin` 2.881.792 bytes); testes host 15
OK; PV host_test 155 OK; clang-format limpo.

### T5 — Regressão e medição (2026-08-04)

- `release.py` PV verde:
  `releases/v2.4.0_waveshare-esp32-p4-wifi6-touch-lcd-7b-professor-virtual.zip`.
- `release.py` 7b original verde:
  `releases/v2.4.0_waveshare-esp32-p4-wifi6-touch-lcd-7b.zip` (o upstream
  continua em 800×800 RAW8 — a troca de sensor é exclusiva das variantes PV).
- Testes host: release 8 OK; PV `host_test/run.sh` 155 verificações, 0 falhas.
- `xiaozhi.bin` da variante PV: **2.879.696 bytes** (0x2bf0d0) — 31% livres
  na partição de app de 4 MB. Delta da fase: +100.672 bytes sobre a F1
  (2.779.024).
