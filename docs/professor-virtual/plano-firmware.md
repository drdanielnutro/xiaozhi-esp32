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
- **Q2(a) — Identidade de build:** novas variantes `professor-virtual-7b` e
  `professor-virtual-7b-p4x` no `config.json` da família
  `main/boards/waveshare/esp32-p4-wifi6-touch-lcd/` (mesmas flags da 7b +
  `CONFIG_PROFESSOR_VIRTUAL=y`); zero duplicação de pinos.
- **Q3(a) — Voz do tutor:** decodificação MP3 no lado PV (componente
  `espressif/esp_audio_codec`, já no manifest) + método aditivo
  `AudioService::PlayPcm(...)` empurrando na fila interna de playback
  (dono único do codec).

Decisões de engenharia complementares: gravação em **WAV/PCM 16 kHz mono**
(canal do mic de `ReadAudioData`, cabeçalho WAV de 44 bytes; ~960 KB por
30 s em PSRAM), validada contra o backend via `curl` antes de codificar
firmware; sons de UI convertidos MP3→OGG (sem decode MP3 para assets);
textos pt-BR em `pv_strings.h` próprio (sem mexer no sistema de locales).

> Pendência de registro: ao iniciar a F0, gravar estas decisões no
> `.claude/autonomy/decision-log.jsonl`.

## Arquitetura do código novo

```
main/professor_virtual/            (novo, aditivo)
├── pv_app.cc/.h                   singleton PvApp: init (board/audio/UI), event loop, fases RAM (§9.2)
├── pv_backend_client.cc/.h        contrato §7 completo; cJSON; SEM retry de /api/turn; timeouts §8.1
├── pv_session_mirror.cc/.h        espelho RAM de state/lesson (§7.2/§7.3); detecção de avanço (§9.4)
├── pv_audio.cc/.h                 gravação WAV; MP3→PCM (esp_audio_codec) → PlayPcm; sons OGG locais
├── pv_camera.cc/.h                preview, captura JPEG, lote da preparação (PSRAM)
├── pv_settings.cc/.h              NVS namespace "pv" (backend_url etc.)
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
5. `main/audio/audio_service.{h,cc}` — método `PlayPcm` aditivo (Q3).

## Fases (cada uma executável via `/autonomous-phase`, com commit e revisão Codex ao final)

### F0 — Fundações e prova do build *(primeira fase autônoma: pequena e observada)*

Kconfig + CMake + gate no `main.cc` + variantes no `config.json` + esqueleto
`PvApp` (boot → tela LVGL própria "PV" + versão). Validar formato de áudio
com o backend por `curl` (WAV 16 kHz mono → `POST /api/turn`; critério: 200
com veredicto). **Pronto quando:** `release.py` builda `professor-virtual-7b`
**e** a variante `esp32-p4-wifi6-touch-lcd-7b` original continua buildando
(regressão); binário cabe na OTA de 4 MB (medir; plano B: REMOVE_ITEM dos
fontes do assistente); testes host passam; decisões registradas no
decision-log.

### F1 — Rede, configuração e hidratação (§9.1)

Callback de rede próprio; tela de configuração (backend URL via teclado
LVGL → NVS "pv"); `GET /api/health` periódico (10 s) + indicador;
`GET /api/state` + `GET /api/lesson`; roteamento de boot para telas
placeholder (preparação/tutoria/celebração/failsafe). **Spikes:** (a)
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

`POST /api/turn` multipart (`image`+`session_id`, chunked, timeout ≥120 s,
**sem retry**); parse da resposta; base64→PNG na tela; base64→MP3→PCM→
`PlayPcm` (voz); sons locais de feedback (OGG); re-hidratação pós-turno;
efeitos de erro 502/409 (§7.5). **Pronto quando:** ciclo completo mostrar
tarefa→foto→resposta com voz+imagem funciona contra o backend real; RAM
medida no pico do turno.

### F4 — Turno por áudio

Gravação ≤30 s com contador e auto-stop (§9.3), upload WAV, bloqueio de
comandos fora de `idle`. **Pronto quando:** turnos por voz funcionam fim a
fim (formato pré-validado na F0).

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

Captura em lote (≤20), miniaturas com excluir/refazer no mesmo índice,
envio único com campo `files` repetido, ilegíveis marcadas, revisão em
acordeões + chime, recomeçar com confirmação. Estratégia de RAM: lote em
PSRAM com teto por página + orçamento agregado **medido** (risco do dossiê
§5). **Pronto quando:** lição real de N páginas preparada no dispositivo e
tutoria iniciada.

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
| Formato de áudio rejeitado pela API multimodal | F0 (curl) | Alternar para Opus/OGG (encoder já existe) |
| API MP3 do `esp_audio_codec` (componente não vendorizado) | F3 | Componente baixa no 1º build (F0); validar API então |
| Binário > 4 MB com código morto do assistente | F0 | REMOVE_ITEM condicional dos fontes do assistente |
| RAM do lote da preparação | F7 | Teto por página + medição; qualidade adaptativa |
| Eco na gravação (caminho pré-AFE sem AEC) | F4 | Nada toca durante a gravação (spec); plano B: expor tap pós-AFE (toque aditivo) |

## Processo transversal

- Cada fase: decisões operacionais via Codex Decision Proxy (registradas no
  decision-log); revisão independente do Codex ao final (máx. 2 rodadas);
  relatório com "o que ainda exige hardware físico".
- Regressão obrigatória por fase: build da variante PV **e** da variante
  XiaoZhi 7b original + `python3 -m unittest discover -s scripts/tests`.
- O backend jamais é alterado; necessidade de mudança vira adaptação no
  firmware ou escalonamento ao dono (5 condições do `AGENTS.md`).
- Push, flash físico e alterações no backend: sempre do proprietário.

## Pré-requisitos do proprietário (podem andar em paralelo)

1. Confirmar câmera OV5647 e alto-falante físicos (dossiê §7).
2. Backend escutando em `0.0.0.0` na LAN (spec B.1, com o aviso de
   segurança da §7 — no mínimo firewall restringindo a porta).
3. Primeiro flash da placa (revela a revisão do chip → variante `7b` vs
   `7b-p4x`).

## Como iniciar

Quando o proprietário autorizar:

```
/autonomous-phase docs/professor-virtual/plano-firmware.md — Fase F0
```

Rollout recomendado: F0 observada → F1 assistida → demais fases em ritmo
normal.
