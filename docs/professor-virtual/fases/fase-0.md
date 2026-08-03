# Fase F0 — Fundações e prova do build

> Regra de ouro: cada checkbox é marcado **no mesmo commit** que conclui a
> task — o estado nunca pode divergir do código. No encerramento da fase,
> atualizar a tabela "Status das fases" do `plano-firmware.md` no commit
> final, após a revisão independente do Codex.

## Objetivo

Fundações do Professor Virtual sobre a base XiaoZhi: Kconfig
(`CONFIG_PROFESSOR_VIRTUAL`) + CMake condicional + gate em `main/main.cc` +
variantes `professor-virtual-7b`/`professor-virtual-7b-p4x` no `config.json`
da família Waveshare P4 + esqueleto `PvApp` (boot → tela LVGL própria "PV" +
versão). Smoke test de interoperabilidade por `curl` contra o backend real
usando o perfil v1.1 (WAV PCM mono/16 kHz, `request_id`, `media=url`,
`audio_format=wav`, `image_max_px=1280`, token), confirmando resposta e mídia
por `audio_url`/`image_url`. Qualquer falha do smoke test interrompe a F0
para diagnóstico — sem troca de formato e sem fallback acionável.

## Pronto quando

`release.py` builda `esp32-p4-wifi6-touch-lcd-7b-professor-virtual` **e** a variante
`esp32-p4-wifi6-touch-lcd-7b` original continua buildando (regressão);
binário cabe na OTA de 4 MB (medir; plano B: REMOVE_ITEM dos fontes do
assistente); testes host passam; decisões registradas no decision-log.

## Pendências físicas (hardware)

- Flash e boot na placa real (primeiro flash — ação do dono; revela revisão
  do chip → variante `7b` vs `7b-p4x`).
- Verificação visual da tela "PV" + versão no display físico.

## Tasks

- [x] T1 — Kconfig `PROFESSOR_VIRTUAL` + gate `#if CONFIG_PROFESSOR_VIRTUAL`
      em `main/main.cc` · pronto quando: com a opção desligada, o firmware
      compila idêntico ao atual; com ela ligada, `main.cc` desvia para o
      entrypoint do PV.
- [x] T2 — Esqueleto `main/professor_virtual/` (`pv_app.cc/.h`,
      `pv_strings.h` mínimo) + bloco condicional no `main/CMakeLists.txt` ·
      pronto quando: `PvApp::Start()` inicializa board/display e mostra tela
      LVGL própria "Professor Virtual" + versão, mantendo `WifiBoard`
      funcional (callback de rede próprio registrado, sem ativar o fluxo
      assistente).
- [x] T3 — Variantes `esp32-p4-wifi6-touch-lcd-7b[-p4x]-professor-virtual` no
      `config.json` da família `main/boards/waveshare/esp32-p4-wifi6-touch-lcd/`
      · pronto quando: `release.py --list-boards` lista as duas variantes com
      as mesmas flags da 7b/7b-p4x + `CONFIG_PROFESSOR_VIRTUAL=y`.
      (Nomes retificados pela decisão Q2a-retificada de 2026-08-03.)
- [x] T4 — Build `esp32-p4-wifi6-touch-lcd-7b-professor-virtual` via `release.py` + medição do binário
      vs partição OTA de 4 MB · pronto quando: build verde e tamanho medido
      registrado nas notas (plano B REMOVE_ITEM só se estourar).
- [x] T5 — Regressão: build da variante `esp32-p4-wifi6-touch-lcd-7b`
      original + `python3 -m unittest discover -s scripts/tests` · pronto
      quando: build verde e testes host passando.
- [x] T6 — Smoke test de interoperabilidade por `curl` contra o backend real
      (perfil v1.1 completo, WAV PCM mono/16 kHz na subida, token; validar
      eco de `request_id`, `audio_base64`/`image_base64` vazios, download de
      `audio_url`/`image_url` com MIME/extensão corretos, WAV finito
      RIFF/data coerentes) · pronto quando: turno 200 com mídia validada;
      falha interrompe a F0 para diagnóstico.

## Contexto de retomada

(vazio)

## Notas da fase

### T4 — Build PV (2026-08-03)

- `release.py` fim a fim verde:
  `releases/v2.4.0_waveshare-esp32-p4-wifi6-touch-lcd-7b-professor-virtual.zip`.
- `xiaozhi.bin`: **2.717.184 bytes** no build da T4; **2.719.136 bytes** após a
  correção do P1 da revisão — partição de app 4 MB (0x400000), ~35% livres. Plano B (REMOVE_ITEM dos
  fontes do assistente) não foi necessário.
- `pv_app.cc.obj` presente no build; `CONFIG_PROFESSOR_VIRTUAL=y` no sdkconfig
  gerado.

### T5 — Regressão (2026-08-03)

- `release.py` da variante original verde:
  `releases/v2.4.0_waveshare-esp32-p4-wifi6-touch-lcd-7b.zip`.
- Testes host: `python3 -m unittest discover -s scripts/tests` → 8 testes OK
  (rodados após T3 e novamente após T4/T5).

### T6 — Smoke test v1.1 (2026-08-03, backend em 127.0.0.1:8001)

- Porta 8000 estava ocupada por outro app do proprietário (`app.py`, intocado);
  backend subiu em 8001 via venv próprio do `licao_casa`.
- Preparação paginada v1.1: `prepare/start` → `prepare/page` (index 0,
  `data/images/page_1.jpg`) → `prepare/finish` = `ready` (lição
  `883faf42-d167-465e-b004-b22ae2053888`, sessão nova ativa).
- Turno: WAV de fala pt-BR sintetizada (`say -v Luciana` → afconvert PCM s16le
  mono/16 kHz, ~5,9 s) + perfil completo (`request_id` UUID v4, `media=url`,
  `audio_format=wav`, `image_max_px=1280`). **HTTP 200 em 14,6 s.**
- Validações bloqueantes todas OK: eco do `request_id`;
  `audio_base64`/`image_base64` vazios; `audio_url`/`image_url` relativos com
  padrão `turn_<hex32>_(audio|image)`; MIME `audio/wav`+`.wav` e
  `image/jpeg`+`.jpg`.
- WAV baixado: 552.770 bytes físicos; RIFF ChunkSize 552.762 (= físico − 8);
  `data` 552.726 (= PCM real); PCM s16le mono 16 kHz; ~17,3 s — **arquivo
  finito** conforme o contrato. JPEG: 1280×698, 132.084 bytes — respeita
  `image_max_px=1280`.
- Token: loopback isento (decisão SmokeToken 2026-08-03); exercício real de
  401/503 e do `DEVICE_API_TOKEN` fica para a F1.

### Revisão independente (2026-08-03)

- Rodada 1 (Codex effort high, thread 019fc7aa): 1×P1, 1×P2, nenhum P0.
- P1 (PvApp sem callback de rede próprio, exigido pelo critério T2/Q1a):
  **corrigido** — `RegisterNetworkCallback()` em `PvApp::Initialize()` com
  handler próprio (status na tela de boot sob `DisplayLockGuard`);
  `DeviceStateMachine` fica em `kDeviceStateUnknown` na F0 por decisão
  registrada (StartNetwork só na F1; BOOT button inerte por design na F0 —
  board file fora da lista fechada).
- P2 (testes host sem paridade das variantes PV; build `7b-p4x-professor-virtual`
  não executado): **adiado com registro** para F9/endurecimento; build p4x sob
  demanda (placa do dono é rev v1.3 → variante 7b).

- Estado Git inicial: `1de736a` (worktree limpo, 2026-08-03).
- ESP-IDF v6.0.2 localizado em `~/.espressif/v6.0.2/esp-idf` (ativação via
  `~/.espressif/tools/activate_idf_v6.0.2.sh` + `IDF_PATH`/`PATH` manuais).
- 2026-08-03 — Q2a retificada (Codex 019fc773): nomes das variantes ganham o
  prefixo do leaf da placa por exigência do release.py.
- Backend (dependência somente leitura): `/Users/institutorecriare/VSCodeProjects/licao_casa/backend/`.
