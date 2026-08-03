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

`release.py` builda `professor-virtual-7b` **e** a variante
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
- [ ] T2 — Esqueleto `main/professor_virtual/` (`pv_app.cc/.h`,
      `pv_strings.h` mínimo) + bloco condicional no `main/CMakeLists.txt` ·
      pronto quando: `PvApp::Start()` inicializa board/display e mostra tela
      LVGL própria "Professor Virtual" + versão, mantendo `WifiBoard`
      funcional (callback de rede próprio registrado, sem ativar o fluxo
      assistente).
- [ ] T3 — Variantes `professor-virtual-7b` e `professor-virtual-7b-p4x` no
      `config.json` da família `main/boards/waveshare/esp32-p4-wifi6-touch-lcd/`
      · pronto quando: `release.py --list-boards` lista as duas variantes com
      as mesmas flags da 7b/7b-p4x + `CONFIG_PROFESSOR_VIRTUAL=y`.
- [ ] T4 — Build `professor-virtual-7b` via `release.py` + medição do binário
      vs partição OTA de 4 MB · pronto quando: build verde e tamanho medido
      registrado nas notas (plano B REMOVE_ITEM só se estourar).
- [ ] T5 — Regressão: build da variante `esp32-p4-wifi6-touch-lcd-7b`
      original + `python3 -m unittest discover -s scripts/tests` · pronto
      quando: build verde e testes host passando.
- [ ] T6 — Smoke test de interoperabilidade por `curl` contra o backend real
      (perfil v1.1 completo, WAV PCM mono/16 kHz na subida, token; validar
      eco de `request_id`, `audio_base64`/`image_base64` vazios, download de
      `audio_url`/`image_url` com MIME/extensão corretos, WAV finito
      RIFF/data coerentes) · pronto quando: turno 200 com mídia validada;
      falha interrompe a F0 para diagnóstico.

## Contexto de retomada

(vazio)

## Notas da fase

- Estado Git inicial: `1de736a` (worktree limpo, 2026-08-03).
- ESP-IDF v6.0.2 localizado em `~/.espressif/v6.0.2/esp-idf`.
- Backend (dependência somente leitura): `/Users/institutorecriare/VSCodeProjects/licao_casa/backend/`.
