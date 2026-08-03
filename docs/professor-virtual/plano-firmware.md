# Plano do firmware Professor Virtual (Etapa 2)

> Roadmap aprovado como plano de trabalho; **nenhuma fase foi iniciada**.
> Especificação do produto: `DOCUMENTACAO-APP.md` (contrato §7, mídia §8,
> comportamento §9, sequência B.5). Dossiê do hardware:
> `docs/professor-virtual/placa-esp32-p4-wifi6-touch-lcd-7b.md`.
> Constituição do projeto: `AGENTS.md` (Codex) e `CLAUDE.md` (Claude).

## Contexto e fatos verificados no código

- `main/main.cc:14-28` só inicializa NVS e chama
  `Application::Initialize()+Run()` — é o ponto de troca ideal para um app
  alternativo.
- `AudioService` é independente de Protocol (`main/audio/audio_service.cc`:
  única referência global é `Board::GetAudioCodec()` na L520); expõe
  `ReadAudioData()` (PCM 16 kHz pré-AFE, 2 canais mic+referência na 7B) e
  `PlaySound()` (OGG/Opus embarcado). A reprodução hoje decodifica **só
  Opus** (`audio_service.cc:376-440`).
- `WifiBoard` chama `Application::GetInstance()` diretamente
  (`main/boards/common/wifi_board.cc:171,178-185,212-217,237`) →
  `application.cc` precisa continuar linkado; sem o loop `Run()`,
  `Schedule()/Alert()` não executam (o PV mostra a própria UI de
  configuração de rede).
- Protocol/OTA do assistente só nascem após o evento de rede
  (`main/application.cc:275-355`); se o PV assumir
  `Board::SetNetworkEventCallback` (público), o fluxo assistente nunca
  ativa.
- UI: o chat vive em `lv_screen_active()` criado por `SetupUI()` (chamado
  apenas em `application.cc:64`); sem chamá-lo, a tela fica livre para o
  PV. `LvglAllocatedImage` (`main/display/lvgl_display/lvgl_image.cc:37-46`)
  + `CONFIG_LV_USE_LODEPNG=y` (`sdkconfig.defaults:56`) exibem PNG de
  buffer em RAM; exemplo completo HTTP→RAM→tela em
  `main/mcp_server.cc:244-282`. Lock via `DisplayLockGuard`.
  `MipiLcdDisplay` não redimensiona o cache de imagem do LVGL — o init do
  PV deve fazê-lo.
- HTTP: wrapper `Http` do componente `78/esp-ml307` (`CreateHttp(n)`; canal
  3 é o usado por apps); multipart chunked comprovado em
  `main/boards/common/esp_video.cc:956-1037`; leitura por `Read()`
  streaming ou `ReadAll()`; **nenhum SetTimeout é usado no repo** (risco a
  medir na F1).
- Sons locais: pipeline pronto — `.ogg` embarcado + `AudioService::PlaySound`
  + conversor `scripts/mp3_to_ogg.sh`.
- Strings/fontes: locale `pt-BR` não existe (só `pt-PT`); a fonte da 7B é
  `font_noto_sans_basic_30_4` (cobertura de acentos a validar na F1). Os
  textos do PV vêm da spec (Apêndice A) em header próprio.

## Decisões estruturais (Codex Decision Proxy, 26/07/2026, thread 019fa167)

- **Q1(a) — Seleção do app:** `#if CONFIG_PROFESSOR_VIRTUAL` em
  `main/main.cc` (~4 linhas); `application.cc`/protocols continuam
  compilados mas nunca inicializados; o PV registra o próprio callback de
  rede e mantém a `DeviceStateMachine` em estados compatíveis com o
  `WifiBoard`.
- **Q2(a) — Identidade de build:** novas variantes na família
  `main/boards/waveshare/esp32-p4-wifi6-touch-lcd/` (mesmas flags da 7b +
  `CONFIG_PROFESSOR_VIRTUAL=y`); zero duplicação de pinos.
  *Retificada em 2026-08-03 (F0, thread Codex 019fc773):* `scripts/release.py`
  exige que o nome da variante contenha o leaf do diretório da placa; nomes
  definitivos: `esp32-p4-wifi6-touch-lcd-7b-professor-virtual` e
  `esp32-p4-wifi6-touch-lcd-7b-p4x-professor-virtual` (sem alterar
  `release.py` e sem diretório de placa artificial).
- **Q3(a) — Voz do tutor:** `SUPERSEDED pela Task 12 — contrato v1.1 validado`.
  - *Decisão anterior (histórica, não acionável):* decodificação MP3 no lado
    PV (componente `espressif/esp_audio_codec`, já no manifest) + método
    aditivo `AudioService::PlayPcm(...)`.
  - **Decisão vigente:** o firmware **solicita `audio_format=wav`** e **recebe
    a voz do tutor por `audio_url`** (contrato v1.1). O `pv_audio`
    **interpreta a estrutura RIFF/WAVE percorrendo seus chunks** e **não
    presume PCM em offset fixo de 44 bytes**; **exige PCM s16le, mono,
    16 kHz**; **exige arquivo finito** (RIFF `ChunkSize` = tamanho físico − 8
    e `data` `Subchunk2Size` = PCM real, conforme o contrato); e **entrega o
    PCM a `AudioService::PlayPcm(...)`** (dono único do codec, fila interna de
    playback). **Não existe fallback MP3 para a voz remota.** Fish
    Audio/Itachi é tratado **apenas como evidência empírica do backend**: o
    requisito do firmware permanece **neutro ao provedor**. A dependência
    `esp_audio_codec` pode continuar existindo no upstream, mas **deixa de ser
    requisito da voz do Professor Virtual**.

Decisões de engenharia complementares: gravação em **WAV/PCM 16 kHz mono**
(canal do mic de `ReadAudioData`; **cabeçalho WAV canônico de 44 bytes escrito
pelo próprio firmware na subida** — essa referência vale **somente** para o
arquivo que o firmware cria; ~960 KB por 30 s em PSRAM), caminho de subida já
validado empiricamente contra o backend. **O WAV baixado do backend exige
parser de chunks**, e a premissa dos 44 bytes não se aplica a ele. Sons de UI
convertidos MP3→OGG — **conversão de assets locais, que continua válida**, pois
esses assets **não são a voz remota do tutor**. Textos pt-BR em `pv_strings.h`
próprio (sem mexer no sistema de locales).

> Pendência de registro: ao iniciar a F0, gravar estas decisões no
> `.claude/autonomy/decision-log.jsonl`.

## Arquitetura do código novo

```
main/professor_virtual/            (novo, aditivo)
├── pv_app.cc/.h                   singleton PvApp: init (board/audio/UI), event loop, fases RAM (§9.2)
├── pv_backend_client.cc/.h        contrato de dispositivo v1.1 (token, mídia por URL, idempotência
│                                  fail-safe por request_id); cJSON; timeouts §8.1
├── pv_session_mirror.cc/.h        espelho RAM de state/lesson (§7.2/§7.3); detecção de avanço (§9.4)
├── pv_audio.cc/.h                 gravação WAV para subida; reprodução do WAV/PCM recebido por URL
│                                  (parser RIFF/WAVE → PlayPcm); sons OGG locais
├── pv_camera.cc/.h                preview, captura JPEG, preparação página a página (sem lote
│                                  full-resolution residente em PSRAM)
├── pv_settings.cc/.h              NVS namespace "pv" (backend_url, DEVICE_API_TOKEN etc.)
├── pv_strings.h                   textos pt-BR (Apêndice A da spec)
├── assets_sounds/*.ogg            sons locais convertidos de licao_casa/frontend/public/sounds/
└── ui/                            telas LVGL próprias (lv_screen_load): conexão/config, preparação,
                                   revisão, tutoria, failsafe/PIN, celebração
```

**Toques em arquivos compartilhados com o upstream (lista fechada; qualquer
adição exige justificativa):**

1. `main/main.cc` — gate `#if CONFIG_PROFESSOR_VIRTUAL` (Q1).
2. `main/Kconfig.projbuild` — `config PROFESSOR_VIRTUAL` na zona de features (~L778+).
3. `main/CMakeLists.txt` — bloco condicional: GLOB de `professor_virtual/*.cc` +
   `ui/*.cc`, INCLUDE_DIRS, EMBED dos `.ogg` do PV.
4. `main/boards/waveshare/esp32-p4-wifi6-touch-lcd/config.json` — 2 variantes novas (Q2).
5. `main/audio/audio_service.{h,cc}` — método `PlayPcm` aditivo (Q3 vigente:
   recebe o PCM extraído do WAV baixado por `audio_url`).

Nenhum arquivo ou componente novo entra no escopo por conta da Task 12.

### Perfil comum de turno do dispositivo (`pv_backend_client`)

Todo **turno lógico** do dispositivo (F3, F4 e qualquer turno futuro) envia:

- `request_id`: **UUID v4 novo** por turno lógico;
- `media=url`;
- `audio_format=wav`;
- `image_max_px=1280`;
- `session_id`;
- **token em todas as chamadas** (`Authorization: Bearer <token>` ou
  `X-Api-Token`).

Na resposta **200**, o cliente:

- valida o **eco de `request_id`**;
- exige `audio_base64=""` e `image_base64=""`;
- resolve `audio_url` e `image_url` **relativamente à base do backend**;
- baixa ambas **com o token**;
- valida MIME e extensão (`audio/wav`+`.wav`, `image/jpeg`+`.jpg`);
- reproduz o WAV via **parser RIFF/WAVE por chunks → PCM → `PlayPcm`**;
- exibe o JPEG obtido por `image_url`;
- preserva o **fluxo acoplado** de áudio e imagem.

### Retry e idempotência de `POST /api/turn`

- **Sem `request_id`, retry automático de `/api/turn` é proibido.**
- O UUID identifica **um único turno lógico**; **nunca** reutilizar um UUID
  para outro conteúdo, outra lição ou outro momento da instalação.
- Uma retransmissão conserva **o mesmo UUID, os mesmos campos e os mesmos
  bytes** de mídia.
- Com **cliente único/serial e cache íntegro**, a retransmissão do MESMO turno
  pode:
  - **processar**, quando a falha anterior ocorreu antes do marcador
    `processing`;
  - receber **replay 200**, quando existe resposta `done` válida e replayável;
  - receber **409**, quando o resultado é indeterminado, supersedido ou não
    replayável.
- **Status HTTP isolado, inclusive 502, não autoriza retry automático.** A
  garantia é não haver dupla aplicação silenciosa dentro dessas premissas.
- Após **409**, o dispositivo: descarta os ids pendentes; **não reproduz o 409
  como resposta pedagógica**; re-hidrata `GET /api/state` + `GET /api/lesson`;
  e só então inicia um **turno lógico novo com UUID novo**.
- **Nunca** permitir dupla aplicação pedagógica silenciosa.

## Evidência empírica congelada do contrato v1.1

Registro do que foi **observado** na validação do backend (concluída antes
desta Task 12). Não contém segredo.

- Fish endpoint: `https://api.fish.audio/v1/tts`
- Modelo observado: `s2.1-pro-free`
- Voz observada: Itachi
- Voice id: `c5a6cb585b094dedb241365e7e271973`
- MP3 do fluxo web: mono, 44,1 kHz, 128 kbps
- WAV do dispositivo: PCM s16le mono/16 kHz, arquivo finito
- Preflight: MP3 10,08 s; WAV 8,62 s
- E2E pós-gap-closure: HTTP 200 em 16,1 s
- WAV: 476.980 bytes físicos; RIFF `ChunkSize` 476972; `data`/PCM 476936;
  duração ~14,9 s
- JPEG: 1280×698
- Aprovação humana: 2026-08-02
- Backend final: 295 testes, auditoria independente APPROVED,
  `threats_open: 0`

**Declaração:**

- estes valores são **evidência, não requisitos do firmware**;
- **provedor, modelo e voz podem mudar sem alterar o firmware**;
- o firmware depende **apenas** de contrato, formatos, MIME, endpoints,
  autenticação e regra de retry;
- **não existe fallback MP3 aprovado para a voz remota.**

## Fases (cada uma executável via `/autonomous-phase`, com commit e revisão Codex ao final)

### F0 — Fundações e prova do build *(primeira fase autônoma: pequena e observada)*

Kconfig + CMake + gate no `main.cc` + variantes no `config.json` + esqueleto
`PvApp` (boot → tela LVGL própria "PV" + versão). **Smoke test de
interoperabilidade por `curl`** — não é decisão de formato: o formato já está
decidido e validado pelo contrato v1.1. O smoke test deve: enviar **WAV PCM
mono/16 kHz**; solicitar o **perfil v1.1** (`request_id`, `media=url`,
`audio_format=wav`, `image_max_px=1280`, token); e **confirmar a resposta e a
mídia** recuperada por `audio_url`/`image_url`. **Qualquer falha interrompe a
F0 para diagnóstico** da divergência entre firmware e contrato: **não** se
alterna para Opus/OGG, MP3 ou outro formato e **não** se mantém fallback de
formato acionável. **Pronto quando:** `release.py` builda
`esp32-p4-wifi6-touch-lcd-7b-professor-virtual`
**e** a variante `esp32-p4-wifi6-touch-lcd-7b` original continua buildando
(regressão); binário cabe na OTA de 4 MB (medir; plano B: REMOVE_ITEM dos
fontes do assistente); testes host passam; decisões registradas no
decision-log.

### F1 — Rede, configuração e hidratação (§9.1)

Callback de rede próprio; tela de configuração (backend URL via teclado
LVGL → NVS "pv"); `GET /api/health` periódico (10 s) + indicador;
`GET /api/state` + `GET /api/lesson`; roteamento de boot para telas
placeholder (preparação/tutoria/celebração/failsafe).

**Credencial de dispositivo (contrato v1.1):**

- `DEVICE_API_TOKEN` **provisionado no namespace NVS exato `"pv"`** (o módulo
  continua se chamando `pv_settings`);
- o token **nunca aparece em UI, log ou mensagem de erro**;
- **toda** chamada HTTP envia `Authorization: Bearer <token>` **ou**
  `X-Api-Token: <token>`;
- **`401`** = token ausente/incorreto; **`503`** = backend remoto sem token
  configurado; **ambos interrompem o fluxo normal** e encaminham à tela de
  configuração/erro, **nunca à resposta pedagógica**.

Endpoints que exigem a credencial: `/api/health`, `/api/state`, `/api/lesson`,
`/api/turn`, `/api/media/...`, `/api/prepare/start`, `/api/prepare/page`,
`/api/prepare/finish` e os endpoints adultos usados futuramente.

**Spikes:** (a)
timeout do wrapper `Http` com resposta lenta (se insuficiente → fallback
`esp_http_client` direto no `pv_backend_client`); (b) cobertura de acentos
pt-BR da fonte (fallback: `AddTextGlyphs`/outra fonte da família).
**Pronto quando:** com backend real na LAN, o dispositivo boota, hidrata e
roteia; desconexão mostra o estado correto. **Exige placa em mãos
(primeiro flash — ação do dono).**

### F2 — Câmera

Preview no LVGL (frame → `LvglAllocatedImage` raw/RGB565), captura JPEG
(encoder por hardware quando disponível), ajuste de qualidade/resolução
para página A4 legível (dossiê §7 item 6). **Pronto quando:** foto de
caderno legível salva/exibida; tamanho típico por página medido (alvo:
centenas de KB).

### F3 — Fatia vertical do turno por foto *(o marco central; B.5 item 9)*

`POST /api/turn` multipart (chunked, timeout ≥120 s) contendo **a imagem JPEG
e `session_id`** — **nenhum arquivo de áudio é enviado na F3** —, aplicando o
**perfil comum de turno v1.1** descrito acima (`request_id` UUID v4 novo,
`media=url`, `audio_format=wav`, `image_max_px=1280`, token em todas as
chamadas). Parse da resposta com validação do eco de `request_id`; download de
`image_url` (JPEG exibido na tela) e de `audio_url` (WAV interpretado por
**parser RIFF/WAVE por chunks** → PCM → `PlayPcm`); sons locais de feedback
(OGG); re-hidratação pós-turno conforme o contrato. **Não há decodificação
base64 de mídia** e **não há decoder nem fallback MP3 para a voz**. Erros
502/409 seguem a **regra de idempotência** acima (§7.5 + contrato v1.1).
**Pronto quando:** ciclo completo mostrar
tarefa→foto→resposta com voz+imagem funciona contra o backend real; RAM
medida no pico do turno.

### F4 — Turno por áudio

Gravação ≤30 s com contador e auto-stop (§9.3). `POST /api/turn` multipart
contendo **o WAV PCM mono/16 kHz e `session_id`** — **nenhuma imagem é enviada
na F4** —, aplicando o **mesmo perfil comum v1.1** (`request_id`, `media=url`,
`audio_format=wav`, `image_max_px=1280`, token). A **resposta continua trazendo
áudio e imagem por URL** (fluxo acoplado preservado). Bloqueio de comandos fora
de `idle`. O caminho de subida WAV **já foi validado empiricamente** contra o
backend; **não existe fallback WebM/Opus/OGG para a subida**. Erros 502/409
seguem a regra de idempotência acima. **Pronto quando:** turnos por voz
funcionam fim a fim.

### F5 — Máquina de fases completa (§9.2–§9.5)

Transições, replay local, inatividade 120 s (1×/tarefa), entretenimento em
loop com troca aos 15 s, watchdog de 10 s da voz, toasts 5 s, detecção de
avanço rigorosa (fim do áudio **e** re-hidratação; cabeçalho segura a
tarefa concluída). **Pronto quando:** checklist da §9.3 demonstrável.

### F6 — Failsafe e modo adulto (§9.6)

Overlay bloqueante (nenhum comando da criança acessível), 3 telas, PIN via
`/api/adult/verify`, resolve via `/api/adult/resolve`, saída só após
re-hidratação confirmar; avanço sem som de celebração. **Pronto quando:**
fluxo forçado (3 erros por foto no backend real) passa.

### F7 — Preparação da lição (§9.8)

Captura página a página (≤20) com **upload incremental**, substituindo o envio
em lote único:

1. `POST /api/prepare/start` → `upload_id`;
2. para cada página, `POST /api/prepare/page` com `upload_id`, `index` (0–19) e
   o arquivo **JPEG/PNG ≤10 MB**;
3. **liberar a imagem full-resolution** assim que o upload é confirmado;
4. manter em memória **somente metadados e a miniatura** necessária à revisão;
5. `POST /api/prepare/finish` com `upload_id`;
6. em **`illegible`**, o staging é **mantido**: reenviar **somente os índices
   ilegíveis** por `/api/prepare/page`;
7. chamar **`finish` novamente**;
8. em **`ready`**, o staging está encerrado.

Preservados: excluir/refazer no mesmo índice, ilegíveis marcadas, revisão em
acordeões + chime e recomeçar com confirmação. Estratégia de RAM: **nunca
manter o lote completo de imagens full-resolution em PSRAM** — apenas **uma
página full-resolution por vez**, com pico de RAM/PSRAM **medido** (risco do
dossiê §5). **Pronto quando:** lição real de N páginas preparada no dispositivo
e tutoria iniciada.

### F8 — Robustez (§9.7, §10)

Matriz de erros completa (com/sem resposta, backend fora, IA fora→502),
reconexão + re-hidratação, recuperação pós-reboot no meio de failsafe,
estabilidade de memória em sessão longa (heap/PSRAM tracking), revisão
final das chaves NVS. **Pronto quando:** roteiro de falhas induzidas passa
sem travar nem vazar.

### F9 — Endurecimento e entrega

Revisão independente completa (Codex, effort high, 2 rodadas),
`clang-format` nos arquivos tocados, atualização de `docs/professor-virtual/`,
checklist final de validação física com o dono, tag de versão.
**Pronto quando:** sem P0/P1; relatório final entregue.

## Riscos mapeados → onde se resolvem

| Risco | Fase | Mitigação/plano B |
|---|---|---|
| Timeout do wrapper `Http` para turno de dezenas de segundos | F1 | Medir; fallback `esp_http_client` direto |
| Fonte sem acentos pt-BR | F1 | `AddTextGlyphs`/fonte maior da família |
| Divergência de integração no smoke test do WAV já aprovado | F0 (curl) | Interromper e diagnosticar a divergência entre firmware e contrato; sem troca de formato e sem fallback acionável |
| Parser RIFF/WAVE do WAV recebido rejeita/interpreta mal o arquivo | F3 | Percorrer os chunks e rejeitar layout inválido; **nunca** usar offset fixo de 44 bytes no WAV recebido |
| Retransmissão de `/api/turn` aplicando o turno duas vezes | F3/F4 | Sem `request_id` não há retry automático; com o MESMO UUID vale a regra de idempotência (processa / replay 200 / 409); após 409, descartar ids, re-hidratar e usar UUID novo |
| Binário > 4 MB com código morto do assistente | F0 | REMOVE_ITEM condicional dos fontes do assistente |
| RAM da preparação página a página | F7 | Somente uma página full-resolution por vez; liberar a página após o upload; manter só metadados e miniaturas; medir pico de RAM/PSRAM |
| Eco na gravação (caminho pré-AFE sem AEC) | F4 | Nada toca durante a gravação (spec); plano B: expor tap pós-AFE (toque aditivo) |

## Processo transversal

- Cada fase: primeiro ato é criar `fases/fase-N.md` (do TEMPLATE) com o
  checklist de tasks; cada task concluída marca o checkbox **no mesmo
  commit**; decisões operacionais via Codex Decision Proxy (registradas no
  decision-log); revisão independente do Codex ao final (máx. 2 rodadas);
  relatório com "o que ainda exige hardware físico"; **atualização da
  tabela "Status das fases"** no commit de encerramento.
- Regressão obrigatória por fase: build da variante PV **e** da variante
  XiaoZhi 7b original + `python3 -m unittest discover -s scripts/tests`.
- **Contrato canônico:** `docs/professor-virtual/contrato-dispositivo.md`. O
  **miolo pedagógico do backend é intocável**; a **borda de transporte** aceita
  **somente mudanças aditivas aprovadas**, preservando o comportamento v1
  (frontend web e testes existentes intactos). O executor do firmware **não
  modifica autonomamente o `licao_casa`**: qualquer necessidade de backend vira
  **handoff explícito ao proprietário**.
- **Regra de retry/idempotência** (seção "Retry e idempotência" acima) vale em
  toda chamada a `/api/turn`, em qualquer fase: nunca há dupla aplicação
  pedagógica silenciosa, e um 409 nunca é reproduzido como turno.
- Push e flash físico: sempre ações do proprietário.

## Pré-requisitos do proprietário (podem andar em paralelo)

1. Confirmar câmera OV5647 e alto-falante físicos (dossiê §7).
2. Backend escutando em `0.0.0.0` na LAN (spec B.1, com o aviso de
   segurança da §7 — no mínimo firewall restringindo a porta).
3. Primeiro flash da placa (revela a revisão do chip → variante `7b` vs
   `7b-p4x`).

## Status das fases

> Atualizado obrigatoriamente no encerramento de cada fase (junto com o
> commit final dela). O estado fino, por task, vive em
> `docs/professor-virtual/fases/fase-N.md` (checklist criado como primeiro
> ato da fase a partir do `fases/TEMPLATE.md`; cada checkbox é marcado no
> mesmo commit que conclui a task). Uma sessão nova retoma o projeto lendo:
> `CLAUDE.md` (automático) → esta tabela → `fases/fase-N.md` da fase
> corrente → `.claude/autonomy/decision-log.jsonl` (precedentes) → dossiê
> da placa. O hook de SessionStart injeta esse estado automaticamente.

| Fase | Status | Concluída em | Commit | Pendências físicas |
|---|---|---|---|---|
| F0 — Fundações | concluída | 2026-08-03 | c5fb376 | nenhuma — flash e tela "PV" validados no hardware em 2026-08-03 |
| F1 — Rede e hidratação | não iniciada | — | — | — |
| F2 — Câmera | não iniciada | — | — | — |
| F3 — Turno por foto | não iniciada | — | — | — |
| F4 — Turno por áudio | não iniciada | — | — | — |
| F5 — Máquina de fases | não iniciada | — | — | — |
| F6 — Failsafe/adulto | não iniciada | — | — | — |
| F7 — Preparação | não iniciada | — | — | — |
| F8 — Robustez | não iniciada | — | — | — |
| F9 — Entrega | não iniciada | — | — | — |

## Como iniciar

Quando o proprietário autorizar:

```
/autonomous-phase docs/professor-virtual/plano-firmware.md — Fase F0
```

Rollout recomendado: F0 observada → F1 assistida → demais fases em ritmo
normal.
