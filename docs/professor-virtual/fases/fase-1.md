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

- ~~Flash + validação com backend na LAN~~ **RESOLVIDO (2026-08-04):**
  boot → Wi-Fi → hidratação → rota Preparação sem nenhum toque; queda do
  backend com rota no ar → badge "servidor: desconectado" e tela mantida;
  volta do backend → re-hidratação e badge "conectado" sozinhos (§9.7).
- ~~Spike (b) acentos pt-BR~~ **RESOLVIDO (2026-08-04):** todas as telas
  pt-BR legíveis com acentuação correta na fonte da 7B.
- ~~401/503 reais~~ **RESOLVIDO (2026-08-04):** backend sem token → 503 →
  tela de config; token errado → 401 → "Token recusado"; token certo →
  hidratação completa (log do uvicorn confirmando cada caso).
- Spike (a) **parcial → F8:** timeouts exercitados com backend ausente/IP
  morto (recuperação ok); resposta lenta artificial e roteiro completo da
  ressalva F1-HttpTimeout ficam para a F8.

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
- [x] T4 — Boot de rede real: `SetDeviceState(kDeviceStateStarting)` +
      `StartNetwork()`; tela de configuração do PV (backend URL + token,
      teclado LVGL, token mascarado, gravação em NVS `"pv"`) acessível
      quando falta configuração e em `401`/`503`; tela de status para o modo
      de configuração Wi-Fi do `WifiBoard`
      · pronto quando: build verde; fluxo de telas e transições de estado
      legais na `DeviceStateMachine`.
- [x] T5 — Hidratação + roteamento + health: `GET /api/state` +
      `GET /api/lesson` após rede conectada; roteamento para telas
      placeholder (preparação/tutoria/celebração/failsafe) conforme §9.1;
      `GET /api/health` periódico de 10 s + indicador de conexão;
      desconexão mostra estado correto e re-hidrata ao voltar
      · pronto quando: build verde; lógica de rota cobre os quatro destinos
      e os estados de erro.
- [x] T6 — Regressão e medição: build `esp32-p4-wifi6-touch-lcd-7b-professor-virtual`
      + build `esp32-p4-wifi6-touch-lcd-7b` original + testes host
      (`python3 -m unittest discover -s scripts/tests`) + tamanho do binário
      vs OTA 4 MB registrado
      · pronto quando: tudo verde e medição nas notas.
- [x] T7 — Validação física com backend real na LAN (flash pelo dono):
      boot → hidrata → roteia; desconexão correta; spikes (a) e (b)
      observados · pronto quando: confirmado pelo proprietário na placa.
      (Concluída em 2026-08-04; spike (a) coberto parcialmente — resto → F8.)

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

### T4 — Boot de rede + tela de configuração (2026-08-03, subagente opus)

- PvApp virou consumidor de fila FreeRTOS (`PvEvent` POD, 8 slots): callbacks
  de rede (task Wi-Fi) e Salvar (task LVGL) só postam; telas e
  DeviceStateMachine mudam apenas no `Run()` (decisão 3b). Fase fina em
  `PvPhase{Booting,WifiConnecting,WifiConfigMode,AwaitingBackendConfig,
  Online,Offline}`.
- `Unknown→Starting` antes de `StartNetwork()`; `Starting→WifiConfiguring`
  continua por conta do WifiBoard (ambas legais). Activating/Idle ficam
  para a T5.
- `ui/`: `pv_ui_theme` (paleta + fonte com fallback `montserrat_14` para os
  símbolos do teclado, ausentes na Noto Sans 30), `pv_status_screen`,
  `pv_config_screen` (URL pré-preenchida; token `password_mode` +
  `password_show_time=0`, campo sempre vazio, salvar com token vazio
  preserva o existente, `ScrubString` nas cópias em RAM).
- Modo config Wi-Fi: instruções com `GetApSsid()`/`GetApWebUrl()` (sem
  hardcode). T5 abre a tela de config via
  `RequestConfigScreen(PvConfigReason::Unauthorized|Unavailable)` (enum
  fechado — sem caminho para vazar token/detalhe do servidor).
- Observação registrada: `WifiBoard::StartWifiConfigMode` enfileira um
  `Application::Schedule` que nunca executa no PV (lambda órfão, 1× por
  entrada em config mode; inofensivo; correção exigiria tocar arquivo
  compartilhado — fora do escopo).
- Validação do orquestrador: host test 122 OK; testes host de release 8 OK;
  build incremental verde com os 3 `.obj` de `ui/` presentes.

### T5 — Hidratação, roteamento e health (2026-08-03, subagente opus)

- `pv_worker`: task FreeRTOS própria (8192 B, prio 2) para o HTTP bloqueante;
  resultado (`PvSessionState`/`PvLesson`) em holder sob mutex com
  transferência de posse via `TakeHydration()/TakeHealth()`; coalescência por
  `atomic<bool>` (tick de 10 s nunca empilha chamadas de 15 s). Worker nunca
  toca LVGL nem DeviceStateMachine.
- Sequência: Activating (task principal) → health → state → lesson →
  `DecideRoute` → Idle → tela da rota. Falha de hidratação PERMANECE em
  Activating (Activating→Starting é ilegal) e re-tenta no tick seguinte;
  401/503 → tela de config (`Unauthorized`/`Unavailable`) e timer parado.
- Gatilhos de re-hidratação: ficar Online configurado; salvar config; 
  reconexão Wi-Fi; health voltando a passar (§9.7).
- `pv_route_text` (puro, testado no host) + `ui/pv_route_screens`: quatro
  placeholders com badge de conexão; tutoria mostra `current_item`/
  `current_tarefa` + título da tarefa do espelho (prova de hidratação real).
- Desconexão com rota carregada: tela permanece, badge vira "desconectado" e
  a rota nunca troca com dado velho; tela técnica offline só antes do
  primeiro roteamento.
- Validação do orquestrador: host test **147 OK** (+25 casos de textos de
  rota, incl. corte UTF-8 sem partir acento); build verde
  (`xiaozhi.bin` 2.779.024 bytes, 34% livres); 10 `.obj` do PV presentes.

### T6 — Regressão e medição (2026-08-03)

- `release.py` PV verde: `releases/v2.4.0_waveshare-esp32-p4-wifi6-touch-lcd-7b-professor-virtual.zip`.
- `release.py` 7b original verde: `releases/v2.4.0_waveshare-esp32-p4-wifi6-touch-lcd-7b.zip`.
- Testes host: `python3 -m unittest discover -s scripts/tests` → 8 OK.
- `xiaozhi.bin` da variante PV: **2.779.024 bytes** (0x2a6790) — 34% livres
  na partição de app de 4 MB. Sem necessidade do plano B (REMOVE_ITEM).

### Revisão independente — rodada 1 (2026-08-03, Codex thread 019fc93f)

5×P1 + 1×P2, nenhum P0. Todas as alegações verificadas no código antes da
correção. **P1 corrigidos:**

1. Token em log DEBUG do wrapper (`http_client.cc:162` loga todos os
   headers): `esp_log_level_set("HttpClient", ESP_LOG_INFO)` no
   `PvApp::Initialize` — defesa em profundidade; o token jamais aparece em
   log mesmo em build de diagnóstico.
2. `ReadAll()` trava com corpo > 8 KiB (produtor pausa a 8 KiB e `ReadAll`
   só drena após EOF): leitura incremental `ReadBody()` em blocos de 1 KiB
   com teto de 256 KiB.
3. `GetStatusCode()` = -1 (timeout/sem resposta) virava `HttpError`:
   status < 100 agora é `NetworkError`.
4. Resultado HTTP obsoleto podia "reviver" Online após desconexão:
   geração de conectividade (`net_generation_`) incrementada em
   desconexão/config-mode, carregada nos pedidos ao worker e conferida em
   `HandleHydrationDone`/`HandleHealthDone` (descarta geração antiga).
5. Parsers frouxos: sessão agora exige `session_id` E `session_status`
   não-vazios (senão `ParseError`); `IsSessionUsable` virou lista fechada
   {"active","completed"} (status desconhecido → preparação; substitui a
   interpretação registrada na T3); lição só aceita `status=="no_lesson"`
   literal (outro status → `ParseError`).

Host test: **155 verificações, 0 falhas** (novos casos das regressões
acima). Riscos que a T7 deve cobrir (do revisor): desconectar com
health/hidratação em voo; lição > 8 KiB; logs em build DEBUG sem token.

### Revisão independente — rodada 2 (2026-08-03, Codex thread 019fc959)

**NO_FINDINGS — fase aprovada.** Correções dos 5 P1 verificadas pelo
revisor; nenhuma regressão nova. Encerramento com a T7 (validação física)
aberta como pendência do proprietário.

### T7 — Validação física (2026-08-03/04, Mac + backend real na LAN)

Além dos cenários do checklist (todos ✓), a validação revelou e corrigiu
dois problemas reais e registrou observações:

1. **Toque invertido 180° no painel 7B** (decisão F1-TouchMirror): GT911
   com origem oposta à do display; driver upstream nunca compensou (toque
   quase não usado pelo assistente). Correção com gate duplo
   PV+7B no board file (upstream binariamente idêntico). Dívida upstream
   registrada (recomendar issue com a 7B original). Validado: teclado
   preciso nos quatro cantos e centro.
2. **Sem caminho manual para a tela de configuração** (decisão
   F1-ConfigGesture): dispositivo ficou preso apontando para IP morto
   quando o roteador reciclou o IP do Mac. Long-press 3 s no indicador de
   servidor (preferência do proprietário) e no rótulo da versão, nas cinco
   telas; `net_generation_++` ao abrir. Exceção registrada para a F6 (PIN
   não pode ser o único caminho de recuperação). Validado no cenário real.
3. **Strings ajustadas na validação:** mensagem do 503 diferenciada do erro
   de rede; badge ganhou prefixo "servidor:" (era ambíguo com o Wi-Fi).
4. **Observação → F8/F3:** hidratação intermitentemente parou na abertura
   da 3ª conexão (`/api/lesson` nem chegou ao uvicorn; 2× em rajada de
   tentativas, autocura no tick de 10 s). Hipótese: esgotamento transitório
   de sockets (TIME_WAIT) no LWIP. Investigar com instrumentação na F8;
   relevante para os downloads de mídia da F3 (mesmo caminho HTTP).
5. Armadilha de ambiente: backend lê o `.env` da RAIZ do `licao_casa`
   (`config.py:32`), não de `backend/.env`. Recomendação ao proprietário:
   reservar IP fixo para o Mac no roteador (IP reciclado foi a causa raiz
   do cenário 2).

**P2 adiado com registro:** `ResolveUrl` aceita URL absoluta de qualquer
origem; sem download de mídia na F1 não há exposição, mas ANTES da F3 o
helper deve restringir URLs absolutas à mesma origem da base (ou rejeitar),
para o header de autenticação nunca viajar a outro host. Gatilho: abertura
da F3.

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
