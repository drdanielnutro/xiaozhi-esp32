# Fase F2B — Resolução de extração (rota UVC)

> Regra de ouro: cada checkbox é marcado **no mesmo commit** que conclui a
> task — o estado nunca pode divergir do código. No encerramento da fase,
> atualizar a tabela "Status das fases" do `plano-firmware.md` no commit
> final, após a revisão independente do Codex.

## Objetivo

Garantir resolução de captura suficiente para a extração COMPLETA do
manuscrito antes da F3. A validação física de 2026-08-10 (A/B em
`../evidencias/f2b/`) provou que a CSI OV5647 é insuficiente em qualidade
para a função central e que o teto é de silício (ISP do P4 ≤1920×1080).
Rota desta fase: **câmera UVC NE-HD362** (Sony IMX362 12 MP, MJPEG interno,
autofoco, validada no piloto com o MESMO backend) na porta USB-A OTG HS da
7B, decidida por um **spike de bancada** (frame único MJPEG em escada de
resoluções). Objetivo do produto (decisão do proprietário, 2026-08-10): um
dispositivo que rode o backend e ajude a criança — se a 7B servir, melhor;
senão, trocar hardware sem apego (árvore de fallback nas Notas).

## Pronto quando

Spike responde a viabilidade: mínimo = enumeração estável + frame MJPEG
único válido (SOI/EOI, CRC do dump, decodifica, cena correta) em 1920×1080
E em ≥1 degrau ≥2592×1944, com página A4 de manuscrito legível a 100%;
ideal = 3264×2448 (ou 4000×3000) nítido com AF convergido, escada 3×
estável. Decisão de rota registrada (decision-log + plano-firmware) e, se
PASS, desenho da integração definido. Extração completa comprovada com o
backend real (ou legibilidade a 100% aprovada pelo proprietário como proxy
até a F3).

## Pendências físicas (hardware)

- Spike de bancada no Mac: flash da variante spike (ação do dono), câmera
  NE-HD362 na porta USB-A da 7B, escada de resoluções com dump serial.
- Se PASS: A/B de extração com o backend real (mesma página, CSI vs UVC).

## Tasks

- [ ] T1 — Inspeção da API real do esp_video UVC pós-download (baixar
      managed_components no Mac; ler `espressif__esp_video`: campos de
      `esp_video_init_usb_uvc_config_t`, Kconfigs UVC do componente, FOURCC
      exposto pelo device UVC — JPEG vs MJPG, ENUM_FRAMESIZES
      implementado?, alocação de buffer vs `dwMaxVideoFrameBufSize`
      (interna vs PSRAM — se interna, escalar na T2), DQBUF
      bloqueante/O_NONBLOCK, V4L2_CID_FOCUS_AUTO) · pronto quando: fatos
      registrados nas Notas e lista final de `sdkconfig_append` fechada.
- [ ] T2 — Decisões estruturais via Codex (ratificar arquitetura
      "A-sem-board" das Notas; ciclo open→close por degrau [rec.: sim];
      REQBUFS count [rec.: 2]; baud do console [rec.: 115200]; degrau
      opcional 4000×3000 [rec.: sim, nunca bloqueante]; timeout e frames
      de descarte p/ AF [rec.: ~10 frames/3 s]) · pronto quando: decisões
      no decision-log.
- [ ] T3 — Implementação: `main/professor_virtual/pv_uvc_spike.{h,cc}`,
      3º ramo no `main.cc`, `CONFIG_PV_UVC_SPIKE` no Kconfig.projbuild,
      variante `esp32-p4-wifi6-touch-lcd-7b-professor-virtual-uvc-spike`
      no config.json, flag `--all` no `extract_jpeg_dump.py` · pronto
      quando: `python3 scripts/release.py waveshare/esp32-p4-wifi6-touch-lcd
      --name esp32-p4-wifi6-touch-lcd-7b-professor-virtual-uvc-spike` gera
      o zip (apagar zip pré-existente em releases/ antes — senão o build é
      PULADO com exit 0).
- [ ] T4 — Regressão: build spike + build `7b-professor-virtual` + build
      `7b` original + testes host (incluindo teste novo do `--all`) ·
      pronto quando: 3 builds verdes e testes host passando.
- [ ] T5 — Bancada (flash = ação do dono; roteiro nas Notas): enumeração
      limpa + escada 800×600 → 1920×1080 → 2592×1944 → 3264×2448
      (→ 4000×3000 opcional) com `PV-UVC-RUNG … result=PASS` e
      applied==requested; `extract_jpeg_dump.py --all`; escada 3× · pronto
      quando: log bruto + JPGs em `../evidencias/f2b/` e tabela
      passa/falha por degrau preenchida nas Notas.
- [ ] T6 — Consolidação: decisão de rota F2B (UVC direto / tuning / outra
      câmera / outra plataforma) no decision-log + plano-firmware; destino
      do código do spike (rec.: manter gateado como ferramenta de
      bancada); desenho da integração (UVC-only vs híbrido CSI-preview) se
      PASS · pronto quando: decisão registrada e tabela de status
      atualizada.

## Contexto de retomada

> Preencha ao interromper o trabalho no meio de uma task (fim de sessão,
> pausa pedida pelo proprietário, bloqueio temporário). Substitua todo o
> conteúdo por "(vazio)" no commit que concluir a task retomada. O hook de
> SessionStart injeta esta seção integralmente na próxima sessão.

- Task em andamento: T1 (primeira task; fase aberta em 2026-08-10 na
  estação Windows/WSL — esta sessão do Mac NÃO tem memória daquela
  conversa; TODO o contexto necessário está neste arquivo).
- Último passo concluído (2026-08-10, estação Windows): validação física
  A/B que decidiu a rota — fotos CSI 1280×960 extraídas por dump serial e
  comparadas com a baseline UVC do piloto (`../evidencias/f2b/`, ler o
  README). Veredito do proprietário: CSI insuficiente (no piloto, essa
  classe de resolução já causava extração INCOMPLETA silenciosa até em
  impresso); rota UVC virou NECESSÁRIA. Pesquisas ChatGPT/Gemini analisadas
  (`../f2b-pesquisa-{chatgpt,gemini}.md`); spike desenhado (Notas, seção
  "Desenho do spike"); hooks de sessão corrigidos para reconhecer
  fase-2b.md; rotação 180° órfã commitada (validação física pendente).
- Próximo passo exato: T1 — no Mac, `git pull`; baixar dependências (um
  build da variante PV ou `idf.py reconfigure` popula managed_components/);
  ler `managed_components/espressif__esp_video/` respondendo os itens da
  T1; registrar nas Notas; fechar a lista de sdkconfig_append da variante
  spike. Depois T2 via `mcp__codex-council__codex` (fase roda com
  `/autonomous-phase fase-2b`).
- Decisões já tomadas: `F2B-RouteUvc` no decision-log (2026-08-10) — rota
  UVC necessária; spike decide; árvore de fallback (tuning → outra câmera
  UVC → outra plataforma, backend intacto); plano B interino CSI 1080p
  morto; degrau 2 da T6 da F2 condicionado ao desenho pós-spike.
- Estado do worktree / armadilhas: worktree limpo após os commits de
  abertura. Ambiente Mac: backend em
  /Users/institutorecriare/VSCodeProjects/licao_casa/backend (somente
  leitura; o spike NÃO precisa de backend nem de rede); porta serial da F2
  era /dev/cu.usbmodem5B3E0883401 a 115200 (captura: idf.py monitor ou
  miniterm com tee); builds fora do export.sh exigem ESP_IDF_VERSION=6.0;
  board dir do release.py é waveshare/esp32-p4-wifi6-touch-lcd; zip
  existente em releases/ PULA o build; Wi-Fi da placa "quarto_2.4GHz";
  flash é SEMPRE ação do proprietário. Durante o spike a tela fica
  APAGADA (o board nunca é construído — comportamento esperado).

## Notas da fase

### Abertura (2026-08-10, estação Windows/WSL)

Fase aberta fora do fluxo autônomo, em sessão interativa com o
proprietário, como handoff para o MacBook. Commits de abertura: correção
dos hooks (fase com sufixo alfabético + parser F2B da tabela; 14/14 testes
verdes), rotação 180° órfã (validação física pendente — binário 46db44b
não a contém) e este pacote (fase, evidências, pesquisas renomeadas,
decision-log, plano-firmware).

### Bancada de 2026-08-10 — validação física que decidiu a rota

Setup: 7B via USB-C "USB TO UART" (CH343 → COM3 no Windows); firmware PV
do release 46db44b (OV5647 CSI 1280×960 RAW10 binning, JPEG q85); captura
serial de dentro do WSL via interop `powershell.exe` (script
`C:\Users\Public\pv_capture.ps1` — a 1ª captura perdeu 90 bytes por
polling lento; corrigido com ReadBufferSize 4 MB e checagem de marcador só
na cauda; dumps seguintes íntegros, CRC-32 ok).

Roteiro do proprietário: rota Preparação → "Ver a câmera" → "Tirar foto"
(página de apostila "O Trabalho e a Tecnologia") → "Exportar
(diagnóstico)" → `scripts/pv/extract_jpeg_dump.py`.

- Foto 1 (pouca luz): JPEG 193.759 B. Impresso grande legível; legenda
  pequena mole (ruído/ganho). Foto 2 (mais luz, mesma página): 162.851 B;
  página quase cheia, legenda nítida a 300%, tom azulado (AWB), leve
  clarão. Luz ajuda muito — mas não muda o veredito.
- Medições do firmware: CaptureRaw 1280×960 UYVY 2.457.600 B; encode q85
  540–555 ms via fallback SW (encoder HW rejeita UYVY); decode 381–395 ms;
  PSRAM livre 15,27 MB (maior bloco 12,58 MB); dump ~18–24 s a 115200.
- A/B com a baseline UVC do piloto (`uvc-baseline-piloto-page1.jpg`,
  1358×1920, extração comprovada no MESMO backend): UVC ≈ 6,2 px/mm na
  página (crédito minúsculo legível; traço de lápis apagado visível) vs
  CSI ≈ 3,5–4 px/mm (foco fixo, mais ruído) — e a baseline roda em modo
  REDUZIDO; o nativo 3264×2448 dobraria de novo. Veredito do proprietário:
  manter a OV5647 como câmera de captura derrubaria a aplicação.

### Pesquisas externas (prompt `../f2b-deep-research-prompt.md`)

Resultados em `../f2b-pesquisa-chatgpt.md` e `../f2b-pesquisa-gemini.md`.
Consenso (fato): ISP do P4 ≤1920×1080 em silício, sem contorno; bypass CSI
com demosaic por software impraticável; rota UVC recomendada; NE-HD362
(160–260 mA) cabe no VBUS ~500 mA da 7B (TPS2051C); decoder JPEG HW ~4K;
MJPEG sobe ao backend sem reencode. Divergências resolvidas: sensor MIPI
com ISP interno EXISTE (ov5645.c YUV422 2592×1944@15fps — Gemini certo,
ChatGPT se contradiz) mas é rota inferior (10 MB/frame em PSRAM + comprar
módulo); o "máx. 1080p comprovado" do ChatGPT para UVC confunde streaming
contínuo com frame único — host UVC é agnóstico à resolução; riscos reais
são descritores/Max Packet Size no DWC2 (AEGHB-962/AEGHB-1321) e
`dwMaxVideoFrameBufSize`. Ressalva ao Gemini: "comprovadamente funciona"
para >1080p UVC no P4 excede a evidência citada — honesto é "muito
provável, sem demonstração pública"; por isso o spike é o passo decisivo.
Crítica ao prompt (pedida pelo dono): era placa-cêntrico; faltou "e se não
der nesta placa, qual hardware?"; a lacuna se fecha com o spike, não com
mais pesquisa.

### Verificações locais de código (2026-08-10)

- O caminho UVC já existe no firmware: `EspVideo` compila com
  `CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE`
  (`main/boards/common/esp_video.cc:132`); 3 boards S3 têm o código de
  init UVC — porém é código MORTO (zero `=y` no repo; o
  `esp-s3-lcd-ev-board-2` nem compila com UVC — chave sobrando). Valores
  de referência: `.uvc={.uvc_dev_num=1,.task_stack=4096,.task_priority=5,
  .task_affinity=-1}`, `.usb={.init_usb_host_lib=true,...}`.
- `EspVideo` NUNCA escolhe resolução (herda o default do driver via G_FMT;
  não há ENUM_FRAMESIZES no repo) e o ranking de formatos aborta com
  FOURCC desconhecido — `V4L2_PIX_FMT_MJPEG` ('MJPG') não é tratado em
  lugar NENHUM; `V4L2_PIX_FMT_JPEG` só sob
  `CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT` (`esp_video.cc:216-239`).
  `CaptureRaw`/`AcquirePreviewFrame` rejeitam JPEG/MJPEG
  (`ExpectedFrameBytes` devolve 0 — frame de tamanho fixo por desenho). O
  `Capture()` legado aceita JPEG mas o corromperia com o byte-swap que a
  7B liga (`esp_video.cc:456-465`).
- Ninguém usa USB host no caminho P4 hoje (RNDIS só instanciado por board
  S3) — sem conflito. Global útil: `sdkconfig.defaults.esp32p4:28`
  `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=1024`.
- `PvPhotoDump` é standalone (FreeRTOS/mbedtls/stdio; zero LVGL/PvApp) —
  reutilizável direto pelo spike. `extract_jpeg_dump.py` extrai só o
  ÚLTIMO bloco (por isso a flag `--all` na T3).
- `main.cc:30-36` boota o app por Kconfig e `Board::GetInstance()` é lazy
  (`board.h:62-65`): substituindo o app, o board (e o CSI) nunca é
  construído.
- Componente `esp_video` não estava no disco da estação Windows
  (managed_components nunca baixado lá; builds da F2 foram no Mac) — por
  isso a T1 existe.

### Desenho do spike (aprovado pelo proprietário em 2026-08-10; ratificar na T2)

Arquitetura **"A-sem-board"** — módulo autocontido
`main/professor_virtual/pv_uvc_spike.{h,cc}` que NÃO passa pelo
EspVideo/board: chama `esp_video_init` só com `.usb_uvc`, abre
`ESP_VIDEO_USB_UVC_DEVICE_NAME(0)` e fala V4L2 direto. Sequência: ENUM_FMT
logando TODOS os FOURCC; por degrau da escada: S_FMT(w,h) → G_FMT
confirmando applied==requested → REQBUFS/mmap → STREAMON → descartar ~10
frames (AF/exposição) → DQBUF com deadline → validar SOI/EOI (FFD8/FFD9) →
copiar p/ PSRAM → `PvPhotoDump::Request()` e aguardar `!busy()` →
STREAMOFF → munmap/close → próximo degrau. Log por degrau:
`PV-UVC-RUNG requested=WxH applied=WxH fourcc=… bytesused=N result=PASS|FAIL`
+ sumário final + loop idle. Tela fica apagada (board nunca construído).
Zero mudanças em board file e EspVideo (o que sobrevive para a integração
é o conhecimento: FOURCC real, configs, degraus válidos, tamanhos).

Gate: `CONFIG_PV_UVC_SPIKE` (bool, default n, depends on
PROFESSOR_VIRTUAL, ao lado de `main/Kconfig.projbuild:808`); 3º ramo no
`main.cc`; variante `esp32-p4-wifi6-touch-lcd-7b-professor-virtual-uvc-
spike` = cópia da entrada PV (mantendo as flags RAW10 — inócuas, CSI nunca
inicializa, e exigidas por `scripts/tests/test_pv_f2.py`) +
`CONFIG_PV_UVC_SPIKE=y` + `CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE=y`
+ configs USB host a fechar na T1 (candidatos:
`CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=2048..4096`; FIFO/bias do DWC2
só se a bancada acusar "EP MPS exceeds").

Riscos e mitigações: MJPG vs JPEG no FOURCC (neutro p/ spike — pipeline
próprio aceita ambos; registrar p/ integração); descritor >
CONTROL_TRANSFER_MAX_SIZE (subir na variante); "EP MPS exceeds
supported limit" (logar descritores; alt-settings; se irrecuperável →
ramo outra câmera); VBUS (fonte adequada; hub USB alimentado como
diagnóstico; anotar re-enumerações/brownout); S_FMT ajustado
silenciosamente (G_FMT sempre; degrau FAIL não derruba a escada); DQBUF
travado (deadline; STREAMOFF; próximo degrau; power-cycle entre
repetições); AF não converge (descartar ~10 frames/3 s; V4L2_CID_FOCUS_AUTO
se o backend expor; senão registrar como limitação).

Roteiro de bancada da T5 (flash = dono): câmera no USB-A; monitor serial
com tee; conferir enumeração (VID/PID, sem erros de descritor/MPS) e a
lista ENUM_FMT; escada automática completa; por degrau conferir
`result=PASS`; `extract_jpeg_dump.py --all`; abrir cada JPG (cena,
corrupção, foco); repetir a escada 3× e anotar power-cycles.

### Árvore de decisão da fase (sem fixação)

PASS em ≥2592×1944 → rota UVC confirmada; F2B vira integração (UVC-only vs
híbrido CSI-preview + UVC-captura — decidir na T6) + A/B de extração com o
backend real. FALHA por descritores/MPS → uma rodada de tuning; persistindo
→ pesquisa curta dirigida a câmeras UVC "embedded-friendly" (outra CÂMERA,
não outra placa). Classe UVC-no-P4 inviável (improvável) → decisão de
plataforma com o proprietário (ex.: SBC Linux — backend intacto, contrato é
HTTP; descarta o firmware F0–F2). Plano B interino CSI 1920×1080: morto
(+12% de linhas não fecha gap de óptica/foco).

### Registro da estação Windows (2026-08-10)

Ficaram naquela estação (não versionados): script de captura serial
`C:\Users\Public\pv_capture.ps1` (lê COM3 a 115200, encerra no
`PV-JPEG-END`); cópias das fotos em `C:\Users\Public\`; regra de firewall
do Kaspersky "Professor Virtual WSL TCP 8001" (Permitir/Entrada/TCP/porta
local 8001/qualquer endereço) criada para expor o backend rodando no WSL
(`~/licao_casa`) à placa na LAN — relevante se a bancada voltar àquela
máquina. A placa enxergava o backend em `http://192.168.15.9:8001` na F2
(Mac na LAN de 192.168.15.x).
