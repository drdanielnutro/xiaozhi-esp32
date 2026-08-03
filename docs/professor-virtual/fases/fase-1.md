# Fase F1 — Rede, configuração e hidratação (§9.1)

> Regra de ouro: cada checkbox é marcado **no mesmo commit** que conclui a
> task — o estado nunca pode divergir do código. No encerramento da fase,
> atualizar a tabela "Status das fases" do `plano-firmware.md` no commit
> final, após a revisão independente do Codex.

## Objetivo

Rede e hidratação do Professor Virtual: callback de rede próprio já existente
(F0) passa a conviver com `StartNetwork()` real; tela de configuração do PV
(backend URL + `DEVICE_API_TOKEN` via teclado LVGL → NVS namespace exato
`"pv"`); `GET /api/health` periódico (10 s) + indicador de conexão;
hidratação `GET /api/state` + `GET /api/lesson`; roteamento de boot para
telas placeholder (preparação/tutoria/celebração/failsafe) conforme §9.1.

Credencial (contrato v1.1): token **nunca** aparece em UI, log ou mensagem de
erro; **toda** chamada HTTP envia `Authorization: Bearer <token>` ou
`X-Api-Token`; `401` e `503` interrompem o fluxo normal e encaminham à tela
de configuração/erro, nunca à resposta pedagógica.

Spikes do plano: (a) timeout do wrapper `Http` com resposta lenta (fallback
`esp_http_client` direto só se insuficiente); (b) cobertura de acentos pt-BR
da fonte `font_noto_sans_basic_30_4` (análise estática já indica cobertura
U+00A1–U+00FF; validar visualmente na placa).

## Pronto quando

Com backend real na LAN, o dispositivo boota, hidrata e roteia; desconexão
mostra o estado correto. Builds PV + 7b original verdes; testes host passam;
decisões registradas no decision-log. **Exige placa em mãos (flash — ação do
dono).**

## Pendências físicas (hardware)

- Flash da variante F1 e validação na placa real com backend na LAN:
  boot → conexão Wi-Fi → hidratação → roteamento; desconexão mostra estado
  correto (ação do proprietário).
- Spike (a): comportamento do timeout do wrapper `Http` com resposta
  lenta/queda do backend, observado na placa.
- Spike (b): confirmação visual dos acentos pt-BR na tela.
- Exercício real de `401`/`503` com `DEVICE_API_TOKEN` configurado no
  backend (pendência herdada da F0).

## Tasks

- [x] T1 — Decisões estruturais da F1 via Codex (teclado LVGL, provisão do
      token, condução da `DeviceStateMachine`, estratégia de timeout HTTP)
      · pronto quando: decisões registradas no decision-log.
- [x] T2 — `pv_settings` (NVS `"pv"`: `backend_url`, `api_token`) +
      `CONFIG_LV_USE_KEYBOARD=y` nas variantes PV do `config.json`
      · pronto quando: build PV verde com a flag presente no sdkconfig
      gerado e módulo usado pelo `PvApp`.
- [x] T3 — `pv_backend_client` (token em toda chamada, `SetTimeout`,
      `GET /api/health`, `GET /api/state`, `GET /api/lesson`, resolução de
      URL relativa à base, parse cJSON, taxonomia de erros: ok / rede /
      HTTP-4xx-5xx / 401 / 503 / JSON inválido) + `pv_session_mirror`
      (espelho RAM de state/lesson + decisão de rota §9.1)
      · pronto quando: build verde; parse e rota validados contra os JSONs
      reais do contrato (§7.2/§7.3, incl. `no_session`, `invalid_state`,
      `no_lesson`, ausência de `status` no sucesso da lição).
- [ ] T4 — Boot de rede real: `SetDeviceState(kDeviceStateStarting)` +
      `StartNetwork()`; tela de configuração do PV (backend URL + token,
      teclado LVGL, token mascarado, gravação em NVS `"pv"`) acessível
      quando falta configuração e em `401`/`503`; tela de status para o modo
      de configuração Wi-Fi do `WifiBoard`
      · pronto quando: build verde; fluxo de telas e transições de estado
      legais na `DeviceStateMachine`.
- [ ] T5 — Hidratação + roteamento + health: `GET /api/state` +
      `GET /api/lesson` após rede conectada; roteamento para telas
      placeholder (preparação/tutoria/celebração/failsafe) conforme §9.1;
      `GET /api/health` periódico de 10 s + indicador de conexão;
      desconexão mostra estado correto e re-hidrata ao voltar
      · pronto quando: build verde; lógica de rota cobre os quatro destinos
      e os estados de erro.
- [ ] T6 — Regressão e medição: build `esp32-p4-wifi6-touch-lcd-7b-professor-virtual`
      + build `esp32-p4-wifi6-touch-lcd-7b` original + testes host
      (`python3 -m unittest discover -s scripts/tests`) + tamanho do binário
      vs OTA 4 MB registrado
      · pronto quando: tudo verde e medição nas notas.
- [ ] T7 — Validação física com backend real na LAN (flash pelo dono):
      boot → hidrata → roteia; desconexão correta; spikes (a) e (b)
      observados · pronto quando: confirmado pelo proprietário na placa.

## Contexto de retomada

(vazio)

## Notas da fase

### T1 — Decisões estruturais (2026-08-03, Codex thread 019fc8cc)

- **F1-Keyboard (1a):** `CONFIG_LV_USE_KEYBOARD=y` só nas variantes PV via
  `config.json`.
- **F1-Token (2a):** provisão pela tela de configuração touchscreen → NVS
  `"pv"`; `lv_textarea_set_password_show_time(..., 0)` obrigatório (default
  LVGL revela o último caractere por 1500 ms); campo volta vazio ao reabrir;
  NVS nunca lido de volta para a UI; token proibido em log/erro/dump.
- **F1-StateMachine (3b):** Starting → Activating (hidratação) → Idle (após
  hidratar com sucesso); estado fino do PV em enum próprio; transições só na
  task principal do PV (callbacks apenas sinalizam). Correção factual do
  Codex: reentrada no config Wi-Fi é legal a partir de Idle
  (`wifi_board.cc:215`); parar em Starting quebraria após o primeiro config
  mode.
- **F1-HttpTimeout (4a):** `Http::SetTimeout` por requisição (health 5 s,
  state/lesson 15 s); fallback `esp_http_client` só com falha demonstrada no
  spike físico (roteiro: conexão lenta, atraso pré-headers, corpo
  interrompido, backend indisponível, recuperação pós-timeout).

### T2 — pv_settings + teclado LVGL (2026-08-03)

- `pv_settings.h/.cc`: namespace NVS `"pv"`, chaves `backend_url` (com
  normalização de espaços/barra final) e `api_token` (nunca logado; só
  presença). `PvApp::Initialize` loga o estado de provisão.
- `CONFIG_LV_USE_KEYBOARD=y` apendado às duas variantes PV no `config.json`;
  sdkconfig gerado confirma a flag. Binário: 2.722.800 bytes (antes:
  2.719.136) — folga de ~35% na OTA de 4 MB mantida.
- Armadilha de ambiente registrada: builds fora do `export.sh` oficial devem
  exportar `ESP_IDF_VERSION=6.0` (major.minor). Com `6.0.2`, o
  `orsource "Kconfig.idf_v$ESP_IDF_VERSION.in"` do `esp_wifi_remote` não
  resolve, os símbolos `WIFI_RMT_*` somem do sdkconfig e o build quebra em
  `wifi_manager.cc`.

### T3 — pv_backend_client + pv_session_mirror (2026-08-03, subagente opus)

- `pv_session_mirror` é PURO (só C++ padrão + cJSON) e testado no host:
  `host_test/run.sh` → **122 verificações, 0 falhas** (com ASan/UBSan),
  cobrindo os três formatos de state, os dois de lesson, rotas §9.1 e corpos
  inválidos. Mapas do contrato viram vetores de pares para preservar a ordem
  do JSON (detecção de avanço da F5 compara posições).
- `pv_backend_client`: um `Http` novo por requisição (`CreateHttp(3)`),
  `SetTimeout` 5 s (health) / 15 s (state/lesson), `Close()` em todos os
  caminhos, `Authorization: Bearer` em escopo curto (token jamais logado),
  corpo de erro não lido/logado; 200 sem corpo → `NetworkError`
  (recuperável), corpo fora do contrato → `ParseError`.
- Interpretações registradas: sessão identificada por `session_id` string;
  `status` de topo desconhecido → `InvalidState` (remédio já é preparação);
  `completed` é sessão utilizável (vai à celebração antes do failsafe).
- Armadilha de build: o GLOB de `main/CMakeLists.txt` não usa
  `CONFIGURE_DEPENDS` — arquivo novo em `professor_virtual/` exige
  `idf.py reconfigure` (senão o link fica verde sem os objetos novos).

- Estado Git inicial: `ac258c8` (worktree limpo, 2026-08-03).
- Fatos de código levantados na abertura da fase (exploração 2026-08-03):
  - `Http` wrapper (`78__esp-ml307`) tem `SetTimeout` (default 30 s);
    nenhum call site em `main/` o usa ainda; classe concreta na WifiBoard é
    `HttpClient` via `EspNetwork::CreateHttp`.
  - `CONFIG_LV_USE_KEYBOARD=n` vem de `sdkconfig.defaults:64`;
    `lv_buttonmatrix` e `lv_textarea` já habilitados.
  - Fonte `font_noto_sans_basic_30_4` cobre U+0020–U+007E e U+00A1–U+00FF
    (acentos pt-BR inclusos) — spike (b) tende a passar; confirmar na placa.
  - `WifiBoard::StartNetwork` exige `DeviceStateMachine` fora de
    `kDeviceStateUnknown` para o caminho de config mode
    (`wifi_board.cc:171` exige transição legal para `WifiConfiguring`;
    `EnterWifiConfigMode` checa o estado em `wifi_board.cc:237`).
  - Hidratação: §7.2/§7.3 — `state` pode ser `no_session`/`invalid_state`;
    lição detectada pela presença de `lesson_id` (sem campo `status`).
