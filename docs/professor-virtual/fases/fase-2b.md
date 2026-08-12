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

- [x] T1 — Inspeção da API real do esp_video UVC pós-download (baixar
      managed_components no Mac; ler `espressif__esp_video`: campos de
      `esp_video_init_usb_uvc_config_t`, Kconfigs UVC do componente, FOURCC
      exposto pelo device UVC — JPEG vs MJPG, ENUM_FRAMESIZES
      implementado?, alocação de buffer vs `dwMaxVideoFrameBufSize`
      (interna vs PSRAM — se interna, escalar na T2), DQBUF
      bloqueante/O_NONBLOCK, V4L2_CID_FOCUS_AUTO) · pronto quando: fatos
      registrados nas Notas e lista final de `sdkconfig_append` fechada.
- [x] T2 — Decisões estruturais via Codex (ratificar arquitetura
      "A-sem-board" das Notas; ciclo open→close por degrau [rec.: sim];
      REQBUFS count [rec.: 2]; baud do console [rec.: 115200]; degrau
      opcional 4000×3000 [rec.: sim, nunca bloqueante]; timeout e frames
      de descarte p/ AF [rec.: ~10 frames/3 s]) · pronto quando: decisões
      no decision-log.
- [x] T3 — Implementação: `main/professor_virtual/pv_uvc_spike.{h,cc}`,
      3º ramo no `main.cc`, `CONFIG_PV_UVC_SPIKE` no Kconfig.projbuild,
      variante `esp32-p4-wifi6-touch-lcd-7b-professor-virtual-uvc-spike`
      no config.json, flag `--all` no `extract_jpeg_dump.py` · pronto
      quando: `python3 scripts/release.py waveshare/esp32-p4-wifi6-touch-lcd
      --name esp32-p4-wifi6-touch-lcd-7b-professor-virtual-uvc-spike` gera
      o zip (apagar zip pré-existente em releases/ antes — senão o build é
      PULADO com exit 0).
- [x] T4 — Regressão: build spike + build `7b-professor-virtual` + build
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

- Task em andamento: T5 (bancada), pausada em 2026-08-12 após 14 rodadas.
  Decisão do proprietário: **continuar na estação Windows** (troca da
  fonte upstream). Mistério aberto: canal ISOC mudo (ver Notas, "rodadas
  7-14") com código e câmera exonerados por experimento. Análise neutra
  do Codex solicitada — registrar o resultado quando disponível (se a
  sessão encerrar antes, refazer a consulta com os fatos das Notas).
- Estado da placa: firmware de CONTROLE (recompilação do commit 299f3ec,
  escada única por boot, DEBUG por pacote ligado) flashado. O firmware
  MAIS RECENTE do spike é o do commit 1c02248 (laço por replug + janela
  30 s) — zip em releases/ (rebuildar se ausente).
- Protocolo Windows (próxima sessão): (1) `git pull`; (2) conectar a 7B
  ao desktop pelo "USB TO UART" (COM3/CH343 — dossiê da placa, seção
  conexão serial; usbipd para WSL se preciso); (3) câmera DESPLUGADA;
  flash da variante spike (merged-binary.bin @0x0); (4) captura serial
  115200 SEM tocar DTR/RTS (o `C:\Users\Public\pv_capture.ps1` daquela
  estação captura COM3; conferir se não reseta a placa ao abrir); (5)
  boot com porta USB-A vazia → plugar a câmera → escada roda sozinha
  (firmware do laço) — replug dispara novas escadas; (6) peças de
  diagnóstico se persistir: hub USB alimentado entre câmera e placa;
  adaptador USB-A fêmea→USB-C macho para testar a porta "USB OTG"
  (mesmo controlador, conector virgem).
- Fatos que a sessão Windows precisa saber: TODO reboot da placa com
  câmera energizada pode deixá-la muda/travada (só replug físico ou
  plugagem pós-boot recupera); unplug sem stream aberto cria
  slot-fantasma no driver (só reboot limpa) — por isso a ordem
  desplugar→resetar→plugar; S_PARM≠default emudece a câmera; a câmera é
  saudável (Photo Booth no Mac); o PASS de referência é
  `t5-rodada4-foto-800x600.jpg` (CRC 36aa87a9).
- Armadilhas: SEMPRE liberar a porta serial antes de flash ("multiple
  access on port"); flash é do proprietário; zip existente em releases/
  PULA o build; builds exigem o IDF 6.0.2 (no Mac:
  `~/.espressif/tools/activate_idf_v6.0.2.sh` + PATH; no Windows,
  ambiente próprio daquela estação).

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

### T5 — Bancada (2026-08-11, rodadas 1-6; em andamento)

Roteiro operacional que emergiu: flash é do proprietário (linha
`! zsh releases/flash-spike.sh`, funciona por controle remoto); captura e
monitoramento são do implementador (pyserial com dtr/rts=False; o open da
porta auto-reseta a placa — o boot cai inteiro na captura); liberar a
porta antes de todo flash.

- **Rodada 1** (config da T2): câmera enumera perfeita (14 tamanhos JPEG,
  TODOS os degraus anunciados, até 4000×3000), mas zero frames e assert
  `usbh_dev_close` no teardown → boot loop. Log:
  `t5-rodada1-isoc-sem-frames.log`.
- **Rodada 2** (tuning: usb task 2→6, REQBUFS 4, sizeimage, BIAS_IN,
  DEBUG): o por-pacote revelou a 1ª causa raiz: TODOS os pacotes
  rejeitados por `invalid UVC payload header 0c 4d` — a NE-HD362 não seta
  o bit EOH e `CONFIG_UVC_CHECK_PAYLOAD_HEADER_EOH` (default y) descarta
  tudo. Log: `t5-rodada2-eoh-rejeita-pacotes.log`.
- **Rodada 3** (EOH off): SEM crash (dreno 400 ms) e escada completa —
  mas silêncio total: câmera estava TRAVADA (ver rodadas 5-6).
- **Rodada 4** (pós-replug físico da câmera): **MARCO — 800×600 PASS**:
  frame JPEG 32.405 B íntegro (SOI/EOI, CRC), dump ok, foto verificada no
  Mac (`t5-rodada4-foto-800x600.jpg`). Transporte ISOC high-bandwidth
  3×1024 FUNCIONA no P4 (8.307 callbacks, zero usb err) — a hipótese de
  limitação de silício CAIU. ERR da câmera é intermitente (AF/exposição);
  warmup lida. Degraus 2-5: `open errno=22` — 2ª causa raiz: esp_video
  2.0.1 não sobrevive a open→close→open após streaming
  (`uvc_video_init: Failed to get frame info`). Log:
  `t5-rodada4-800x600-pass.log`.
- **Rodada 5** (device único para a escada, reconfig por degrau): o ciclo
  FUNCIONA (5 degraus reconfiguram sem erro de reopen), mas zero frames:
  3ª causa raiz — a câmera trava o plano de streaming quando atravessa
  reboots da placa energizada (controle/enumeração perfeitos; só replug
  físico destrava; a rodada 4 passou porque o replug era fresco). Log:
  `t5-rodada5-ciclo-ok-camera-travada.log`.
- **Rodada 6** (replug por firmware): host instalado com
  `root_port_unpowered=true`, 1 s off, `usb_host_lib_set_root_port_power`
  — executa limpo e a câmera re-enumera, mas streaming segue mudo:
  o EN do TPS2051C NÃO é fiado ao controle de VBUS do P4; a energia da
  USB-A não cai por software. Destravamento é FÍSICO. Log:
  `t5-rodada6-vbus-nao-destrava.log`. Implicação de produto: liga/desliga
  normal corta a energia da câmera junto (sem problema por construção);
  reboots por software (OTA/recuperação) precisam de mitigação na
  integração (avaliar na T6; candidatos: chave de VBUS no hardware do
  gabinete, ou verificar se o padrão de uso real sequer dispara o
  travamento).

**Rodadas 7-14 (2026-08-11/12) — o mistério do canal mudo.** Rodadas de
protocolo físico (RST provado inócuo para o travamento; laço de escadas
disparado por replug; dança desplugue→RST→plugue) e descobertas em série:
o driver tem **slot-fantasma** (unplug sem stream aberto nunca é adotado
de novo até reboot — o callback de desconexão só existe com stream
aberto); `VIDIOC_S_PARM` fps=10, embora anunciado e aceito, **emudece a
câmera** (revertido); e o padrão final, não explicado em bancada: a
câmera alterna entre *streaming-com-ERR* (rodada 4 — que capturou o
frame íntegro — e run11) e **silêncio absoluto no canal ISOC** (zero
callbacks, zero erros; runs 13/14), com enumeração/S_FMT/STREAMON
perfeitos em todos os casos. **Experimento de controle**: o binário do
próprio commit 299f3ec (o que capturou a foto em 2026-08-11) recompilado
e reexecutado em 2026-08-12 no procedimento idêntico → mudo → **o diff de
firmware está exonerado**. A câmera funciona perfeita num MacBook (Photo
Booth, testado) → **câmera exonerada**. Sem efeito: carregador do Mac,
troca de porta USB do Mac (power-cycle total), reencaixe firme, descansos
variados. Logs: `t5-run11-…`, `t5-run13-…`, `t5-run14-controle-…`.
Análise independente do Codex solicitada de forma neutra (fatos + código,
sem hipóteses do implementador) — resultado a registrar.

Próxima sessão (estação Windows, decisão do proprietário): trocar a fonte
upstream (desktop) e repetir o protocolo; peças de diagnóstico a adquirir:
hub USB alimentado e adaptador USB-A fêmea→USB-C (testa a porta OTG-C,
mesmo controlador por conector virgem).

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

### T1 — API real do caminho UVC (2026-08-10; Mac)

Componentes já estavam no disco do Mac, baixados pelos builds da F2 (a
dependência `usb_host_uvc` do esp_video é por target `[esp32p4, esp32s3]`,
não por Kconfig): **esp_video 2.0.1** (commit 231c131), **usb_host_uvc
2.4.2**, **usb (host stack) 1.4.1** — o stack USB host agora é componente
gerenciado, não parte do IDF. Fontes lidas:
`src/device/esp_video_usb_uvc_device.c`, `esp_video_init.{h,c}`,
`esp_video.c` (config_buffer, dqbuf), `esp_video_ioctl.c`, Kconfigs dos
três componentes, `uvc_host.c`/`uvc_bulk.c`.

- Init: `esp_video_init_usb_uvc_config_t` tem dois blocos — `.uvc{
  uvc_dev_num, task_stack, task_priority, task_affinity}` e `.usb{
  init_usb_host_lib, peripheral_map, task_stack, task_priority,
  task_affinity}`. `peripheral_map=0` seleciona o periférico default (no
  P4, o HS). Os valores de referência dos boards S3 do repo não têm
  `peripheral_map` (API antiga) — incluir o campo. Com `.csi=NULL` o laço
  de detect de sensores não roda: CSI intocado, como o desenho previa.
- Device: abrir `"/dev/video40"` (`ESP_VIDEO_USB_UVC_DEVICE_NAME(0)`;
  IDs 40–49; nome interno "USB-UVC0").
- FOURCC: o device converte o MJPEG da câmera para **`V4L2_PIX_FMT_JPEG`**
  (e YUY2→YUYV, H264, HEVC). Não existe 'MJPG' no caminho — o risco
  "MJPG vs JPEG" do desenho está RESOLVIDO: o pipeline vê JPEG.
- **ENUM_FMT, ENUM_FRAMESIZES (DISCRETE, da frame list real da câmera) e
  ENUM_FRAMEINTERVALS são implementados** — a escada pode ser dirigida
  pelo que a câmera anuncia, não por chute.
- S_FMT exige match EXATO (w, h, formato) na lista da câmera; fora dela
  devolve `ESP_ERR_INVALID_ARG` — não ajusta silenciosamente (G_FMT vira
  confirmação, não descoberta).
- Buffers: para JPEG, tamanho = `sizeimage` (se >0 no S_FMT) senão
  `w*h` bytes; caps = `MALLOC_CAP_SPIRAM` com CONFIG_SPIRAM → **PSRAM**,
  nada a escalar. Zero-copy: os buffers V4L2 são entregues ao uvc_host
  como `user_frame_buffers`. `dwMaxVideoFrameSize` vem da negociação
  Probe/Commit (não dos descritores); frame maior que o buffer gera
  evento OVERFLOW e descarte limpo, não corrupção (`w*h` ≥ qualquer JPEG
  do degrau — seguro). Pior degrau 4000×3000: 12 MB/buffer; REQBUFS=2 →
  24 MB de PSRAM (ok no app do spike, sem UI/LVGL).
- DQBUF é bloqueante com default `portMAX_DELAY`; existe
  **`VIDIOC_S_DQBUF_TIMEOUT`** (struct timeval, extensão Espressif) — o
  deadline por degrau usa isso, sem polling nem O_NONBLOCK.
- open() espera a enumeração até `CONFIG_USB_UVC_INIT_TIMEOUT_MS`
  (default 10 s); se a câmera já enumerou, retorna direto — o ciclo
  open→close por degrau é barato.
- Renegociação: `uvc_host_stream_format_select` para/reconfigura/reinicia
  o stream sozinho — tanto o ciclo STREAMOFF→S_FMT→STREAMON quanto o
  close→open completo por degrau são suportados.
- AF: `V4L2_CID_FOCUS_AUTO` **não é plumbado** (sem ext_controls no
  device UVC; usb_host_uvc 2.4.2 não expõe controles CT/PU). O autofoco
  fica por conta da câmera; mitigação confirmada = descartar ~10 frames.
  Registrar como limitação para a integração.
- Transporte: isoc E bulk implementados (`uvc_bulk.c` dedicado) — câmeras
  MJPEG de alta resolução frequentemente usam bulk; sem lacuna.
- Kconfigs relevantes: `USB_UVC_VIDEO_DEVICE_URB_SIZE` default 10240;
  `USB_UVC_INIT_TIMEOUT_MS` default 10000 (range 500–60000);
  `UVC_PRINTF_CONFIGURATION_DESCRIPTOR` (default n) imprime o descritor
  completo no console — LIGAR no spike (evidência da T5: formatos, MPS,
  dwMaxVideoFrameBufSize); `UVC_INTERVAL_ARRAY_SIZE` default 3. No
  componente usb 1.4.1: `USB_HOST_CONTROL_TRANSFER_MAX_SIZE` default 256
  (o global do repo já sobe para 1024 em `sdkconfig.defaults.esp32p4`) e
  o choice `USB_HOST_HW_BUFFER_BIAS` (BALANCED default; `BIAS_IN` é a
  1ª mitigação se a bancada acusar "EP MPS exceeds").

**Lista FINAL de `sdkconfig_append` da variante spike** (sobre a cópia da
variante PV, mantendo as flags CSI/RAW10 — inócuas com o board nunca
construído e exigidas por `scripts/tests/test_pv_f2.py`):

```
CONFIG_PV_UVC_SPIKE=y
CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE=y
CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=4096
CONFIG_UVC_PRINTF_CONFIGURATION_DESCRIPTOR=y
```

URB size e init timeout ficam nos defaults; FIFO bias fica BALANCED. O
4096 antecipa descritores multi-formato de câmera 12 MP acima de 1 KB
(custo de RAM trivial); T2 ratifica junto com os demais parâmetros.

Ambiente Mac (durável, ex-Contexto de retomada): porta serial da F2
`/dev/cu.usbmodem5B3E0883401` a 115200 (captura: `idf.py monitor` ou
miniterm com tee); builds fora do export.sh exigem `ESP_IDF_VERSION=6.0`;
board dir do release.py `waveshare/esp32-p4-wifi6-touch-lcd`; zip
existente em `releases/` PULA o build (exit 0); o spike não usa rede nem
backend; flash é SEMPRE ação do proprietário.

**Correção de ambiente descoberta na T3**: o IDF 6.0.2 do projeto fica em
`~/.espressif/v6.0.2/esp-idf`, ativado por
`source ~/.espressif/tools/activate_idf_v6.0.2.sh` — e nessa instalação o
`idf.py` é função de shell, invisível ao `os.system()` do release.py:
depois de ativar, faça `export PATH="$IDF_PATH/tools:$PATH"`. O
`~/esp/esp-idf` é um IDF **5.5.2** — NÃO usar (um build acidental com ele
reescreve o `dependencies.lock`, não versionado; o build 6.0.2 restaura).

### T3 — Implementação do spike (2026-08-10; Mac)

Implementada por subagente com revisão do orquestrador. Arquivos:
`pv_uvc_spike.h` (48 l) + `pv_uvc_spike.cc` (732 l, conteúdo inteiro sob
`#if CONFIG_PV_UVC_SPIKE` + `#error` se o device UVC não estiver ligado);
3º ramo no `main.cc` (falha do spike NÃO cai no app normal — preserva o
isolamento); `config PV_UVC_SPIKE` no Kconfig.projbuild; variante
`…-uvc-spike` no config.json (cópia exata da PV + as 4 flags da T1);
`--all` no `extract_jpeg_dump.py` (índice = posição do bloco no log;
bloco corrompido não desloca os demais; exit 1 se algum falhar, íntegros
sempre gravados); 6 testes host novos (21/21 verdes). Formato: os dois
arquivos novos passam `clang-format --dry-run -Werror` (o binário está em
`~/.espressif/tools/esp-clang/…/bin/clang-format`); `main.cc` não foi
reformatado (violações pré-existentes; churn não relacionado).

Desvio documentado no código: `ESP_VIDEO_USB_UVC_DEVICE_NAME()` injeta um
`extern` no escopo de uso — dentro de namespace anônimo vira símbolo de
linkage interna e o link quebra; a única chamada mora em função `static`
no escopo global do .cc.

Build da variante spike:
`releases/v2.4.0_waveshare-esp32-p4-wifi6-touch-lcd-7b-professor-virtual-uvc-spike.zip`
(1.967.944 B; `xiaozhi.bin` 954.496 B — o linker descarta PvApp/LVGL
inteiros porque nada os referencia no ramo do spike; bom para bancada).
`SpikeTask` confirmado no `.map`; sdkconfig efetivo com as 4 flags.

### T5 — Bancada, rodada 1 (2026-08-11; Mac) — FALHA → tuning (ramo previsto)

Fluxo da bancada: flash da variante spike pelo proprietário (esptool,
`--after no_reset`); captura serial minha por pyserial (DTR/RTS soltos —
o abrir da porta dispara o auto-reset e o boot cai inteiro no log);
evidência em `../evidencias/f2b/t5-rodada1-isoc-sem-frames.log`.

O que funcionou: USB host sobe; **NE-HD362 enumera em 2 s**, descritor
limpo (sem "EP MPS exceeds"); FOURCC JPEG com **14 tamanhos — todos os
degraus da escada anunciados** (800×600 … 4000×3000, mais 3840×2160);
YUYV também exposto (12 tamanhos, até 2592×1944@5fps); PSRAM 32,6 MB
livre. Enumeração V4L2 completa logada (formatos × tamanhos ×
intervalos: JPEG dá 30/15/10 fps na maioria dos tamanhos).

O que falhou: degrau 800×600 — S_FMT/G_FMT ok, mas
`Allocating 2 USB transfers for ISOC. Each: 3072 bytes, 1 ISOC packets,
3072 MPS`: endpoint ISOC high-bandwidth (MPS 1024×3), payload negociado
(dwMaxPayloadTransferSize) = 3072 capou o URB em 1 pacote, e o glue
esp_video usa REQBUFS count (=2) como número de URBs → 2 URBs de 1
pacote para microframes de 125 µs. **Zero frames em 8 s** (warmup e
DQBUF final estouram timeout, errno=1). Nenhum LOGW do driver ("usb
err"/"frame error"/"invalid MJPEG SOI" teriam aparecido) — os detalhes
por pacote são LOGD, invisíveis em INFO. Na desmontagem do degrau:
`assert failed: usbh_dev_close usbh.c:1226
(dev_obj->dynamic.num_ctrl_xfers_inflight == 0)` → SW_CPU_RESET →
**boot loop** (5 ciclos idênticos no log). Config da rodada 1: usb host
lib task priority=2 / uvc task=5, herdados do código morto S3 (nunca
validados em streaming real).

Rodada 2 de tuning (decision-log `F2B-SpikeTuningR2-*`, Codex ratificou
os 5 itens; SUPERSEDE F2B-SpikeReqbufs e F2B-SpikeSdkconfig): usb host
lib task 2→6 (daemon ≥ clientes; latência de resubmissão de URB);
REQBUFS 2→4 com `sizeimage = max(1 MiB, w*h/2)` (pior caso 24 MB) e
guarda `req.count==4`; variante ganha `CONFIG_USB_HOST_HW_BUFFER_BIAS_IN=y`
e `CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y` (spike liga DEBUG em runtime para
uvc/uvc-isoc/uvc-bulk/uvc-frame/usb_uvc_device — bancada enxerga pacote
a pacote); dreno de 400 ms entre STREAMOFF e close (workaround do assert
do stack usb 1.4.1 — managed component; se persistir, vira issue
registrada para a integração). fps/S_PARM reservado para eventual
rodada 3.

Desvio de build da rodada 2: `CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y` fez um
`ESP_LOGD` do `espressif__esp_lcd_st7701` (managed component; compila
mesmo sem o spike usar tela) virar código vivo — e ele tem use-after-free
real num log de debug, que o GCC ≥12 trata como erro (`-Werror`).
Correção: `CONFIG_COMPILER_DISABLE_GCC12_WARNINGS=y` na variante spike
(flag oficial do IDF apontada pelo hint do próprio build; confinada à
bancada). Bug do componente anotado — não é nosso código nem caminho de
execução do spike.

### T4 — Regressão (2026-08-10/11; Mac)

3 builds verdes no estado da T3: spike (zip 1.967.944 B), PV normal
(3.015.223 B — contém a rotação 180° de b837db1, validação física
pendente) e 7b original (3.409.027 B); testes host 21/21. Os zips da F2
foram movidos do `releases/` para backup fora do repo antes dos rebuilds
(zip existente PULA o build). O build da 7b original foi interrompido por
desligamento do Mac a ~96% e reexecutado limpo na retomada.
