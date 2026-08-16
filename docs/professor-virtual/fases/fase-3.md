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
- [ ] T4 — `pv_backend_client`: perfil comum de turno v1.1 (`request_id`
      UUID v4 via esp_random; `PostTurnPhoto` multipart chunked com
      `session_id` + `image` + campos v1.1; timeout ≥120 s) + parse da
      resposta (eco de `request_id`, `audio_base64`/`image_base64` vazios,
      veredicto/posição) + `DownloadMedia` com token e validação de
      MIME/extensão (`audio/wav`+`.wav`, `image/jpeg`+`.jpg`) · pronto
      quando: parse coberto por teste host; build PV verde.
- [ ] T5 — `PvWorker`: job de turno (POST + downloads + resultado com
      geração de conectividade) e re-hidratação pós-turno; regra de erro:
      após QUALQUER erro com resposta do servidor (409/502/4xx/5xx),
      re-consultar estado antes de mostrar qualquer coisa; após 409,
      descartar ids pendentes, re-hidratar e só então permitir turno novo
      com UUID novo; nunca reproduzir 409/502 como resposta pedagógica;
      sem retry automático · pronto quando: build PV verde e fluxo de
      estados revisado contra o contrato.
- [ ] T6 — PvApp + UI: fluxo completo na tela (revisão → "Enviar" →
      processando → resposta: imagem do tutor + voz + som de feedback →
      re-hidratação → volta ao preview/rota); comandos bloqueados fora de
      `idle` (mini-máquina da F3); erros com toast/telas conforme §9.7
      (401/503 → configuração; nunca resposta pedagógica) · pronto quando:
      build PV verde e caminho de cada erro rastreável no código.
- [ ] T7 — Regressão e medição: build
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

(vazio)

## Notas da fase

- Estado Git no início da fase: `01fa57e` (worktree limpo), 2026-08-16.
- Placa desconectada durante T1–T7 (combinado com o proprietário em
  2026-08-16); conexão só na T8.
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
