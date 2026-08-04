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
- [ ] T2 — Infra de captura conforme T1 + `pv_camera.cc/.h` (captura de
      frame RGB565 + codificação JPEG com encoder HW/fallback SW; buffers em
      PSRAM com posse clara) · pronto quando: build PV verde e API do
      `pv_camera` consumível pelo `PvApp` sem tocar LVGL fora da task do LVGL.
- [ ] T3 — Preview no LVGL em tela do PV (frame → `LvglAllocatedImage`
      RGB565; start/stop ligado ao ciclo de vida da tela; sem vazamento nem
      uso de buffer após liberação) · pronto quando: build verde; preview
      integrado ao fluxo de telas do PV.
- [ ] T4 — Captura JPEG + exibição da foto capturada + medição de tamanho +
      caminho de validação off-device da legibilidade · pronto quando: build
      verde; tamanho típico por página registrado nas notas.
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
