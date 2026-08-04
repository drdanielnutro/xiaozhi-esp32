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
- [ ] T5 — Regressão e medição: build
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

(vazio)

## Notas da fase

- Estado Git inicial: `3fe622f` (worktree limpo, 2026-08-04).

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
  `JpegReady` → `TakeJpeg` na task principal → preview PARADO → JPEG
  decodificado FORA do lock do display (`jpeg_to_image`, decoder SW do
  esp_new_jpeg já linkado — decoder HW desligado, nenhum Kconfig novo) →
  foto exibida no lugar do preview. Barra em revisão: "Nova foto",
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
