# Fase F3 — Fatia vertical do turno por foto

> Regra de ouro: cada checkbox é marcado **no mesmo commit** que conclui a
> task — o estado nunca pode divergir do código. No encerramento da fase,
> atualizar a tabela "Status das fases" do `plano-firmware.md` no commit
> final, após a revisão independente do Codex.

## Objetivo

O marco central (plano F3; spec B.5 item 9): `POST /api/turn` multipart
(chunked, timeout ≥120 s) contendo **a imagem JPEG e `session_id`** —
**nenhum áudio é enviado na F3** —, aplicando o **perfil comum de turno
v1.1** (`request_id` UUID v4 novo por turno lógico, `media=url`,
`audio_format=wav`, `image_max_px=1280`, token em toda chamada). Parse da
resposta com validação do eco de `request_id` e exigência de
`audio_base64=""`/`image_base64=""`; download de `image_url` (JPEG exibido)
e `audio_url` (WAV interpretado por **parser RIFF/WAVE por chunks** → PCM →
`AudioService::PlayPcm`); sons locais de feedback (OGG); re-hidratação
pós-turno conforme o contrato. **Não há decodificação base64 de mídia** e
**não há decoder nem fallback MP3 para a voz.** Erros 502/409 seguem a
regra de idempotência do contrato v1.1 (§7.5 + contrato).

Fatos herdados:

- Câmera validada (F2): `PvCamera` entrega `PvCameraCaptureResult` (JPEG já
  girado em pé + RGB565 decodificado); tela de revisão pronta
  (`PvCameraScreen`).
- HTTP: `PvBackendClient` cobre health/state/lesson; multipart chunked tem
  exemplo comprovado em `main/boards/common/esp_video.cc:956-1037`.
- `AudioService::PlayPcm` ainda NÃO existe (toque aditivo previsto na lista
  fechada do plano); reprodução atual decodifica só Opus.
- Sons locais: pipeline MP3→OGG pronto (`scripts/mp3_to_ogg.sh`); fontes em
  `licao_casa/frontend/public/sounds/` (dependência somente leitura).
- WAV do backend: arquivo finito, RIFF `ChunkSize` = físico−8, `data` = PCM
  real; PCM s16le mono 16 kHz; parser NUNCA presume offset fixo de 44 bytes.

## Pronto quando

Ciclo completo mostrar tarefa→foto→resposta com voz+imagem funciona contra
o backend real; RAM medida no pico do turno. Builds PV + 7b original
verdes; testes host passam; decisões registradas no decision-log.
**Validação final exige placa em mãos (flash — ação do dono).**

## Pendências físicas (hardware)

- Validação fim a fim com o backend real na LAN: tarefa na tela → foto →
  `POST /api/turn` → imagem do tutor exibida + voz WAV reproduzida no
  alto-falante; medição de RAM no pico do turno (heap/PSRAM).

## Tasks

- [x] T1 — Decisões estruturais da F3 via Codex (onde vive o parse da
      resposta do turno [módulo puro host-testável vs pv_backend_client];
      assinatura/posse de `AudioService::PlayPcm`; desenho do job de turno
      no PvWorker [um job composto POST+downloads vs etapas]; UI do fluxo
      [revisão da câmera ganha "Enviar" e a resposta aparece onde];
      tratamento 409/502/timeout no MVP F3 [sem retransmissão automática];
      quais sons locais entram na F3; destino do export provisório da F2)
      · pronto quando: decisões registradas no decision-log.
- [x] T2 — `AudioService::PlayPcm` aditivo em `main/audio/audio_service.*`
      (PCM s16le mono 16 kHz → fila de playback; sem bloquear tasks de
      áudio; sem tocar o caminho Opus existente) · pronto quando: build PV
      verde e regressão 7b original verde.
- [x] T3 — `pv_audio.cc/.h`: parser RIFF/WAVE por chunks (parte pura,
      host-testável: valida RIFF/WAVE, percorre chunks, exige PCM s16le
      mono 16 kHz e arquivo finito; rejeita layout inválido) + testes host
      cobrindo casos válidos, truncados, placeholder de streaming e offsets
      não canônicos; entrega do PCM a `PlayPcm`; sons OGG locais de
      feedback convertidos e embarcados · pronto quando: testes host verdes
      e build PV verde.
- [x] T4 — `pv_backend_client`: perfil comum de turno v1.1 (`request_id`
      UUID v4 via esp_random; `PostTurnPhoto` multipart chunked com
      `session_id` + `image` + campos v1.1; timeout ≥120 s) + parse da
      resposta (eco de `request_id`, `audio_base64`/`image_base64` vazios,
      veredicto/posição) + `DownloadMedia` com token e validação de
      MIME/extensão (`audio/wav`+`.wav`, `image/jpeg`+`.jpg`) · pronto
      quando: parse coberto por teste host; build PV verde.
- [x] T5 — `PvWorker`: job composto de turno (decisão F3-D4: POST +
      download do WAV + download do JPEG numa única cadeia com UMA
      tentativa HTTP por etapa, sem retry; resultado com etapa que
      interrompeu, http_status preservado e geração de conectividade;
      entrada e saída por slots sob mutex com posse movida; um turno em
      voo por vez) · pronto quando: build PV verde e semântica de erro
      documentada para a T6.
- [x] T6 — PvApp + UI: fluxo completo na tela (revisão → "Enviar" →
      processando → resposta: imagem do tutor + voz + som de feedback →
      re-hidratação → volta ao preview/rota); comandos bloqueados fora de
      `idle` (mini-máquina da F3); regra de erro do contrato: após
      QUALQUER erro com resposta do servidor (409/502/4xx/5xx),
      re-consultar estado antes de mostrar qualquer coisa; após 409,
      descartar ids pendentes, re-hidratar e só então permitir turno novo
      com UUID novo; nunca reproduzir 409/502 como resposta pedagógica;
      erros com toast/telas conforme §9.7 (401/503 → configuração) ·
      pronto quando: build PV verde e caminho de cada erro rastreável no
      código.
- [x] T7 — Regressão e medição: build
      `esp32-p4-wifi6-touch-lcd-7b-professor-virtual` + build
      `esp32-p4-wifi6-touch-lcd-7b` original + testes host
      (`python3 -m unittest discover -s scripts/tests` e host_test do PV) +
      instrumentação de RAM no pico do turno · pronto quando: tudo verde e
      instrumentação pronta (número final sai na T8).
- [ ] T8 — Validação física com o backend real (flash pelo dono): ciclo
      tarefa→foto→resposta com voz+imagem; RAM medida no pico · pronto
      quando: confirmado pelo proprietário.

## Contexto de retomada

> Preencha ao interromper o trabalho no meio de uma task (fim de sessão,
> pausa pedida pelo proprietário, bloqueio temporário). Substitua todo o
> conteúdo por "(vazio)" no commit que concluir a task retomada. O hook de
> SessionStart injeta esta seção integralmente na próxima sessão.

- Task em andamento: T8 — validação física, JÁ MAJORITARIAMENTE FEITA em
  2026-08-16. O CICLO CENTRAL FOI VALIDADO pelo proprietário: tarefa na
  tela (Tutoria) → foto → Enviar → som de feedback + VOZ do tutor no
  alto-falante + IMAGEM da API na tela → saída ao fim do áudio (dois
  terminais) → volta ao preview. RAM no pico (log serial): antes do POST
  100 KB internos/12,0 MB PSRAM livres; fim do job (WAV+RGB vivos) 97 KB/
  10,0 MB; WAV+PCM vivos 97 KB/13,9 MB. Turno completo em ~19,5 s
  (foto 167 KB; voz 423 KB; imagem 1280x698).
- Último passo concluído: bug do "Voltar" da tela de câmera DIAGNOSTICADO
  e CORRIGIDO em código, aguardando reteste físico: o selo de conexão
  (canto sup. direito, com ext_click_area de 40 px para o gesto de 3 s)
  cobria o botão Voltar desde que a barra de ações foi para a lateral
  direita (F2/etapa 3) e engolia os toques; o selo agora fica à esquerda
  da barra SÓ nesta tela (pv_camera_screen.cc, após CreateConnectionBadge).
- Próximo passo exato (amanhã): (1) gravar o binário novo com
  `bash scripts/pv/flash_app.sh` (o build/xiaozhi.bin e o zip já saem com
  a correção; NVS preservada); (2) retestar o "Voltar" no preview e na
  revisão (e conferir que o gesto de 3 s continua funcionando no selo e
  no rodapé da versão); (3) opcional recomendado: 1 erro induzido
  (desligar o backend durante um envio) para ver o caminho de erro na
  tela; (4) proprietário confirma → marcar T8, fechar checklist, tabela
  "Status das fases" no commit final. Backend: subir com
  `cd licao_casa/backend && ./venv/bin/uvicorn main:app --host 0.0.0.0
  --port 8001` (o .env fica na RAIZ do licao_casa; sessão de teste ativa
  criada em 2026-08-16 via POST /api/prepare com data/images/page_1.jpg).
- Rodada 3 (autorizada) CONCLUÍDA; sem pendências de revisão. Incidente
  de boot resolvido (gate do AFE, commit 6f94c9b) — ver Notas.
- Estado do worktree: apenas a correção do Voltar pendente de commit (se
  esta sessão não a commitou, o diff está em
  ui/pv_camera_screen.cc); MCP codex-council com token da conta antiga
  (usar `codex exec` CLI ou reiniciar a sessão para reautenticar).

## Notas da fase

- Estado Git no início da fase: `01fa57e` (worktree limpo), 2026-08-16.
- Placa desconectada durante T1–T7 (combinado com o proprietário em
  2026-08-16); conexão só na T8.
- T8, incidente de boot (2026-08-16): o primeiro flash da F3 entrou em
  crash-loop ANTES de app_main ("assert failed: esp_startup_start_app
  app_startup.c:83" — criação da task principal do FreeRTOS falha), com
  PSRAM/XIP saudáveis e pools de heap presentes. DIAGNÓSTICO por bissecção
  física + 2 sondas (todas gravadas pelo proprietário, NVS preservada):
  F2 boota; T2 e T5 bootam; T6 crasha; sonda 1 (T6 sem vínculo do
  PvAudio) boota; sonda 2 (T6 completo, apenas sem construir o
  AfeAudioEngine) boota com PvAudio disponível. GATILHO DEMONSTRADO: o
  VÍNCULO da pilha AFE/esp-sr (wakenet/dl_lib, arrastada pelo linker a
  partir da T6 via PvAudio→AudioService::Initialize; +736 KB de binário).
  Correlação registrada: com esp-sr vinculado, RETENT_RAM cai de 111 para
  98 KiB no heap_init. CAUSA RAIZ DO ASSERT NÃO DEMONSTRADA (postura
  F2B). CORREÇÃO (decisão F3-AfeGate, Codex decisor): gate
  CONFIG_PROFESSOR_VIRTUAL em audio_service.cc — o PV não constrói o
  motor AFE (só reproduz e, na F4, grava pré-AFE); guardas de nulo já
  existentes + WARN nos pontos de ativação; caminho do assistente
  intacto no #else; lista fechada do plano atualizada. Flash ~480 KB
  menor no PV (medição, não justificativa). QUESTÃO ABERTA CONDICIONAL
  (F8): só se a F4 medir eco que exija tratamento, investigar o
  mecanismo link/layout e decidir tap pós-AFE ou alternativa;
  reintroduzir AFE exige nova decisão + validação física de boot.
- Revisão independente, rodada 3 (2026-08-16, autorizada explicitamente
  pelo proprietário; escopo restrito às correções da rodada 2): destino
  da foto pelo http_status VERIFICADO como correto e completo;
  `hydration_pending()` correto na concorrência, mas com P1 residual —
  um HydrationDone perdido com o turno em Idle deixaria
  `hydration_ready_` armado para sempre e a guarda recusaria todos os
  envios. CORRIGIDO: a reconciliação agora drena QUALQUER resultado de
  hidratação pronto (HandleHydrationDone genérico, no-op sem resultado),
  em qualquer fase, fazendo o que o evento perdido teria feito. Builds e
  testes host verdes após a correção.
- Revisão independente, rodada 2 (2026-08-16, Codex effort high, sessão
  nova): das correções da rodada 1, 8 CONFIRMADAS e 2 incompletas que
  compunham 1 P0 e 1 P1 novos; o adiamento do harness de fakes (P1f) foi
  ACEITO ("deixa de ser bloqueante da F3 enquanto dívida de cobertura",
  condicionado à F8 e à T8). CORRIGIDOS em seguida: P0 — o destino da
  foto passa a vir do http_status, não só da etapa: 200 (mesmo com corpo
  perdido/inválido/eco divergente) = turno APLICADO e 409 = indeterminado
  ⇒ a foto SAI da revisão (to_preview) nos dois casos, inclusive no ramo
  de geração obsoleta, que agora preserva a evidência do status e marca o
  espelho como velho; 502/4xx sem efeito mantêm a revisão. P1 — guarda de
  envio troca `hydrate_in_flight()` por `hydration_pending()` (em
  execução OU resultado pronto ainda não consumido por TakeHydration —
  sem vão, pois `hydration_ready_` é gravado antes de `in_flight` cair).
  LIMITE DE RODADAS: o teto de 2 revisões foi atingido; estas duas
  correções NÃO passaram por verificação independente — pendência
  apresentada ao proprietário junto com a T8 (uma rodada extra de
  verificação exige autorização dele).
- Revisão independente, rodada 1 (2026-08-16, Codex effort high via
  `codex exec` — o MCP ficou com token da conta antiga após troca de
  conta do proprietário; fallback técnico previsto): 1 P0, 6 P1, 4 P2.
  CORRIGIDOS: P0 (turno inalcançável — botão de câmera agora também na
  rota Tutoring e guarda de rota aceita Preparation|Tutoring; Failsafe/
  Celebration seguem bloqueadas); P1a (erro COM resposta agora entra em
  `ErrorRecovering`: tela bloqueada em rótulo neutro até a TENTATIVA de
  re-hidratação terminar; rota ≠ Tutoring vence o aviso — no failsafe o
  overlay É o aviso); P1b (mensagem de mídia neutra, sem anunciar
  avanço); P1c (audio_url/image_url exigem prefixo `/api/media/` — token
  jamais sai da origem configurada); P1d (envio recusado com hidratação
  em voo; hidratação OK durante Sending/ErrorRecovering só atualiza o
  espelho, nunca rouba a tela de um POST em voo); P1e (evento de voz com
  `audio_.busy()` é atrasado e é ignorado; envio recusado com voz de
  turno anterior ainda tocando); P2a (pv_wav exige consumo exato do
  RIFF — TrailingGarbage); P2b (Content-Type ausente rejeita); P2c
  (RequireInt rejeita fracionário/fora de faixa); P2d (PlayPcm rejeita
  taxa que zeraria o frame). Testes host: 155+249+114, 0 falhas; builds
  PV e 7b verdes.
  ADIADO (registro): a parte do P1f que exige fakes de FreeRTOS/HTTP e
  um harness de concorrência (PlayPcm/PvAudio/PvWorker/fluxo do PvApp)
  fica para a F8 — Robustez, fase da matriz de falhas; o harness host
  atual é de módulos puros e a infraestrutura nova não cabe no MVP da
  F3 sem ampliá-lo. A parte testável AGORA (P1c/P2a/P2c) foi coberta.
- T7 (2026-08-16): regressão e medição no Mac (IDF 6.0.2) — build
  `esp32-p4-wifi6-touch-lcd-7b-professor-virtual` VERDE; build
  `esp32-p4-wifi6-touch-lcd-7b` original VERDE (PlayPcm não regrediu o
  assistente); `python3 -m unittest discover -s scripts/tests` OK;
  host_test do PV: 155 + 170 + 95 verificações, 0 falhas (ASan/UBSan).
  Binário PV: 3.658.848 bytes (~3,49 MiB) no slot OTA de 4 MiB
  (partitions/v2/32m.csv) — folga ~520 KiB. Instrumentação de RAM do
  turno pronta: marcos "antes do POST", "fim do job (WAV+RGB vivos)"
  (pv_worker) e "WAV+PCM vivos" (pv_audio), com heap interna e PSRAM
  livres em KB; os NÚMEROS saem na T8, na placa.
- T1 (2026-08-16): decisões F3-D1..D7 ratificadas pelo Codex (thread
  01a00abb) e registradas no decision-log. Pontos que moldam as tasks:
  parse do turno ESTRITO no `pv_session_mirror`; `PlayPcm` com rate
  converter próprio, último frame parcial reproduzido e marcador de
  produtor na condição de drained; task própria e persistente do
  `pv_audio` (feedback → drena → voz → sinaliza); job composto `Turn` no
  worker com UMA tentativa HTTP e re-hidratação fora do job; modo
  `Response` na tela de câmera com dois terminais de saída (voz E
  hidratação); só correct-ding/wrong-neutral na F3; export provisório
  mantido até a F9.
