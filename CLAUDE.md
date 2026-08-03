# Diretrizes do projeto XiaoZhi

## Project

XiaoZhi is an ESP-IDF C/C++ voice-assistant firmware supporting many chips, boards, displays, audio devices, and network transports. A build selects exactly one board implementation.

Use ESP-IDF v6.0.2 when possible. IDF 5.5.x is retained only for documented legacy boards.

## Architecture

- `main/application.*`: main event loop, protocol lifecycle, and high-level behavior.
- `main/device_state_machine.*`: legal runtime state transitions.
- `main/boards/common/`: board interfaces and reusable hardware/network helpers.
- `main/boards/**/`: board-specific pins, initialization, and build variants.
- `main/audio/`: codecs, audio tasks, engines, wake words, and queues.
- `main/protocols/`: transport-neutral API plus WebSocket and MQTT/UDP.
- `main/display/` and `main/led/`: reusable UI implementations.
- `main/mcp_server.*`: common device-side MCP tools and dispatch.
- `main/Kconfig.projbuild`: board and feature configuration.
- `main/CMakeLists.txt`: source, board, locale, font, and asset selection.
- `scripts/release.py`: canonical board/variant build entry point.

Read the closest existing implementation before adding a new one. Prefer the narrowest owning layer; do not put board-specific behavior into core modules.

## Required Rules

- Preserve unrelated worktree changes and keep patches focused.
- A build must export exactly one board factory through `DECLARE_BOARD(...)`.
- Never alter an existing board's pins to support different hardware. Add a uniquely named board or release variant; board identity affects OTA compatibility.
- Core code depends on `Board` interfaces, never a concrete board class or board `config.h`.
- Treat camera, backlight, display, LED, battery, and similar capabilities as optional.
- Change runtime state through `Application::SetDeviceState()` and the state machine.
- Callbacks may run outside the main task. Schedule application mutations with `Application::Schedule()` or event bits.
- Do not block the main event loop or audio tasks. Avoid unbounded queues and repeated large allocations in audio paths.
- Keep shared message semantics in `Protocol`; verify both transports when changing its contract.
- Validate network input and preserve `cJSON` ownership. NVS keys are persistent API and require migration when changed.
- Guard target-specific features with Kconfig/component rules. Do not assume every target has PSRAM or S3/P4 resources.
- Do not manually edit generated/vendor output: `build/`, `releases/`, `managed_components/`, `components/`, `sdkconfig*`, `main/assets/lang_config.h`, or generated mmap headers.
- Format only touched C/C++ files with the repository `.clang-format`; avoid unrelated mass formatting.

## Boards and Configuration

Board selection is a coupled chain:

`config.json` -> `scripts/release.py` -> `main/Kconfig.projbuild` -> `main/CMakeLists.txt` -> board source and `config.h`.

When adding a board or variant, update every relevant link in that chain. Include a unique board identity, correct chip target, flash/partition settings, exactly one `DECLARE_BOARD`, and board documentation. Follow `docs/custom-board.md`.

## Commands

Source the intended ESP-IDF environment first:

```sh
source /path/to/esp-idf/export.sh
idf.py --version
```

```sh
# Discover exact board and variant names
python3 scripts/release.py --list-boards

# Canonical variant build
python3 scripts/release.py <board-directory> --name <variant-name>

# Host-side release tests
python3 -m unittest discover -s scripts/tests -v

# Format/check touched files
clang-format -i <files>
clang-format --dry-run -Werror <files>
```

The release script changes local `sdkconfig` and build state. Do not assume the build directory still represents a previous target.

## Validation

- Board-only change: build affected variants and smoke-test changed hardware.
- Core, common-board, audio, protocol, display, dependency, Kconfig, or CMake change: run host tests and build representative affected chip/network paths.
- Protocol changes: verify WebSocket and MQTT/UDP when shared behavior changes.
- Audio changes: verify capture, playback, wake/VAD, interruption, reconnect, and applicable AEC modes.
- UI/assets changes: verify applicable no-display/OLED/LVGL paths and partition size.
- Always report what was tested and what still needs physical hardware. A successful build is not hardware validation.

## Authoritative Documentation

- Overview and SDK policy: `README.md`
- SDK compatibility: `docs/esp-idf-6-migration.md`
- Board guide: `docs/custom-board.md`
- Audio design: `main/audio/README.md`
- Code style: `docs/code_style.md`
- Protocols: `docs/websocket.md`, `docs/mqtt-udp.md`, `docs/mcp-protocol.md`
- CI matrix: `.github/workflows/build.yml`

Keep detailed or fast-changing information in those files, not here. Add a nested `AGENTS.md` only when a subsystem needs specialized instructions.

# Missão deste fork

Este fork existe para criar, sobre a base XiaoZhi, o firmware do **Professor
Virtual** — tutor de lição de casa por voz e câmera para crianças. O
dispositivo embarcado entrega toda a experiência do usuário (preparação da
lição, tutoria e failsafe/modo adulto); o desktop roda apenas o backend.

- Especificação funcional: `DOCUMENTACAO-APP.md` (raiz deste repo). Consulte-a
  antes de implementar ou alterar jornada, estados ou experiência do cliente.
- Contrato do dispositivo: `docs/professor-virtual/contrato-dispositivo.md`
  (perfil v1.1 aditivo — mídia por URL, WAV, imagem redimensionada,
  idempotência, prepare paginado, token). O firmware implementa o cliente
  desse contrato. Em conflito sobre campos, endpoints ou transporte v1.1, este
  contrato vence a descrição v1 de `DOCUMENTACAO-APP.md`.
- O backend em `/Users/institutorecriare/VSCodeProjects/licao_casa/backend/` tem duas zonas: o **miolo
  pedagógico é intocável** (prompts, vereditos, máquina de estados da sessão,
  failsafe — `gemini.py`, `session_engine.py` e as transições de estado de
  `main.py`); a borda de transporte v1.1 já foi entregue e validada. Durante
  fases do firmware, `licao_casa` é dependência externa somente leitura. Não o
  altere: qualquer nova necessidade de backend exige handoff explícito ao
  proprietário.
- Retry de `POST /api/turn`: proibido sem `request_id`; com `request_id`
  (contrato v1.1), cliente único/serial e cache íntegro, a retransmissão do
  MESMO turno pode processar após falha pré-`processing`, devolver replay 200
  ou 409 fail-safe. Status HTTP isolado, inclusive 502, não autoriza retry
  automático; a garantia é não haver dupla aplicação silenciosa dentro dessas
  premissas. Após 409: descartar ids pendentes, re-hidratar e usar UUID novo.
- Placa alvo: Waveshare ESP32-P4-WIFI6-Touch-LCD-7B.
- Upstream `78/xiaozhi-esp32`: sincronização pull-only; mantenha o trabalho do
  Professor Virtual aditivo (placa própria, módulos novos, Kconfig próprio) em
  vez de editar core compartilhado.
- Ambiente físico já validado na estação Windows: WSL2, USB-C **"USB TO
  UART"**, ponte CH343 em **COM3**, chip ESP32-P4 **v1.3** e variante `7b`.
  Isso não descreve toda sessão: detecte primeiro sistema operacional, shell e
  conexão atuais. Use `powershell.exe`/COM3 e `usbipd` somente naquela estação;
  nunca presuma esses recursos no macOS ou em outro host. Detalhes:
  `docs/professor-virtual/placa-esp32-p4-wifi6-touch-lcd-7b.md`, seção
  "Conexão serial". Flash físico é sempre ação do proprietário.

# Instruções específicas para Claude Code

Durante uma fase iniciada com `/autonomous-phase`, você é o implementador e
orquestrador. Não transfira ao usuário escolhas de biblioteca, nomes, estrutura
de arquivos, estratégia de testes, detalhes de API, tratamento de erros ou
outras decisões operacionais.

O Codex é o decisor operacional e o revisor independente. `AGENTS.md` contém
instruções exclusivas desse papel e não é instrução de implementação para o
Claude. Para toda dúvida material de implementação, consulte
`mcp__codex-council__codex` antes de usar `AskUserQuestion`; trate a resposta
válida como decisão do proprietário, registre-a e continue. Se o Codex indicar
limite protegido, pare e apresente ao proprietário uma pergunta única. O
fallback técnico permanece definido em `.claude/rules/autonomy.md`.

Ao terminar a implementação, solicite uma revisão independente em uma nova
chamada `codex`. Corrija P0/P1 válidos relacionados à fase e P2 que violem
requisito ou critério de aceitação atual. Registre e adie P2 fora do escopo; não
amplie o MVP para eliminá-los. Repita a revisão no máximo duas vezes e não use
a mesma thread do decisor como revisor. P0/P1 remanescentes bloqueiam a fase.
