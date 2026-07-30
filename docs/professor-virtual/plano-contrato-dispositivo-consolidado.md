# Plano Consolidado — Contrato de Dispositivo v1.1

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Data da consolidação:** 30/07/2026
**Este documento consolida:** `plano-contrato-dispositivo.md`
+ `emenda-01-plano-contrato-dispositivo.md`
+ `emenda-02-plano-contrato-dispositivo.md` (versão corrigida pelo auditor,
commit `64f0ba3`) + `emenda-03-fish-audio.md` e as correções documentais da
auditoria final. **Para execução, este documento prevalece sobre os quatro**;
eles permanecem como histórico. O conteúdo técnico das tasks foi preservado;
as correções posteriores tornaram preflight, governança e sequência
operacional autocontidos e executáveis.

**Goal:** Adicionar ao backend `licao_casa` um perfil de contrato para o
dispositivo embarcado (mídia por URL, áudio WAV/PCM, imagem redimensionada,
idempotência fail-safe por `request_id`, preparação página a página e token de
autenticação fail-closed), preservando literalmente o contrato e o fluxo
pedagógico v1 do frontend web. Na versão v1.1, a única mudança intencional no
comportamento percebido do web é a voz: Fish Audio/Itachi substitui
Polly/Camila. A V1 exata permanece recuperável pela tag `baseline-v1.0`.

**Architecture:** Mudanças aditivas na borda de transporte (campos opcionais,
endpoints novos, campos novos de resposta que só aparecem no perfil v1.1). O
miolo pedagógico (`gemini.py`, `session_engine.py`, transições de veredito) não
é tocado. Requisições sem campos v1.1 recebem exatamente o payload v1 atual.

**Tech Stack:** FastAPI + pydantic v2 + aiofiles, httpx/Fish Audio, Pillow
(novo), pytest + pytest-asyncio + httpx ASGITransport. boto3/Polly permanece
somente como legado inativo da V1.

## Decisões do proprietário (registradas)

- **D1 — v1 literal:** requisição sem campos v1.1 recebe exatamente o conjunto
  de chaves v1 atual; campos v1.1 aparecem somente quando algum campo do perfil
  é enviado.
- **D2 — fail-closed:** não-loopback com token configurado e credencial
  ausente/errada ⇒ `401`; não-loopback com `DEVICE_API_TOKEN` NÃO configurado ⇒
  `503`; loopback sempre isento; origem ausente/desconhecida NÃO é loopback.
- **D3:** `5588b3e` é a baseline do código V1, condicionada à reconciliação e
  arquivamento corretos do GSD antes de tag/branch; arquivos não rastreados
  ficam fora da tag.
- **D4 — Fish Audio na v1.1:** Fish Audio substitui Polly como provedor ativo
  de todo TTS da v1.1. O web recebe MP3 44,1 kHz/128 kbps e o dispositivo
  recebe WAV PCM s16le mono/16 kHz, ambos com a voz Itachi
  (`c5a6cb585b094dedb241365e7e271973`) e modelo configurável
  `s2.1-pro-free`. A integração é HTTP bloqueante até o áudio completo, sem
  WebSocket, tags emocionais, retry ou fallback para Polly. Qualquer falha ou
  reprovação do preflight interrompe antes da Task 4.
- **D5 — duas tags:** `baseline-v1.0` aponta exatamente para `5588b3e` e marca
  a baseline funcional do código; a tag automática `v1.0` do GSD marca o
  encerramento documental do milestone. `git.create_tag` permanece `true`.

## Estado atual

| Item | Estado |
|---|---|
| Contrato canônico (`contrato-dispositivo.md`) criado | FEITO (commit `caa3ba8`) — emendas da Parte 1 abaixo PENDENTES |
| CLAUDE.md (zonas + retry) | FEITO (commit `3873025`) — ajuste de redação na Parte 1 PENDENTE |
| Emenda 03 Fish Audio incorporada ao consolidado | FEITO — revisão independente pré-código aprovada em 30/07/2026 |
| `AGENTS.md` / decision-policy / decision-log | PENDENTE (Parte 1) |
| Reconciliação GSD, baseline, branch | PENDENTE (Parte 2) |
| Tasks 3–11 (backend `licao_casa`) | PENDENTES (Parte 3) |
| Task 12 (`plano-firmware.md`) | PENDENTE (Parte 4, pós-Task 11) |

## Repositórios-alvo

| Parte | Repositório |
|---|---|
| Parte 1 (governança + contrato) e Parte 4 (Task 12) | `xiaozhi-esp32` |
| Parte 2 (pré-condições GSD/Git — operacional, com o proprietário) | `licao_casa` |
| Parte 3 (Tasks 3–11) | `licao_casa` (`backend/`) |

Atualizações do contrato canônico exigidas durante as Tasks 3.9 e 11 são
checkpoints/handoffs para commit separado no `xiaozhi-esp32`. O executor GSD
do `licao_casa` não faz commits autônomos no outro repositório nem fora do
branch/worktree sob seu controle.

**Ordem de execução:** Parte 1 → Parte 2 → conversão mecânica das tasks da
Parte 3 em `PLAN.md` do GSD (sem redesenho) → Tasks 3 → 3.9 → 4 → 5 → 6 → 7 →
8 → 9 → 10 → 11 → revisão independente final → Parte 4 → só então fases F1+ do
firmware.

## Global Constraints

- **v1 literal (D1):** requisições sem campos v1.1 retornam exatamente as
  chaves v1 de hoje e preservam sua semântica. A mudança intencional da v1.1 é
  o provedor/voz e, portanto, os bytes do MP3; a V1 exata está preservada em
  `baseline-v1.0`. A suíte existente (152 testes) não pode ser editada nem
  removida — apenas ADICIONAR testes. `python -m pytest tests/ -q` verde ao
  fim de CADA task.
- **Miolo pedagógico intocável:** zero edições em `gemini.py`,
  `session_engine.py`, `celebration.py`, `image_gen.py` e nas transições de
  veredito do `/api/turn` (passos 1–9 e 11–13b, exceto os pontos de
  supersessão/marcador explicitamente definidos na Task 8).
- Todos os campos novos são opcionais; ausentes ⇒ caminho v1 exato.
- Token e PIN jamais em corpo de resposta ou log.
- **Gate documental da Emenda 03:** antes da Task 3 e de qualquer código da
  Parte 3, submeter o diff documental da
  `emenda-03-fish-audio.md` e deste consolidado a revisão independente. A
  execução só prossegue após aprovação; findings bloqueantes exigem correção
  documental antes do preflight.
- Ambiente: `cd /Users/institutorecriare/VSCodeProjects/licao_casa/backend && source venv/bin/activate`.
- Commits no `licao_casa`: um por task, mensagem curta em português.
- Estilo: seguir o código existente (async + aiofiles, docstrings/comentários
  em inglês, pydantic v2).

---

# Parte 1 — Repositório `xiaozhi-esp32`: governança e contrato

### Task G1: Alinhar `AGENTS.md`, `decision-policy.md`, decision-log e CLAUDE.md

**Files:**
- Modify: `AGENTS.md`
- Modify: `.claude/autonomy/decision-policy.md`
- Modify: `.claude/autonomy/decision-log.jsonl` (apenas ADICIONAR entrada)
- Modify: `CLAUDE.md` (ajuste de redação do retry)

- [ ] **Step 1: `AGENTS.md`** — na seção "Seu papel neste repositório",
substituir o bullet do `licao_casa` (que declara o backend "intocável") por:

```markdown
- **`/Users/institutorecriare/VSCodeProjects/licao_casa`**: o Professor Virtual existente.
  O `backend/` (Python + FastAPI) tem duas zonas: o **miolo pedagógico é
  intocável** (prompts, vereditos, máquina de estados da sessão, failsafe —
  `gemini.py`, `session_engine.py` e as transições de estado de `main.py`);
  a **borda de transporte aceita mudanças aditivas** definidas em
  `docs/professor-virtual/contrato-dispositivo.md` (perfil v1.1), sempre
  preservando o comportamento v1 literal para o frontend web. O `frontend/`
  (React) é a implementação de referência do comportamento do cliente.
```

Em "Restrições obrigatórias", substituir `sem retry automático de turno` por:

```markdown
  retry automático de `POST /api/turn` proibido sem `request_id`; com cliente
  único/serial, cache íntegro e o MESMO `request_id` (contrato v1.1), uma
  retransmissão pode processar se a falha anterior ocorreu antes de
  `processing`, devolver replay 200 quando há resposta `done` válida e
  replayável, ou 409 fail-safe quando o resultado é indeterminado,
  supersedido ou não replayável. Status HTTP isolado, inclusive 502, não
  autoriza retry automático; a garantia é não haver dupla aplicação
  silenciosa dentro dessas premissas;
```

Em "Fora de escopo", substituir o bullet correspondente por:

```markdown
- **Fora de escopo:** alterar o miolo pedagógico do backend; mudanças de
  transporte fora do contrato v1.1 aprovado; soluções em nuvem; duplicar a
  fonte de verdade no dispositivo; manter qualquer parte da experiência do
  usuário no desktop.
```

Na seção "Preservação do backend", ADICIONAR ao final:

```markdown
Exceção aprovada pelo proprietário (30/07/2026): as mudanças aditivas de
transporte do contrato v1.1 (`docs/professor-virtual/contrato-dispositivo.md`)
estão pré-autorizadas nos termos do plano consolidado. Mudanças além desse
escopo continuam sujeitas às 5 condições e ao escalonamento.
```

Em "Invariantes do Professor Virtual", substituir o bullet "**Nunca** faça
retry automático de `POST /api/turn` ..." por:

```markdown
- Retry de `POST /api/turn` sem `request_id`: **nunca** (pode virar outro
  turno). Com cliente único/serial, cache íntegro e o MESMO `request_id`
  (v1.1), a retransmissão pode processar se a falha anterior ocorreu antes de
  `processing`, devolver replay 200 se há resposta `done` válida e replayável,
  ou 409 fail-safe se o resultado é indeterminado, supersedido ou não
  replayável. Status HTTP isolado, inclusive 502, não autoriza retry
  automático. Após 409, descartar ids pendentes, re-hidratar `GET /api/state`
  + `GET /api/lesson` e iniciar turno lógico novo com UUID novo.
```

- [ ] **Step 2: `.claude/autonomy/decision-policy.md`** — na seção "Missão
Professor Virtual — regras duras", substituir os bullets "Preserve o contrato
HTTP…", "Nunca aprove retry…" e "Mudança no backend…" por:

```markdown
- Preserve o contrato HTTP: seção 7 da especificação (v1) + perfil v1.1 de
  `docs/professor-virtual/contrato-dispositivo.md`. Não invente endpoints,
  campos, estados ou capacidades fora deles.
- Retry de `POST /api/turn`: nunca aprove retry sem `request_id`. Com
  cliente único/serial, cache íntegro e o MESMO `request_id` (v1.1), uma
  retransmissão pode processar após falha pré-`processing`, devolver replay
  200 ou 409 fail-safe. Status HTTP isolado, inclusive 502, não autoriza retry
  automático. A garantia é não haver dupla aplicação silenciosa dentro dessas
  premissas. Após 409: descartar ids pendentes, re-hidratar e usar UUID novo.
- Nunca aprove mover regra pedagógica, contador ou decisão de avanço para o
  dispositivo: o backend é a única fonte de verdade pedagógica.
- Mudança no backend: as mudanças aditivas de transporte do contrato v1.1
  estão pré-aprovadas pelo proprietário (30/07/2026, plano consolidado).
  Qualquer mudança FORA desse escopo — em especial no miolo pedagógico —
  mantém as 5 condições do `AGENTS.md` e `escalate: true`.
```

- [ ] **Step 3: `decision-log.jsonl`** — adicionar uma entrada registrando
D1, D2 e D3 (data 30/07/2026, decisor: proprietário via Codex Decision Proxy,
referência: plano consolidado).

- [ ] **Step 4: `CLAUDE.md`** — substituir o bullet do retry (que hoje diz que
o reenvio "devolve a resposta armazenada sem criar novo turno") por:

```markdown
- Retry de `POST /api/turn`: proibido sem `request_id`; com `request_id`
  (contrato v1.1), cliente único/serial e cache íntegro, a retransmissão do
  MESMO turno pode processar após falha pré-`processing`, devolver replay 200
  ou 409 fail-safe. Status HTTP isolado, inclusive 502, não autoriza retry
  automático; a garantia é não haver dupla aplicação silenciosa dentro dessas
  premissas. Após 409: descartar ids pendentes, re-hidratar e usar UUID novo.
```

- [ ] **Step 5: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/xiaozhi-esp32
git add AGENTS.md .claude/autonomy/decision-policy.md .claude/autonomy/decision-log.jsonl CLAUDE.md
git commit -m "Governança alinhada ao contrato v1.1 (duas zonas, retry idempotente, D1-D3)"
```

### Task G2: Emendar o contrato canônico (`docs/professor-virtual/contrato-dispositivo.md`)

- [ ] **Step 1: Princípio de retry** — substituir o item 3 de "## Princípios"
por, literalmente:

> 3. **Retry de turno:** retry de `POST /api/turn` continua PROIBIDO sem
> `request_id`. Com cache íntegro e cliente serial conforme este contrato,
> uma retransmissão do mesmo turno pode processar quando a falha anterior
> ocorreu antes do marcador `processing`, devolver replay 200 quando existe
> resposta `done` válida e replayável, ou devolver 409 quando o resultado é
> indeterminado, supersedido ou não replayável. A garantia é não haver dupla
> aplicação silenciosa dentro dessas premissas. Status HTTP isolado, inclusive
> 502, não autoriza retry automático. Após 409, o dispositivo descarta todos
> os ids pendentes anteriores, re-hidrata `GET /api/state` +
> `GET /api/lesson` e só então inicia um turno lógico novo com UUID novo.

- [ ] **Step 2: Resposta v1 literal (D1)** — substituir **somente** a sentença
`Clientes v1 ignoram os campos extras.` pela garantia abaixo. Preservar todo o
restante da seção, inclusive URLs relativas, formatos e semântica de `null`:

> Requisições **sem** nenhum campo v1.1 recebem **exatamente** o payload v1
> atual (mesmo conjunto de chaves; nenhuma chave nova). Os campos
> `audio_url`, `image_url`, `audio_format`, `image_format` e `request_id`
> aparecem na resposta **somente** quando a requisição envia ao menos um
> campo do perfil v1.1 (`request_id`, `media`, `audio_format`,
> `image_max_px`).

- [ ] **Step 3: Linha do `request_id` na tabela** — substituir por:

```markdown
| `request_id` | str | 1–128 caracteres, não branco; único durante toda a instalação (UUID v4 recomendado) | chave de idempotência (ver abaixo) |
```

e adicionar logo abaixo da tabela:

> O cliente gera um `request_id` novo para cada turno lógico novo e jamais
> reutiliza um id para outro conteúdo, outra lição ou outro momento da
> instalação. O mesmo id só pode reaparecer com os mesmos campos e bytes de
> mídia do turno original quando a resposta se perdeu. O backend rejeita
> `request_id` composto apenas por espaços ou com mais de 128 caracteres com
> 400. Por normalização da stack de formulários, valor vazio
> (`request_id=`) equivale a campo omitido: não ativa idempotência nem o
> perfil v1.1.

- [ ] **Step 4: Seção "Idempotência (`request_id`)"** — substituir a seção
inteira pelo texto normativo autocontido abaixo:

```markdown
### Idempotência (`request_id`)

O cache `data/last_turn_response.json` contém:

- `seen_request_ids`: ledger dos ids já reservados/aplicados;
- `rehydration_required`: sentinela de recuperação após corrupção descoberta
  sem um id corrente;
- `last`: o único registro cuja resposta ainda pode ser replayada
  (`processing`, `done`, `indeterminate` ou `null`).

O ledger íntegro não é truncado na v1.1. Remover um id antigo permitiria que
ele fosse aplicado novamente. Compactação futura só pode ocorrer com mecanismo
durável equivalente e mudança explícita de protocolo.

Para uma requisição com `request_id`, antes de LLM/TTS/geração de imagem e de
qualquer mutação de estado:

- se `last` tem o mesmo id e status `done`, o backend devolve exatamente a
  resposta armazenada (200), sem LLM, TTS, imagem, contador ou transição, desde
  que ela valide e seu campo `request_id` ecoe o mesmo id; divergência é cache
  inválido e segue o fluxo 409;
- se o id consta em `seen_request_ids`, mas não existe resposta replayável, ou
  se `last` tem status `processing`/`indeterminate`, o backend devolve 409;
- se `rehydration_required=true`, a primeira tentativa futura é registrada
  como `indeterminate` e recebe 409; somente outro id, criado depois da
  re-hidratação, pode prosseguir;
- somente um id ainda não visto entra no pipeline.

Depois que LLM e mídia terminam, mas antes dos efeitos persistentes do caminho
de sucesso, o backend grava atomicamente `processing`. Em seguida persiste
transições/contadores em `state.json` e o append em `conversation.json`; ao
final grava atomicamente `done` + resposta.

O marcador não cobre os arquivos de mídia já escritos (inertes até serem
referenciados por um `done`) nem os incrementos de falha técnica dos caminhos
502, que mantêm a semântica por tentativa da v1. Antes de persistir esse
incremento técnico, o backend supersede qualquer replay anterior, mas não
reserva o id da tentativa que falhou. Falha antes de `processing` deixa esse
mesmo id elegível para processamento; falha depois de `processing` deixa o id
em 409 fail-safe. Portanto, o status HTTP isolado não autoriza retry
automático.

Qualquer mutação de estado que não pertence à própria resposta armazenada
grava a supersessão (`last: null`) **antes** de persistir o novo estado. Isso
inclui: turno v1 sem `request_id`, publicação de nova lição,
`POST /api/adult/resolve` bem-sucedido e expiração persistida por
`GET /api/state`, além dos incrementos técnicos dos caminhos 502 anteriores ao
marcador. Os ids conhecidos permanecem no ledger e retornam 409; a resposta
anterior deixa de ser replayável. Falha ao gravar a supersessão impede a
mutação subsequente.

Cache ilegível ou semanticamente inválido nunca faz o backend assumir que o id
corrente (ou o primeiro id futuro) é novo. O arquivo é copiado para
`last_turn_response.corrupt.json` e substituído atomicamente. Quando existe um
id corrente, ele fica `indeterminate` e recebe 409; quando a corrupção é
descoberta por uma mutação que não reserva o id corrente (turno v1, nova lição,
`adult/resolve`, expiração ou caminho 502 pré-marcador),
`rehydration_required=true` força 409 na primeira tentativa futura. Depois do
409, o dispositivo re-hidrata e usa id novo.

**Limite sob corrupção integral:** ids que existiam somente no arquivo
ilegível não podem ser reconstruídos. Por isso, a garantia nesse cenário
depende do cliente único/serial obedecer ao 409: descartar qualquer id anterior
e gerar UUID novo após re-hidratar. Um cliente fora do protocolo que, depois
do 409, envie outro id histórico perdido pode fazê-lo parecer novo. Eliminar
esse limite exigiria ledger separado com durabilidade independente e fica fora
da v1.1.

O 409 significa “a resposta não está disponível e o turno pode já ter sido
aplicado”; não é resposta pedagógica e não deve ser reproduzido como turno.
```

- [ ] **Step 5: Ciclo de vida da mídia** — substituir "Arquivos de turnos
anteriores são apagados quando um novo turno gera mídia." por:

> A mídia nova é escrita SEM remover a anterior; a remoção da mídia não
> referenciada acontece apenas DEPOIS da consolidação da nova resposta
> idempotente (ou da supersessão), dentro da mesma fronteira protegida do
> turno, em limpeza best-effort (órfãos tolerados; mídia ainda referenciada
> por resposta replayável nunca é apagada).

- [ ] **Step 6: Autenticação (D2)** — substituir a seção inteira por:

```markdown
## Autenticação (token de dispositivo)

- Config: variável de ambiente `DEVICE_API_TOKEN` no backend (`.env`).
- Toda requisição HTTP cuja origem não é loopback exige
  `Authorization: Bearer <token>` ou, alternativamente,
  `X-Api-Token: <token>`. Isso inclui `/api/health`, `/api/media/...` e todos
  os demais endpoints.
- Com token configurado, credencial ausente ou incorreta retorna
  `401 {"detail":"Token inválido ou ausente"}`.
- Com token não configurado, origem não-loopback retorna
  `503 {"detail":"DEVICE_API_TOKEN não configurado no servidor"}`.
- `127.0.0.1`, `::1` e `localhost` são loopback e permanecem isentos.
- Origem ausente ou desconhecida não é loopback.
- Não existe modo remoto sem token.
- Token e PIN nunca aparecem em corpo de resposta ou log.
```

- [ ] **Step 7: Neutralidade de TTS** — na tabela de `audio_format` e na seção
"Formato WAV", remover as menções a "Polly" da interface normativa (descrever
apenas: WAV = PCM s16le mono 16 kHz com header RIFF/WAVE; MP3 = mono 44,1 kHz,
128 kbps). Na seção "Validações empíricas pendentes", substituir o preflight
Polly por Fish Audio: modelo configurado, voz Itachi
(`c5a6cb585b094dedb241365e7e271973`), MP3 e WAV, tempos observados e aprovação
humana. O provedor não aparece como requisito do firmware.

- [ ] **Step 8: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/xiaozhi-esp32
git add docs/professor-virtual/contrato-dispositivo.md
git commit -m "Contrato v1.1: idempotência fail-safe completa, v1 literal, token fail-closed, TTS neutro"
```

---

# Parte 2 — Pré-condições operacionais GSD/Git no `licao_casa`

Executar ANTES de abrir o milestone, numa sessão local do `licao_casa` com o
proprietário. Preservar todos os arquivos não rastreados existentes
(auditorias, respostas e notas).

## Gate A — confirmar e auditar a V1

1. Confirmar em modo somente leitura o estado de Git e que `5588b3e` continua
   sendo a baseline funcional aprovada.
2. Executar `/gsd:audit-milestone`.
3. Tratar o resultado como diagnóstico. Esse comando não reconcilia nem
   corrige automaticamente `STATE.md`, `ROADMAP.md`, summaries, verificações,
   UATs, requisitos ou quick tasks.
4. Confrontar todos esses artefatos e reconciliar as divergências encontradas
   (incluindo a fase 5 como "executing" em um lugar e concluída em outro).
5. Qualquer gap que dependa de julgamento humano deve ser apresentado ao
   proprietário para resolução ou aceite explícito e documentado.
6. Repetir a auditoria. Não executar `complete-milestone` enquanto ela não
   estiver aprovada ou enquanto os gaps não tiverem sido explicitamente
   aceitos e registrados.

## Gate B — preservar os dois marcos Git

1. Verificar se a tag `baseline-v1.0` já existe.
2. Se não existir, e somente após autorização explícita do proprietário,
   criá-la apontando exatamente para `5588b3e`:

   ```bash
   git tag -a baseline-v1.0 5588b3e \
     -m "Baseline funcional da V1 antes do contrato de dispositivo v1.1"
   ```

3. Se já existir, verificar que `baseline-v1.0^{}` resolve exatamente para
   `5588b3e`; qualquer divergência interrompe o fluxo.
4. Manter `git.create_tag: true`.
5. Executar `/gsd:complete-milestone 1.0`.
6. Quando o GSD perguntar pelos diretórios das fases, escolher arquivá-los em
   `.planning/milestones/`; não deixá-los para remoção posterior por
   `phases.clear`.
7. Permitir que o GSD crie sua tag automática `v1.0`. Ela representa o
   fechamento documental do milestone e é deliberadamente distinta da
   `baseline-v1.0`, que representa o código V1 funcional.
8. Somente depois que o fechamento e todos os commits de arquivamento
   terminarem, criar, com autorização explícita do proprietário, a branch
   `milestone/device-contract-v1.1` a partir do HEAD final da `main`.

## Gate C — registrar o novo milestone sem redesenhar

1. Na branch dedicada, executar:

   ```text
   /gsd:new-milestone "Contrato de dispositivo v1.1"
   ```

2. Usar versão `v1.1`, pular a pesquisa e registrar uma única fase.
3. Requisitos e roadmap são representação mecânica do contrato e das Tasks
   3–11 já aprovadas. O roadmapper não está autorizado a acrescentar, remover,
   dividir, fundir ou redesenhar conteúdo técnico.
4. Se a estrutura produzida divergir, interromper e pedir correção mecânica;
   não aceitar a divergência por conveniência.

## Gate D — importar somente a Parte 3

Não executar `/gsd:import` diretamente sobre este consolidado: o comando não
possui seletor de seção e o documento contém tarefas dos dois repositórios.

Depois que o milestone informar o número real da fase:

1. criar no `licao_casa` um artefato de importação mecanicamente derivado,
   contendo somente esta Parte 3 (Tasks 3–11);
2. usar frontmatter GSD com `phase: N`, número do plano, `type` adequado e
   `autonomous: false`;
3. preservar verbatim o conteúdo técnico das Tasks 3–10;
4. representar as escritas da Task 11 no `xiaozhi-esp32` como checkpoint/handoff
   humano, mantendo o texto exato a registrar e exigindo commit separado no
   repositório correto;
5. usar o caminho absoluto do artefato no `/gsd:import --from ...`;
6. validar paridade 1:1 entre o artefato, esta Parte 3 e o `PLAN.md` resultante;
7. resolver conflitos de decisões antigas atualizando somente o contexto e as
   decisões do milestone v1.1; não reescrever conteúdo técnico;
8. somente depois da aprovação da paridade executar `/gsd:execute-phase N`.

---

# Parte 3 — Repositório `licao_casa` (backend): Tasks 3–11

Todas as tasks rodam com o venv ativo:

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa/backend && source venv/bin/activate
```

## Task 3: Token de dispositivo (Settings + middleware fail-closed)

**Files:**
- Modify: `backend/config.py`
- Modify: `backend/main.py`
- Test: `backend/tests/test_device_auth.py` (novo)

**Interfaces:**
- Consumes: `main.settings` (instância própria de `main.py:46`, monkeypatchável).
- Produces: guard D2 — `401` credencial errada com token configurado; `503`
  remoto sem token configurado; loopback isento; `_is_loopback(host)` público
  para testes.

- [ ] **Step 1: Escrever os testes que falham**

Criar `backend/tests/test_device_auth.py`:

```python
import pytest
from httpx import ASGITransport, AsyncClient

import main
from main import app


def _client(client_addr):
    transport = ASGITransport(app=app, client=client_addr)
    return AsyncClient(transport=transport, base_url="http://test")


class TestDeviceToken:
    """Token guard for non-loopback clients (contrato v1.1, D2 fail-closed)."""

    @pytest.mark.asyncio
    async def test_remote_without_token_is_rejected(self, monkeypatch):
        monkeypatch.setattr(main.settings, "device_api_token", "segredo123")
        async with _client(("192.168.0.42", 5555)) as ac:
            resp = await ac.get("/api/health")
        assert resp.status_code == 401

    @pytest.mark.asyncio
    async def test_remote_with_bearer_token_passes(self, monkeypatch):
        monkeypatch.setattr(main.settings, "device_api_token", "segredo123")
        async with _client(("192.168.0.42", 5555)) as ac:
            resp = await ac.get(
                "/api/health", headers={"Authorization": "Bearer segredo123"}
            )
        assert resp.status_code == 200

    @pytest.mark.asyncio
    async def test_remote_with_x_api_token_passes(self, monkeypatch):
        monkeypatch.setattr(main.settings, "device_api_token", "segredo123")
        async with _client(("192.168.0.42", 5555)) as ac:
            resp = await ac.get("/api/health", headers={"X-Api-Token": "segredo123"})
        assert resp.status_code == 200

    @pytest.mark.asyncio
    async def test_loopback_without_token_passes(self, monkeypatch):
        monkeypatch.setattr(main.settings, "device_api_token", "segredo123")
        async with _client(("127.0.0.1", 5555)) as ac:
            resp = await ac.get("/api/health")
        assert resp.status_code == 200

    @pytest.mark.asyncio
    async def test_unset_token_rejects_remote_with_503(self, monkeypatch):
        monkeypatch.setattr(main.settings, "device_api_token", "")
        async with _client(("192.168.0.42", 5555)) as ac:
            resp = await ac.get("/api/health")
        assert resp.status_code == 503

    @pytest.mark.asyncio
    async def test_unset_token_still_allows_loopback(self, monkeypatch):
        monkeypatch.setattr(main.settings, "device_api_token", "")
        async with _client(("127.0.0.1", 5555)) as ac:
            resp = await ac.get("/api/health")
        assert resp.status_code == 200


class TestLoopbackDetection:
    def test_none_origin_is_not_loopback(self):
        from main import _is_loopback
        assert _is_loopback(None) is False

    def test_known_hosts(self):
        from main import _is_loopback
        assert _is_loopback("127.0.0.1") is True
        assert _is_loopback("::1") is True
        assert _is_loopback("192.168.0.42") is False
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `python -m pytest tests/test_device_auth.py -v`
Expected: FAIL — `AttributeError`/`ImportError` (settings sem
`device_api_token`; `_is_loopback` inexistente).

- [ ] **Step 3: Implementar**

Em `backend/config.py`, dentro de `Settings`, após `adult_pin: str = ""`,
adicionar a autenticação do dispositivo e a configuração não secreta do TTS.
`FISH_API_KEY` fica vazio no código e só é preenchido no `.env` local:

```python
    device_api_token: str = ""
    fish_api_key: str = ""
    fish_voice_id: str = "c5a6cb585b094dedb241365e7e271973"
    fish_tts_model: str = "s2.1-pro-free"
    fish_tts_api_url: str = "https://api.fish.audio/v1/tts"
    fish_tts_timeout_seconds: float = Field(default=120.0, gt=0)
    fish_tts_latency: str = "normal"
    fish_tts_speed: float = Field(default=1.0, gt=0)
```

Em `backend/main.py`: adicionar `import secrets` ao topo; na linha do fastapi,
incluir `Request`; adicionar `from fastapi.responses import JSONResponse`:

```python
from fastapi import FastAPI, UploadFile, File, Form, HTTPException, Request
from fastapi.responses import JSONResponse
```

Logo após `_state_lock = asyncio.Lock()`:

```python
# Loopback exempt (web frontend via Vite proxy). An ABSENT/unknown origin is
# NOT loopback: fail-closed (D2).
_LOOPBACK_HOSTS = {"127.0.0.1", "::1", "localhost"}


def _is_loopback(client_host: str | None) -> bool:
    return client_host is not None and client_host in _LOOPBACK_HOSTS


@app.middleware("http")
async def device_token_guard(request: Request, call_next):
    """Device-token guard (contrato v1.1, fail-closed — D2).

    Reads settings.device_api_token at request time (tests monkeypatch it,
    same pattern as adult_pin). Missing/failed credential with a configured
    token -> 401; server without DEVICE_API_TOKEN receiving a remote
    connection -> 503 (server misconfiguration, mirroring the adult_pin 503
    convention).
    """
    client_host = request.client.host if request.client else None
    if not _is_loopback(client_host):
        token = settings.device_api_token
        if not token:
            return JSONResponse(
                status_code=503,
                content={"detail": "DEVICE_API_TOKEN não configurado no servidor"},
            )
        auth = request.headers.get("authorization") or ""
        provided = request.headers.get("x-api-token") or ""
        if not provided and auth.startswith("Bearer "):
            provided = auth[len("Bearer "):]
        if not secrets.compare_digest(provided, token):
            return JSONResponse(
                status_code=401,
                content={"detail": "Token inválido ou ausente"},
            )
    return await call_next(request)
```

- [ ] **Step 4: Rodar testes novos e suíte inteira**

Run: `python -m pytest tests/test_device_auth.py -v && python -m pytest tests/ -q`
Expected: tudo PASS (o `client` do conftest usa loopback default do
ASGITransport).

- [ ] **Step 5: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add backend/config.py backend/main.py backend/tests/test_device_auth.py
git commit -m "Configura token do dispositivo e Fish Audio para a v1.1"
```

## Task 3.9 (preflight): Validação real do Fish MP3 + WAV — ANTES da Task 4

**Files:**
- Create: `backend/scripts/check_fish_tts.py`

**Gate:** requer autorização do proprietário para duas chamadas reais de
volume mínimo. Antes de rodar, o proprietário configura `FISH_API_KEY` apenas
no `.env` local. O segredo nunca aparece no script, documento, commit ou log.
Resultado técnico e audição humana são registrados separadamente no contrato
canônico antes da Task 4.

- [ ] **Step 1: Criar o script standalone**

```python
"""Manual Fish Audio check for the v1.1 MP3/WAV contract.

Run from backend/ with the venv active: python scripts/check_fish_tts.py
Writes both listening files to the system temporary directory.
"""

import io
import sys
import tempfile
import time
import wave
from pathlib import Path

import httpx

sys.path.insert(0, str(Path(__file__).parent.parent))

from config import settings  # noqa: E402


TEST_TEXT = """Certo... vamos de novo, mas desta vez preste atenção ao ritmo.

Você não precisa correr. A pressa faz até uma ideia simples parecer difícil.

Observe apenas três movimentos. Permanece igual: criacionismo fixista. Muda em linha reta: Lamarck. Forma ramificações: Darwin.

É isso. Três desenhos, três maneiras de explicar a mudança.

Heh, não foi tão complicado, foi? Agora explique do seu jeito. Se conseguir fazer isso, você realmente entendeu."""


def _payload(output_format: str) -> dict:
    payload = {
        "text": TEST_TEXT,
        "reference_id": settings.fish_voice_id,
        "format": output_format,
        "sample_rate": 44100 if output_format == "mp3" else 16000,
        "latency": settings.fish_tts_latency,
        "normalize": True,
        "prosody": {
            "speed": settings.fish_tts_speed,
            "volume": 0,
            "normalize_loudness": True,
        },
    }
    if output_format == "mp3":
        payload["mp3_bitrate"] = 128
    return payload


def _synthesize(client: httpx.Client, output_format: str) -> tuple[bytes, float]:
    started = time.perf_counter()
    response = client.post(
        settings.fish_tts_api_url,
        headers={
            "Authorization": f"Bearer {settings.fish_api_key}",
            "Content-Type": "application/json",
            "Accept": "audio/mpeg" if output_format == "mp3" else "audio/wav",
            "model": settings.fish_tts_model,
        },
        json=_payload(output_format),
    )
    elapsed = time.perf_counter() - started
    if not 200 <= response.status_code < 300:
        raise RuntimeError(f"Fish Audio HTTP {response.status_code}")
    content_type = response.headers.get("content-type", "").split(";", 1)[0]
    if not response.content or content_type == "application/json":
        raise RuntimeError("Fish Audio retornou áudio vazio ou JSON")
    return bytes(response.content), elapsed


def _validate_wav(data: bytes) -> None:
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise RuntimeError("WAV sem RIFF/WAVE válido")
    try:
        with wave.open(io.BytesIO(data), "rb") as wav_file:
            values = (
                wav_file.getnchannels(),
                wav_file.getframerate(),
                wav_file.getsampwidth(),
            )
    except (EOFError, wave.Error) as exc:
        raise RuntimeError("WAV inválido") from exc
    if values != (1, 16000, 2):
        raise RuntimeError(
            f"WAV inesperado: channels={values[0]} rate={values[1]} "
            f"sampwidth={values[2]}"
        )


def _validate_mp3(data: bytes) -> None:
    has_id3 = data[:3] == b"ID3"
    has_frame_sync = len(data) >= 2 and data[0] == 0xFF and data[1] & 0xE0 == 0xE0
    if not (has_id3 or has_frame_sync):
        raise RuntimeError("MP3 sem ID3/frame sync válido")


def main() -> None:
    if not settings.fish_api_key.strip():
        raise SystemExit("BLOCKER: configure FISH_API_KEY apenas no .env local")

    outputs: list[tuple[str, Path, float]] = []
    with httpx.Client(timeout=settings.fish_tts_timeout_seconds) as client:
        for output_format in ("mp3", "wav"):
            data, elapsed = _synthesize(client, output_format)
            if output_format == "wav":
                _validate_wav(data)
            else:
                _validate_mp3(data)
            path = (
                Path(tempfile.gettempdir())
                / f"fish_itachi_preflight.{output_format}"
            )
            path.write_bytes(data)
            outputs.append((output_format, path, elapsed))

    for output_format, path, elapsed in outputs:
        print(
            f"OK técnico format={output_format} model={settings.fish_tts_model} "
            f"voice={settings.fish_voice_id} time={elapsed:.2f}s file={path}"
        )
    print("PAUSA OBRIGATÓRIA: ouça MP3 e WAV e aprove voz/qualidade.")


if __name__ == "__main__":
    try:
        main()
    except (httpx.HTTPError, OSError, RuntimeError) as exc:
        print(f"BLOCKER Fish Audio: {exc}", file=sys.stderr)
        raise SystemExit(1) from None
```

O texto é plain e não contém marcadores emocionais. O script usa HTTP
diretamente, não importa `fish_audio.py` e termina antes de a Task 4 começar.

- [ ] **Step 2: Rodar com autorização e audição humana**

Run: `python scripts/check_fish_tts.py`

Expected: dois `OK técnico`, MP3 Itachi reproduzível, WAV Itachi reproduzível
e WAV validado como RIFF/WAVE mono/16-bit/16 kHz.

**Condição de parada (D4):** qualquer falha técnica, voz incorreta ou qualidade
reprovada interrompe antes da Task 4 e exige nova emenda. Não há retry
automático nem fallback para Polly.

- [ ] **Step 3: Rodar a suíte completa**

Run: `python -m pytest tests/ -q`
Expected: tudo PASS.

- [ ] **Step 4: Registrar resultado e commitar por repositório**

Registrar no contrato: data, endpoint, modelo, voice id, formatos, parâmetros,
tempos observados e aprovação humana — nunca a chave.

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add backend/scripts/check_fish_tts.py
git commit -m "Preflight: valida Fish Audio Itachi em MP3 e WAV 16 kHz"
cd /Users/institutorecriare/VSCodeProjects/xiaozhi-esp32
git add docs/professor-virtual/contrato-dispositivo.md
git commit -m "Contrato v1.1: registra preflight Fish MP3 e WAV"
```

## Task 4: Fish Audio para web MP3 e dispositivo WAV

Só começa depois de o preflight Fish MP3+WAV e a audição humana serem
aprovados.

**Files:**
- Create: `backend/fish_audio.py`
- Modify: `backend/main.py` (somente import do TTS nesta task)
- Test: `backend/tests/test_fish_audio.py` (novo)

`backend/polly.py`, `backend/tests/test_polly.py`, a fixture `mock_polly`
existente e boto3 permanecem sem edição como legado inativo da V1.

**Interface:**

```python
async def synthesize_speech(
    text: str,
    output_format: Literal["mp3", "wav"] = "mp3",
    voice_id: str | None = None,
    model: str | None = None,
) -> bytes
```

- [ ] **Step 1: Escrever os testes novos que falham**

Criar `backend/tests/test_fish_audio.py` cobrindo:

- headers Bearer/model e voice id Itachi;
- texto recebido sem alteração ou tags;
- MP3 44,1 kHz/128 kbps;
- WAV RIFF/WAVE mono/16-bit/16 kHz;
- formato inválido;
- chave ausente;
- timeout/falha de transporte;
- HTTP 401, 402, 422, 429 e 5xx;
- resposta vazia/JSON e WAV inválido;
- erro sanitizado sem chave, texto integral ou corpo remoto;
- `main.synthesize_speech is fish_audio.synthesize_speech`, provando que o
  runtime v1.1 não chama Polly.

Usar uma fixture local que substitui `httpx.AsyncClient`; nenhum teste faz rede
real.

- [ ] **Step 2: Rodar e ver falhar**

Run: `python -m pytest tests/test_fish_audio.py -v`
Expected: FAIL — módulo `fish_audio` ainda não existe.

- [ ] **Step 3: Implementar `backend/fish_audio.py`**

```python
"""Fish Audio TTS adapter for complete-response MP3/WAV synthesis."""

import io
from typing import Literal
import wave

import httpx

from config import settings


AudioFormat = Literal["mp3", "wav"]


class FishAudioError(RuntimeError):
    """Sanitized Fish Audio failure safe to surface in internal debug."""


def _validate_wav(data: bytes) -> None:
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise FishAudioError("Fish Audio returned an invalid WAV")
    try:
        with wave.open(io.BytesIO(data), "rb") as wav_file:
            values = (
                wav_file.getnchannels(),
                wav_file.getframerate(),
                wav_file.getsampwidth(),
            )
    except (EOFError, wave.Error) as exc:
        raise FishAudioError("Fish Audio returned an invalid WAV") from exc
    if values != (1, 16000, 2):
        raise FishAudioError("Fish Audio returned an unexpected WAV layout")


async def synthesize_speech(
    text: str,
    output_format: AudioFormat = "mp3",
    voice_id: str | None = None,
    model: str | None = None,
) -> bytes:
    """Synthesize complete MP3 or WAV audio without retry or fallback."""
    if output_format not in ("mp3", "wav"):
        raise ValueError("output_format must be 'mp3' or 'wav'")
    if not text.strip():
        raise ValueError("text must not be blank")
    api_key = settings.fish_api_key.strip()
    if not api_key:
        raise FishAudioError("FISH_API_KEY is not configured")

    payload = {
        "text": text,
        "reference_id": voice_id or settings.fish_voice_id,
        "format": output_format,
        "sample_rate": 44100 if output_format == "mp3" else 16000,
        "latency": settings.fish_tts_latency,
        "normalize": True,
        "prosody": {
            "speed": settings.fish_tts_speed,
            "volume": 0,
            "normalize_loudness": True,
        },
    }
    if output_format == "mp3":
        payload["mp3_bitrate"] = 128

    headers = {
        "Authorization": f"Bearer {api_key}",
        "Content-Type": "application/json",
        "Accept": "audio/mpeg" if output_format == "mp3" else "audio/wav",
        "model": model or settings.fish_tts_model,
    }
    try:
        async with httpx.AsyncClient(
            timeout=settings.fish_tts_timeout_seconds
        ) as client:
            response = await client.post(
                settings.fish_tts_api_url,
                headers=headers,
                json=payload,
            )
    except httpx.TimeoutException as exc:
        raise FishAudioError("Fish Audio request timed out") from exc
    except httpx.HTTPError as exc:
        raise FishAudioError("Fish Audio transport failed") from exc

    if not 200 <= response.status_code < 300:
        raise FishAudioError(f"Fish Audio HTTP {response.status_code}")
    content_type = response.headers.get("content-type", "").split(";", 1)[0]
    if not response.content or content_type == "application/json":
        raise FishAudioError("Fish Audio returned no audio")

    audio = bytes(response.content)
    if output_format == "wav":
        _validate_wav(audio)
    return audio
```

Não incluir corpo remoto, chave ou texto nos erros. Não adicionar retry.

Em `backend/main.py`, substituir somente:

```python
from polly import synthesize_speech
```

por:

```python
from fish_audio import synthesize_speech
```

- [ ] **Step 4: Rodar testes novos, regressão Polly e suíte**

Run:
`python -m pytest tests/test_fish_audio.py tests/test_polly.py -v && python -m pytest tests/ -q`

Expected: tudo PASS; testes Polly continuam verdes cobrindo apenas o legado.

- [ ] **Step 5: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add backend/fish_audio.py backend/main.py backend/tests/test_fish_audio.py
git commit -m "Fish Audio: voz Itachi em MP3 para web e WAV para dispositivo"
```

## Task 5: Redimensionamento de imagem (Pillow)

**Files:**
- Create: `backend/media_utils.py`
- Modify: `backend/requirements.txt`
- Test: `backend/tests/test_media_utils.py` (novo)

**Interfaces:**
- Produces: `resize_image_for_device(image_bytes: bytes, max_px: int) -> bytes`
  — sempre JPEG RGB q85, encaixa no quadrado `max_px`, nunca amplia. Síncrona
  (a Task 7 usa via `run_in_executor`).

- [ ] **Step 1: Instalar Pillow e registrar a dependência**

```bash
pip install "Pillow>=10.4.0"
```

Adicionar linha `Pillow>=10.4.0` ao final de `backend/requirements.txt`.

- [ ] **Step 2: Escrever os testes que falham**

Criar `backend/tests/test_media_utils.py`:

```python
import io

from PIL import Image

from media_utils import resize_image_for_device


def _png(width, height):
    buf = io.BytesIO()
    Image.new("RGB", (width, height), "red").save(buf, format="PNG")
    return buf.getvalue()


class TestResizeImageForDevice:
    def test_downscales_to_max_px_and_reencodes_jpeg(self):
        out = resize_image_for_device(_png(2000, 1000), 1280)
        img = Image.open(io.BytesIO(out))
        assert img.format == "JPEG"
        assert img.size == (1280, 640)

    def test_never_upscales_but_still_jpeg(self):
        out = resize_image_for_device(_png(600, 400), 1280)
        img = Image.open(io.BytesIO(out))
        assert img.format == "JPEG"
        assert img.size == (600, 400)

    def test_portrait_respects_longest_side(self):
        out = resize_image_for_device(_png(1000, 2000), 1280)
        img = Image.open(io.BytesIO(out))
        assert img.size == (640, 1280)
```

- [ ] **Step 3: Rodar e ver falhar**

Run: `python -m pytest tests/test_media_utils.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'media_utils'`.

- [ ] **Step 4: Implementar**

Criar `backend/media_utils.py`:

```python
"""Image transforms for the device profile (contrato v1.1)."""

import io

from PIL import Image


def resize_image_for_device(image_bytes: bytes, max_px: int) -> bytes:
    """Fit the image inside a max_px square and re-encode as JPEG q85.

    Never upscales. Always returns JPEG (RGB) — even when the source already
    fits — so the device needs a single decoder path. Synchronous and
    CPU-bound: callers on the event loop must use run_in_executor.
    """
    img = Image.open(io.BytesIO(image_bytes))
    img = img.convert("RGB")
    img.thumbnail((max_px, max_px), Image.LANCZOS)
    out = io.BytesIO()
    img.save(out, format="JPEG", quality=85)
    return out.getvalue()
```

- [ ] **Step 5: Rodar testes**

Run: `python -m pytest tests/test_media_utils.py -v && python -m pytest tests/ -q`
Expected: tudo PASS.

- [ ] **Step 6: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add backend/media_utils.py backend/requirements.txt backend/tests/test_media_utils.py
git commit -m "media_utils: redimensionamento JPEG para a tela do dispositivo (Pillow)"
```

## Task 6: Armazenamento de mídia do turno + `GET /api/media/{filename}`

**Files:**
- Create: `backend/media_store.py`
- Modify: `backend/main.py` (novo endpoint + imports)
- Test: `backend/tests/test_media_endpoint.py` (novo)

**Interfaces:**
- Produces: `save_turn_media(media_dir: Path, audio_bytes, image_bytes, audio_ext, image_ext) -> tuple[str, str]`
  (URLs relativas; NUNCA apaga arquivos anteriores);
  `cleanup_turn_media(media_dir: Path, keep: set[str]) -> None` (best-effort
  integral — enumeração, inspeção e remoção); `MEDIA_CONTENT_TYPES`;
  endpoint `GET /api/media/{filename}`. A Task 7 consome `save_turn_media`;
  a Task 8 consome `cleanup_turn_media`.

- [ ] **Step 1: Escrever os testes que falham**

Criar `backend/tests/test_media_endpoint.py`:

```python
from pathlib import Path

import pytest
import pytest_asyncio

import main
from media_store import save_turn_media, cleanup_turn_media


@pytest_asyncio.fixture
async def data_dir(tmp_path, monkeypatch):
    d = tmp_path / "data"
    d.mkdir(parents=True, exist_ok=True)
    monkeypatch.setattr(main, "DATA_DIR", d)
    return d


class TestSaveTurnMedia:
    @pytest.mark.asyncio
    async def test_saves_and_returns_relative_urls(self, data_dir):
        audio_url, image_url = await save_turn_media(
            data_dir / "media", b"AAA", b"III", "wav", "jpg"
        )
        assert audio_url.startswith("/api/media/turn_")
        assert audio_url.endswith("_audio.wav")
        assert image_url.endswith("_image.jpg")
        name = audio_url.rsplit("/", 1)[1]
        assert (data_dir / "media" / name).read_bytes() == b"AAA"

    @pytest.mark.asyncio
    async def test_save_does_not_delete_previous_files(self, data_dir):
        media = data_dir / "media"
        media.mkdir(parents=True)
        stale = media / ("turn_" + "0" * 32 + "_audio.mp3")
        stale.write_bytes(b"old")
        await save_turn_media(media, b"AAA", b"III", "mp3", "png")
        assert stale.exists()
        assert len(list(media.iterdir())) == 3

    @pytest.mark.asyncio
    async def test_second_write_failure_preserves_previous_set(
        self, data_dir, monkeypatch
    ):
        import media_store
        media = data_dir / "media"
        media.mkdir(parents=True)
        old = media / ("turn_" + "0" * 32 + "_audio.mp3")
        old.write_bytes(b"old")
        real_open = media_store.aiofiles.open
        calls = {"n": 0}

        def failing_open(path, mode="r", *args, **kwargs):
            calls["n"] += 1
            if calls["n"] == 2:
                raise OSError("disk full")
            return real_open(path, mode, *args, **kwargs)

        monkeypatch.setattr(media_store.aiofiles, "open", failing_open)
        with pytest.raises(OSError):
            await media_store.save_turn_media(media, b"A", b"I", "wav", "jpg")
        assert old.exists()


class TestCleanupTurnMedia:
    def test_cleanup_removes_only_unreferenced(self, tmp_path):
        media = tmp_path / "media"
        media.mkdir(parents=True)
        keep_name = "turn_" + "a" * 32 + "_audio.wav"
        (media / keep_name).write_bytes(b"new")
        (media / ("turn_" + "0" * 32 + "_audio.mp3")).write_bytes(b"old")
        cleanup_turn_media(media, keep={keep_name})
        assert (media / keep_name).exists()
        assert len(list(media.iterdir())) == 1

    def test_cleanup_swallows_directory_enumeration_failure(
        self, tmp_path, monkeypatch
    ):
        media = tmp_path / "media"
        media.mkdir()

        def fail_iterdir(_self):
            raise OSError("I/O error")

        monkeypatch.setattr(Path, "iterdir", fail_iterdir)
        cleanup_turn_media(media, keep=set())  # Must not raise.


class TestGetMediaEndpoint:
    @pytest.mark.asyncio
    async def test_serves_stored_file_with_content_type(self, client, data_dir):
        media = data_dir / "media"
        media.mkdir(parents=True)
        name = "turn_" + "a" * 32 + "_audio.wav"
        (media / name).write_bytes(b"RIFFxxxx")
        resp = await client.get(f"/api/media/{name}")
        assert resp.status_code == 200
        assert resp.headers["content-type"].startswith("audio/wav")
        assert resp.content == b"RIFFxxxx"

    @pytest.mark.asyncio
    async def test_unknown_or_malformed_name_is_404(self, client, data_dir):
        assert (await client.get("/api/media/state.json")).status_code == 404
        assert (await client.get("/api/media/turn_zz_audio.mp3")).status_code == 404
        missing = "turn_" + "b" * 32 + "_image.png"
        assert (await client.get(f"/api/media/{missing}")).status_code == 404
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `python -m pytest tests/test_media_endpoint.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'media_store'`.

- [ ] **Step 3: Implementar o módulo**

Criar `backend/media_store.py`:

```python
"""Turn media files served by GET /api/media (contrato v1.1).

Writing NEVER deletes previous files: the previous idempotent response may
still reference them. Cleanup is a separate, best-effort step the caller runs
only AFTER the new turn's response is durably stored (or superseded), inside
the turn lock.
"""

import uuid
from pathlib import Path

import aiofiles

MEDIA_CONTENT_TYPES = {
    "mp3": "audio/mpeg",
    "wav": "audio/wav",
    "png": "image/png",
    "jpg": "image/jpeg",
}


async def save_turn_media(
    media_dir: Path,
    audio_bytes: bytes,
    image_bytes: bytes,
    audio_ext: str,
    image_ext: str,
) -> tuple[str, str]:
    """Persist this turn's media and return relative URLs (/api/media/<name>).

    Does NOT delete anything. A failure here leaves at most one orphan file
    from this turn; the previous turn's set stays intact and servable.
    """
    media_dir.mkdir(parents=True, exist_ok=True)
    turn_id = uuid.uuid4().hex
    audio_name = f"turn_{turn_id}_audio.{audio_ext}"
    image_name = f"turn_{turn_id}_image.{image_ext}"
    async with aiofiles.open(media_dir / audio_name, "wb") as f:
        await f.write(audio_bytes)
    async with aiofiles.open(media_dir / image_name, "wb") as f:
        await f.write(image_bytes)
    return f"/api/media/{audio_name}", f"/api/media/{image_name}"


def cleanup_turn_media(media_dir: Path, keep: set[str]) -> None:
    """Best-effort removal; never fail a turn already consolidated."""
    try:
        files = tuple(media_dir.iterdir()) if media_dir.exists() else ()
    except OSError:
        return
    for file_path in files:
        try:
            if file_path.name not in keep:
                file_path.unlink()
        except OSError:
            pass
```

- [ ] **Step 4: Implementar o endpoint**

Em `backend/main.py`: no import de `fastapi.responses`, incluir `FileResponse`
(`from fastapi.responses import FileResponse, JSONResponse`); adicionar
`from media_store import save_turn_media, cleanup_turn_media, MEDIA_CONTENT_TYPES`.
Após o endpoint `get_lesson`, adicionar:

```python
# Strict allow-list: only names produced by save_turn_media are servable.
_MEDIA_NAME_RE = re.compile(r"^turn_[0-9a-f]{32}_(audio|image)\.(mp3|wav|png|jpg)$")


@app.get("/api/media/{filename}")
async def get_media(filename: str):
    """Serve the latest turn's media files (contrato v1.1, media=url)."""
    if not _MEDIA_NAME_RE.fullmatch(filename):
        raise HTTPException(404, detail="Media not found")
    path = DATA_DIR / "media" / filename
    if not path.exists():
        raise HTTPException(404, detail="Media not found")
    ext = filename.rsplit(".", 1)[1]
    return FileResponse(path, media_type=MEDIA_CONTENT_TYPES[ext])
```

- [ ] **Step 5: Rodar testes + suíte inteira**

Run: `python -m pytest tests/test_media_endpoint.py -v && python -m pytest tests/ -q`
Expected: tudo PASS.

- [ ] **Step 6: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add backend/media_store.py backend/main.py backend/tests/test_media_endpoint.py
git commit -m "Mídia do turno em disco + GET /api/media/{filename} (escrita nunca apaga)"
```

## Task 7: Campos de dispositivo no `POST /api/turn` (v1 literal — D1)

**Files:**
- Modify: `backend/models.py` (`TutoringResponse`)
- Modify: `backend/main.py` (assinatura e corpo de `tutoring_turn`)
- Test: `backend/tests/test_turn_device.py` (novo)

**Interfaces:**
- Consumes: `synthesize_speech(..., output_format=)` (Task 4),
  `resize_image_for_device` (Task 5), `save_turn_media` (Task 6).
- Produces: campos v1.1 na resposta SOMENTE no perfil v1.1 (`is_device_profile`);
  requisição v1 recebe exatamente as chaves v1 (serialização com exclusão);
  validação do `request_id` (1–128, não branco). Idempotência em si é a Task 8.

- [ ] **Step 1: Escrever os testes que falham**

Criar `backend/tests/test_turn_device.py`:

```python
import io
import json
import wave
from unittest.mock import AsyncMock, patch

import pytest
from PIL import Image

from models import GeminiEvaluation


V1_KEYS = {
    "veredicto", "texto_explicacao", "audio_base64", "image_base64",
    "session_status", "current_item", "current_tarefa",
    "wrong_answer_count", "adult_intervention_required",
}


def _png(width, height):
    buf = io.BytesIO()
    Image.new("RGB", (width, height), "blue").save(buf, format="PNG")
    return buf.getvalue()


def _teach_eval():
    return GeminiEvaluation(
        veredicto="teach",
        texto_explicacao="Vamos pensar juntos!",
        prompt_visual="A child thinking about numbers",
    )


def _turn_files():
    return {"audio": ("rec.wav", b"fake-wav-bytes", "audio/wav")}


def _wav_16k():
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(16000)
        wav_file.writeframes(b"\x00\x00" * 80)
    return buf.getvalue()


@pytest.fixture
def mock_tts():
    async def fake_synthesize(text, output_format="mp3", **kwargs):
        return _wav_16k() if output_format == "wav" else b"fake-mp3-data"

    mock = AsyncMock(side_effect=fake_synthesize)
    with patch("main.synthesize_speech", new=mock):
        yield mock


class TestTurnDeviceProfile:
    @pytest.mark.asyncio
    async def test_media_url_returns_urls_and_empty_base64(
        self, client, setup_session, mock_tts
    ):
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())), \
             patch("main.generate_image", new=AsyncMock(return_value=_png(1600, 1000))):
            resp = await client.post(
                "/api/turn",
                data={
                    "session_id": "s1",
                    "media": "url",
                    "audio_format": "wav",
                    "image_max_px": "1280",
                },
                files=_turn_files(),
            )
        assert resp.status_code == 200
        body = resp.json()
        assert body["audio_base64"] == ""
        assert body["image_base64"] == ""
        assert body["audio_url"].endswith("_audio.wav")
        assert body["image_url"].endswith("_image.jpg")
        assert body["audio_format"] == "wav"
        assert body["image_format"] == "jpg"

        audio_resp = await client.get(body["audio_url"])
        assert audio_resp.status_code == 200
        assert audio_resp.content[0:4] == b"RIFF"

        image_resp = await client.get(body["image_url"])
        assert image_resp.status_code == 200
        img = Image.open(io.BytesIO(image_resp.content))
        assert img.format == "JPEG"
        assert max(img.size) <= 1280

    @pytest.mark.asyncio
    async def test_invalid_device_fields_are_400(self, client, setup_session):
        resp = await client.post(
            "/api/turn",
            data={"session_id": "s1", "media": "streaming"},
            files=_turn_files(),
        )
        assert resp.status_code == 400
        resp = await client.post(
            "/api/turn",
            data={"session_id": "s1", "audio_format": "ogg"},
            files=_turn_files(),
        )
        assert resp.status_code == 400
        resp = await client.post(
            "/api/turn",
            data={"session_id": "s1", "image_max_px": "20"},
            files=_turn_files(),
        )
        assert resp.status_code == 400

    @pytest.mark.parametrize("bad_request_id", ["   ", "x" * 129])
    @pytest.mark.asyncio
    async def test_invalid_request_id_is_400(
        self, client, setup_session, bad_request_id
    ):
        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("must not run"))):
            resp = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": bad_request_id},
                files=_turn_files(),
            )
        assert resp.status_code == 400


class TestV1Parity:
    @pytest.mark.asyncio
    async def test_v1_request_returns_exactly_v1_keys(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            resp = await client.post(
                "/api/turn", data={"session_id": "s1"}, files=_turn_files()
            )
        assert resp.status_code == 200
        body = resp.json()
        assert set(body.keys()) == V1_KEYS
        assert body["audio_base64"] != ""
        assert body["image_base64"] != ""

    @pytest.mark.asyncio
    async def test_any_v11_field_enables_device_profile_keys(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            resp = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-solo"},
                files=_turn_files(),
            )
        body = resp.json()
        assert set(body.keys()) == V1_KEYS | {
            "audio_url", "image_url", "audio_format", "image_format", "request_id"
        }
        assert body["request_id"] == "req-solo"
        assert body["audio_url"] is None  # media=url não pedido

    @pytest.mark.asyncio
    async def test_empty_request_id_is_v1_omission(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            resp = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": ""},
                files=_turn_files(),
            )
        assert resp.status_code == 200
        assert set(resp.json()) == V1_KEYS
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `python -m pytest tests/test_turn_device.py -v`
Expected: FAIL — 400 esperado vs 200 (campos ignorados) e KeyError `audio_url`.

- [ ] **Step 3: Estender `TutoringResponse`**

Em `backend/models.py`, adicionar ao final da classe `TutoringResponse`:

```python
    audio_url: Optional[str] = Field(
        default=None, description="Relative /api/media URL when media=url was requested"
    )
    image_url: Optional[str] = Field(
        default=None, description="Relative /api/media URL when media=url was requested"
    )
    audio_format: Literal["mp3", "wav"] = "mp3"
    image_format: Literal["png", "jpg"] = "png"
    request_id: Optional[str] = Field(
        default=None, description="Echo of the client idempotency key (contrato v1.1)"
    )
```

- [ ] **Step 4: Estender o handler**

Em `backend/main.py`:

(a) Imports: adicionar `from media_utils import resize_image_for_device`.
Junto às constantes do módulo:

```python
# Response keys that exist only in the v1.1 device profile (D1: a plain v1
# request must receive the literal v1 payload — exactly today's key set).
_V11_RESPONSE_KEYS = {"audio_url", "image_url", "audio_format", "image_format", "request_id"}
```

(b) Assinatura de `tutoring_turn`:

```python
@app.post("/api/turn")
async def tutoring_turn(
    session_id: str = Form(...),
    audio: Optional[UploadFile] = File(None),
    image: Optional[UploadFile] = File(None),
    request_id: Optional[str] = Form(None),
    media: Optional[str] = Form(None),
    audio_format: Optional[str] = Form(None),
    image_max_px: Optional[int] = Form(None),
):
```

(c) Logo após a validação "exactly one input", adicionar (nesta ordem):

```python
    # Contrato v1.1: optional device-profile fields. Absent fields keep the
    # exact v1 path; invalid values fail fast before touching any state.
    if request_id is not None and (
        not request_id.strip() or len(request_id) > 128
    ):
        raise HTTPException(
            400,
            detail="request_id must be non-blank and at most 128 characters",
        )
    if media not in (None, "url"):
        raise HTTPException(400, detail="media must be 'url' when provided")
    if audio_format not in (None, "mp3", "wav"):
        raise HTTPException(400, detail="audio_format must be 'mp3' or 'wav'")
    if image_max_px is not None and not (64 <= image_max_px <= 4096):
        raise HTTPException(400, detail="image_max_px must be between 64 and 4096")
    wants_wav = audio_format == "wav"
    is_device_profile = any(
        v is not None for v in (request_id, media, audio_format, image_max_px)
    )
```

(d) No passo 10 (geração de mídia), substituir APENAS o bloco `try` (mantendo
o `except` CR-01 byte a byte):

```python
        try:
            is_celebration = evaluation.veredicto == "correct"
            image_coro = (
                load_celebration_image()
                if is_celebration
                else generate_image(evaluation.prompt_visual, api_key=settings.google_api_key)
            )
            audio_bytes, image_bytes = await asyncio.gather(
                synthesize_speech(
                    evaluation.texto_explicacao,
                    output_format="wav" if wants_wav else "mp3",
                ),
                image_coro,
            )
            image_media_format = "png"
            if image_max_px is not None:
                # CPU-bound Pillow work off the event loop; failures fall into
                # the same CR-01 path below (502, snapshot restored).
                image_bytes = await asyncio.get_event_loop().run_in_executor(
                    None, resize_image_for_device, image_bytes, image_max_px
                )
                image_media_format = "jpg"
            if media == "url":
                audio_url, image_url = await save_turn_media(
                    DATA_DIR / "media",
                    audio_bytes,
                    image_bytes,
                    "wav" if wants_wav else "mp3",
                    image_media_format,
                )
            else:
                audio_url = image_url = None
        except Exception as exc:
```

(e) Substituir as duas linhas de encode base64 por:

```python
        if media == "url":
            audio_b64 = image_b64 = ""
        else:
            audio_b64 = base64.b64encode(audio_bytes).decode()
            image_b64 = base64.b64encode(image_bytes).decode()
```

(f) No passo 15, substituir `return TutoringResponse(` por
`response = TutoringResponse(`, acrescentar os campos novos ao construtor
(mantendo os existentes intactos):

```python
        audio_url=audio_url,
        image_url=image_url,
        audio_format="wav" if wants_wav else "mp3",
        image_format=image_media_format,
        request_id=request_id,
```

e encerrar o handler com:

```python
    if not is_device_profile:
        # D1: literal v1 payload — same key set as before v1.1 existed.
        return JSONResponse(content=response.model_dump(exclude=_V11_RESPONSE_KEYS))
    return response
```

(g) Tornar o artefato de debug coerente com o formato sintetizado, sem alterar
o conteúdo pedagógico. Adicionar
`output_audio_format: Literal["mp3", "wav"] = "mp3"` a `_write_debug_turn`,
salvar em `last_output.{output_audio_format}` e, no passo 14, passar
`output_audio_format="wav" if wants_wav else "mp3"`. Nunca salvar chave,
headers ou payload da Fish.

(h) Atualizar somente a terminologia dos três comentários/docstrings legados
de `main.py`: “Polly MP3” → “TTS audio”, “Polly TTS + Nano Banana 2 image” →
“TTS audio + Nano Banana 2 image” e “Polly/image” → “TTS/image”. Isso não
altera fluxo, assinatura ou lógica pedagógica.

(Starlette serializa `JSONResponse` com `ensure_ascii=False`, compacto — o
mesmo render do caminho padrão do FastAPI; o payload v1 permanece literal.)

- [ ] **Step 5: Rodar testes novos + suíte inteira**

Run: `python -m pytest tests/test_turn_device.py -v && python -m pytest tests/ -q`
Expected: tudo PASS.

- [ ] **Step 6: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add backend/models.py backend/main.py backend/tests/test_turn_device.py
git commit -m "/api/turn: perfil de dispositivo com resposta v1 literal (D1)"
```

## Task 8: Idempotência fail-safe por `request_id`

**Files:**
- Modify: `backend/main.py`
- Modify: `backend/persistence.py`
- Test: `backend/tests/test_turn_device.py`
- Test: `backend/tests/test_persistence.py`

**Interfaces:**
- Consumes: campos e resposta da Task 7; `cleanup_turn_media` (Task 6).
- Produces: cache `data/last_turn_response.json` com schema
  `seen_request_ids`/`rehydration_required`/`last`; helpers
  `_validate_turn_cache`, `_read_turn_cache_validated`,
  `_replace_corrupt_turn_cache`, `_quarantine_turn_cache`,
  `_supersede_turn_cache` (a Task 9 consome este último);
  `persistence.write_json_atomic`.

- [ ] **Step 1: `persistence.write_json_atomic` (função ADITIVA)**

Adicionar a `backend/persistence.py` (com `import contextlib`, `import os`,
`import tempfile` no topo):

```python
async def write_json_atomic(filepath: Path, data: dict) -> None:
    """Write JSON via temp file + os.replace so readers never see a torn file.

    Synchronous I/O on purpose: payloads are small and the caller holds the
    turn lock — atomicity matters more than yielding here.
    """
    filepath.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_path = tempfile.mkstemp(dir=filepath.parent, suffix=".tmp")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(json.dumps(data, indent=2, ensure_ascii=False))
        os.replace(tmp_path, filepath)
    except BaseException:
        with contextlib.suppress(FileNotFoundError):
            os.unlink(tmp_path)
        raise
```

Teste (APENAS adicionar a `backend/tests/test_persistence.py`):

```python
@pytest.mark.asyncio
async def test_write_json_atomic_roundtrip(tmp_path):
    from persistence import read_json, write_json_atomic
    target = tmp_path / "cache.json"
    await write_json_atomic(target, {"a": 1})
    assert await read_json(target) == {"a": 1}
    assert list(tmp_path.glob("*.tmp")) == []
```

- [ ] **Step 2: Helpers do cache em `main.py`**

Adicionar `import contextlib` e `import shutil` ao topo de `main.py`, e
`write_json_atomic` ao import de `persistence`
(`from persistence import read_json, write_json, write_json_atomic`).
Posicionar os helpers **acima de `update_state`**:

```python
_TURN_CACHE_NAME = "last_turn_response.json"

_FAILSAFE_409 = (
    "Turno com este request_id pode já ter sido aplicado — "
    "re-hidrate /api/state e envie um novo turno com request_id novo"
)


def _turn_cache_path() -> Path:
    return DATA_DIR / _TURN_CACHE_NAME


def _validate_turn_cache(raw: object) -> tuple[list[str], dict | None, bool]:
    """Validate the whole cache before callers inspect any nested field."""
    if not isinstance(raw, dict):
        raise ValueError("turn cache must be an object")

    seen = raw.get("seen_request_ids")
    last = raw.get("last")
    rehydration_required = raw.get("rehydration_required")
    if (
        not isinstance(seen, list)
        or not all(isinstance(value, str) and value for value in seen)
        or len(seen) != len(set(seen))
        or not isinstance(rehydration_required, bool)
    ):
        raise ValueError("invalid cache ledger")
    if last is None:
        return seen, None, rehydration_required
    if not isinstance(last, dict):
        raise ValueError("invalid last record")

    cached_id = last.get("request_id")
    status = last.get("status")
    if (
        not isinstance(cached_id, str)
        or not cached_id
        or cached_id not in seen
        or status not in {"processing", "done", "indeterminate"}
    ):
        raise ValueError("invalid last record")
    if status == "done" and not isinstance(last.get("response"), dict):
        raise ValueError("done cache has no response object")
    if status != "done" and "response" in last:
        raise ValueError("non-done cache must not contain response")
    if rehydration_required:
        raise ValueError("rehydration sentinel must not have a last record")
    return seen, last, False


async def _read_turn_cache_validated() -> tuple[list[str], dict | None, bool]:
    path = _turn_cache_path()
    if not path.exists():
        # read_json returns {} for a missing file; distinguish that from an
        # existing but invalid empty object.
        return [], None, False
    return _validate_turn_cache(await read_json(path))


async def _replace_corrupt_turn_cache(replacement: dict) -> None:
    """Copy a bad cache for diagnosis, then atomically replace the original.

    Copy-before-replace is intentional: a crash at any point leaves either the
    invalid original or the canonical replacement at the live path — never a
    missing cache that could be mistaken for a fresh installation.
    """
    path = _turn_cache_path()
    corrupt_path = path.with_suffix(".corrupt.json")
    with contextlib.suppress(OSError):
        corrupt_path.unlink()
    shutil.copy2(path, corrupt_path)
    await write_json_atomic(path, replacement)


async def _quarantine_turn_cache(
    request_id: str,
    *,
    seen_ids: list[str] | None = None,
) -> None:
    """Replace an unreadable/invalid cache with an 'indeterminate' record.

    The id that hit the bad cache stays blocked (it may be the very id the
    lost record referred to), so retrying it keeps failing safe; any NEW
    request_id processes normally after the device obeys the 409 instruction.
    When only the cached response is invalid, preserve the already validated
    ledger instead of forgetting older ids.
    """
    known_seen = list(seen_ids or [])
    if request_id not in known_seen:
        known_seen.append(request_id)
    await _replace_corrupt_turn_cache({
        "seen_request_ids": known_seen,
        "rehydration_required": False,
        "last": {"request_id": request_id, "status": "indeterminate"},
    })


async def _supersede_turn_cache() -> None:
    """Make every previously stored response non-replayable.

    Called under _state_lock before a state mutation that does not belong to
    the cached response: v1 turn, new lesson, adult resolve, expiration, or a
    pre-marker 502. A missing cache needs no write, preserving the exact v1
    path until a device cache has actually existed.
    """
    path = _turn_cache_path()
    if not path.exists():
        return
    try:
        seen, _, rehydration_required = await _read_turn_cache_validated()
    except Exception:
        # Unknown ids cannot be recovered. Force the NEXT device request to
        # receive 409 before any new id is accepted; never guess that it is new.
        await _replace_corrupt_turn_cache({
            "seen_request_ids": [],
            "rehydration_required": True,
            "last": None,
        })
        return
    await write_json_atomic(path, {
        "seen_request_ids": seen,
        "rehydration_required": rehydration_required,
        "last": None,
    })
```

(Como `shutil` entra em `main.py` aqui, a Task 9 não deve duplicar o import.)

- [ ] **Step 3: Mutações de estado fora de `/api/turn`**

(a) Substituir o final de `update_state` (ainda dentro de `_state_lock`) por:

```python
        new_state = transform_fn(state)
        new_state.updated_at = datetime.utcnow().isoformat()
        # adult/resolve (único consumidor atual de update_state) supersedes
        # any tutoring response before publishing its state transition.
        await _supersede_turn_cache()
        await write_json(DATA_DIR / "state.json", new_state.model_dump())
        return new_state
```

(b) Substituir integralmente `get_state` por:

```python
@app.get("/api/state")
async def get_state():
    raw = await read_json(DATA_DIR / "state.json")
    if not raw:
        return {"status": "no_session"}
    try:
        session = SessionState.model_validate(raw)
        must_expire = check_session_expired(session)
    except Exception:
        return {
            "status": "invalid_state",
            "error": "state.json failed validation — re-prepare lesson",
        }
    if not must_expire:
        return session.model_dump()

    # Re-read after acquiring the turn lock: /api/turn or /api/prepare may
    # have changed the session while this GET was deciding to expire it.
    async with _state_lock:
        current_raw = await read_json(DATA_DIR / "state.json")
        if not current_raw:
            return {"status": "no_session"}
        try:
            session = SessionState.model_validate(current_raw)
            must_expire = check_session_expired(session)
        except Exception:
            return {
                "status": "invalid_state",
                "error": "state.json failed validation — re-prepare lesson",
            }
        if must_expire:
            session = session.model_copy(deep=True)
            session.session_status = "expired"
            session.updated_at = datetime.utcnow().isoformat()
            # Persistence failures must propagate as server errors; they are
            # not validation failures and state remains unmodified.
            await _supersede_turn_cache()
            await write_json(
                DATA_DIR / "state.json",
                session.model_dump(),
            )
        return session.model_dump()
```

(c) Nos dois handlers de erro 502 de `/api/turn` (exceção de Gemini no passo 8
e exceção de mídia no passo 10), ADICIONAR imediatamente antes do respectivo
`await write_json(...state.json...)`:

```python
            # This failed attempt persists a technical-failure mutation, so
            # any response from an earlier successful turn is now stale.
            # Do not reserve the current request_id: failure happened before
            # its processing marker, and that same id remains eligible.
            await _supersede_turn_cache()
```

- [ ] **Step 4: Replay fail-safe** — primeira coisa DENTRO de
`async with _state_lock:` no handler do turno:

```python
        seen: list[str] = []
        if request_id:
            try:
                seen, last, rehydration_required = (
                    await _read_turn_cache_validated()
                )
            except Exception:
                await _quarantine_turn_cache(request_id)
                raise HTTPException(409, detail=_FAILSAFE_409)
            if rehydration_required:
                # Corruption was discovered by a v1 turn/new lesson, when no
                # current device id existed. Block and remember this first id;
                # a different id after re-hydration may then proceed.
                seen = [*seen, request_id]
                await write_json_atomic(_turn_cache_path(), {
                    "seen_request_ids": seen,
                    "rehydration_required": False,
                    "last": {
                        "request_id": request_id,
                        "status": "indeterminate",
                    },
                })
                raise HTTPException(409, detail=_FAILSAFE_409)
            if last is not None and last["request_id"] == request_id:
                if last["status"] == "done":
                    try:
                        cached_response = TutoringResponse.model_validate(
                            last["response"]
                        )
                        if cached_response.request_id != request_id:
                            raise ValueError("cached response request_id mismatch")
                        return cached_response
                    except Exception:
                        # Valid JSON, invalid schema: same quarantine, no 500.
                        await _quarantine_turn_cache(
                            request_id,
                            seen_ids=seen,
                        )
                        raise HTTPException(409, detail=_FAILSAFE_409)
                # "processing" / "indeterminate": may already be applied.
                raise HTTPException(409, detail=_FAILSAFE_409)
            if request_id in seen:
                # Reserved/applied by an older turn; response no longer stored.
                raise HTTPException(409, detail=_FAILSAFE_409)
```

- [ ] **Step 5: Barreira de supersessão** — após o sucesso do passo 10, antes
do passo 11:

```python
        if request_id:
            # No truncation: dropping an id would permit silent reprocessing.
            seen = [*seen, request_id]
            await write_json_atomic(_turn_cache_path(), {
                "seen_request_ids": seen,
                "rehydration_required": False,
                "last": {"request_id": request_id, "status": "processing"},
            })
        else:
            # Must succeed BEFORE counters/state/diary can change. If it fails,
            # the v1 turn has not been applied and the previous replay survives.
            await _supersede_turn_cache()
```

- [ ] **Step 6: Consolidação DENTRO do lock** — após o passo 13b, mover a
construção do `response` para dentro do lock e completar:

```python
        # Build the response and store the idempotent record INSIDE the lock:
        # a queued retry must find "done" the moment the lock frees.
        response = TutoringResponse(
            veredicto=evaluation.veredicto,
            texto_explicacao=evaluation.texto_explicacao,
            audio_base64=audio_b64,
            image_base64=image_b64,
            session_status=state.session_status,
            current_item=state.current_item,
            current_tarefa=state.current_tarefa,
            wrong_answer_count=state.item_progress.get(
                state.current_item, ItemProgress()
            ).tarefas.get(
                state.current_tarefa, TaskProgress()
            ).wrong_answer_count,
            adult_intervention_required=state.adult_intervention_required,
            audio_url=audio_url,
            image_url=image_url,
            audio_format="wav" if wants_wav else "mp3",
            image_format=image_media_format,
            request_id=request_id,
        )
        if request_id:
            await write_json_atomic(_turn_cache_path(), {
                "seen_request_ids": seen,
                "rehydration_required": False,
                "last": {
                    "request_id": request_id,
                    "status": "done",
                    "response": response.model_dump(),
                },
            })
        # Cleanup INSIDE the lock: after the cache is durably updated, or was
        # superseded at the barrier, and never concurrent with the next turn's
        # save_turn_media. cleanup_turn_media is fully best-effort.
        if media == "url":
            cleanup_turn_media(
                DATA_DIR / "media",
                keep={audio_url.rsplit("/", 1)[1], image_url.rsplit("/", 1)[1]},
            )
    # ---- lock released here ----
```

O passo 14 (debug) permanece fora do lock; o retorno final permanece o da
Task 7 Step 4(f) (`if not is_device_profile: ... return response`).

- [ ] **Step 7: Testes**

Adicionar a `tests/test_turn_device.py` a classe `TestTurnIdempotency`
completa (o arquivo já tem `import json`):

```python
class TestTurnIdempotency:
    @pytest.mark.asyncio
    async def test_same_request_id_replays_without_processing(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        data = {"session_id": "s1", "request_id": "req-abc-1"}
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            first = await client.post("/api/turn", data=data, files=_turn_files())
        assert first.status_code == 200
        assert mock_tts.call_count == 1
        gen_calls = mock_image_gen.aio.models.generate_content.call_count

        must_not_run = AsyncMock(side_effect=AssertionError("evaluate_turn on replay"))
        with patch("main.evaluate_turn", new=must_not_run):
            second = await client.post("/api/turn", data=data, files=_turn_files())
        assert second.status_code == 200
        assert second.json() == first.json()
        # Exactly-once: no second TTS/image call, no counter double-apply.
        assert mock_tts.call_count == 1
        assert mock_image_gen.aio.models.generate_content.call_count == gen_calls
        state = json.loads((setup_session / "state.json").read_text())
        assert state["usage_counters"] == {
            "llm_calls": 1,
            "image_generation_calls": 1,
            "tts_chars_used": len(_teach_eval().texto_explicacao),
        }
        conv = json.loads(
            (setup_session / "conversation.json").read_text()
        )
        assert len(conv["turns"]) == 1
        assert conv["turns"][0]["veredicto"] == "teach"

    @pytest.mark.asyncio
    async def test_concurrent_same_request_id_processes_once(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        import asyncio
        data = {"session_id": "s1", "request_id": "req-conc"}
        eval_mock = AsyncMock(return_value=_teach_eval())
        with patch("main.evaluate_turn", new=eval_mock):
            r1, r2 = await asyncio.gather(
                client.post("/api/turn", data=data, files=_turn_files()),
                client.post("/api/turn", data=data, files=_turn_files()),
            )
        assert r1.status_code == 200 and r2.status_code == 200
        assert r1.json() == r2.json()
        assert eval_mock.call_count == 1

    @pytest.mark.asyncio
    async def test_processing_marker_fails_safe_409(
        self, client, setup_session
    ):
        from persistence import write_json_atomic
        await write_json_atomic(
            setup_session / "last_turn_response.json",
            {
                "seen_request_ids": ["req-stuck"],
                "rehydration_required": False,
                "last": {
                    "request_id": "req-stuck",
                    "status": "processing",
                },
            },
        )
        must_not_run = AsyncMock(side_effect=AssertionError("must not process"))
        with patch("main.evaluate_turn", new=must_not_run):
            resp = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-stuck"},
                files=_turn_files(),
            )
        assert resp.status_code == 409

    @pytest.mark.asyncio
    async def test_corrupt_cache_fails_safe_and_quarantines(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        (setup_session / "last_turn_response.json").write_text("{{{not json")
        must_not_run = AsyncMock(side_effect=AssertionError("must not process"))
        with patch("main.evaluate_turn", new=must_not_run):
            resp = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-any"},
                files=_turn_files(),
            )
        assert resp.status_code == 409
        assert (setup_session / "last_turn_response.corrupt.json").exists()
        cache = json.loads((setup_session / "last_turn_response.json").read_text())
        assert cache["last"]["status"] == "indeterminate"
        assert cache["rehydration_required"] is False
        # A NEW request_id afterwards processes normally (self-heal).
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            resp = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-new"},
                files=_turn_files(),
            )
        assert resp.status_code == 200

    @pytest.mark.asyncio
    async def test_new_request_id_processes_normally(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            first = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-1"},
                files=_turn_files(),
            )
            second = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-2"},
                files=_turn_files(),
            )
        assert first.status_code == 200 and second.status_code == 200
        assert second.json()["request_id"] == "req-2"

    @pytest.mark.asyncio
    async def test_failed_turn_is_not_stored_for_replay(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        data = {"session_id": "s1", "request_id": "req-fail"}
        with patch("main.evaluate_turn", new=AsyncMock(side_effect=RuntimeError("boom"))):
            first = await client.post("/api/turn", data=data, files=_turn_files())
        assert first.status_code == 502
        # Failure happened BEFORE the intent marker (no media generated), so
        # the same request_id processes normally on retry.
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            second = await client.post("/api/turn", data=data, files=_turn_files())
        assert second.status_code == 200

    @pytest.mark.asyncio
    async def test_v1_turn_supersedes_previous_replay(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            first = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-a"},
                files=_turn_files(),
            )
            assert first.status_code == 200
            v1 = await client.post(
                "/api/turn", data={"session_id": "s1"}, files=_turn_files()
            )
            assert v1.status_code == 200
        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("no reprocess"))):
            stale = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-a"},
                files=_turn_files(),
            )
        assert stale.status_code == 409

    @pytest.mark.asyncio
    async def test_v1_supersession_failure_precedes_state_changes(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            first = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-a"},
                files=_turn_files(),
            )
        assert first.status_code == 200

        state_before = (setup_session / "state.json").read_bytes()
        diary_before = (setup_session / "conversation.json").read_bytes()
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())), \
             patch("main.write_json_atomic", new=AsyncMock(side_effect=OSError("disk"))):
            with pytest.raises(OSError):
                await client.post(
                    "/api/turn",
                    data={"session_id": "s1"},
                    files=_turn_files(),
                )

        assert (setup_session / "state.json").read_bytes() == state_before
        assert (setup_session / "conversation.json").read_bytes() == diary_before
        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("no reprocess"))):
            replay = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-a"},
                files=_turn_files(),
            )
        assert replay.status_code == 200

    @pytest.mark.parametrize("failure_stage", ["llm", "media"])
    @pytest.mark.asyncio
    async def test_pre_marker_502_supersedes_old_replay_but_keeps_id_retryable(
        self, client, setup_session, mock_tts, mock_image_gen, failure_stage
    ):
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            first = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-old"},
                files=_turn_files(),
            )
        assert first.status_code == 200

        if failure_stage == "llm":
            with patch("main.evaluate_turn", new=AsyncMock(side_effect=RuntimeError("llm"))):
                failed = await client.post(
                    "/api/turn",
                    data={"session_id": "s1", "request_id": "req-failed"},
                    files=_turn_files(),
                )
        else:
            with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())), \
                 patch("main.synthesize_speech", new=AsyncMock(side_effect=RuntimeError("tts"))):
                failed = await client.post(
                    "/api/turn",
                    data={"session_id": "s1", "request_id": "req-failed"},
                    files=_turn_files(),
                )
        assert failed.status_code == 502

        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("no reprocess"))):
            stale = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-old"},
                files=_turn_files(),
            )
        assert stale.status_code == 409

        # The failed id itself was never marked processing and remains valid.
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            retry = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-failed"},
                files=_turn_files(),
            )
        assert retry.status_code == 200

    @pytest.mark.parametrize(
        "bad_cache",
        [
            [],
            {
                "seen_request_ids": "req-x",
                "rehydration_required": False,
                "last": None,
            },
            {
                "seen_request_ids": [],
                "rehydration_required": False,
                "last": [],
            },
            {
                "seen_request_ids": [],
                "rehydration_required": "false",
                "last": None,
            },
            {
                "seen_request_ids": ["req-x"],
                "rehydration_required": False,
                "last": {"request_id": "req-x", "status": "unknown"},
            },
        ],
    )
    @pytest.mark.asyncio
    async def test_semantically_invalid_cache_quarantines_409(
        self, client, setup_session, bad_cache
    ):
        (setup_session / "last_turn_response.json").write_text(
            json.dumps(bad_cache)
        )
        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("no reprocess"))):
            resp = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-q"},
                files=_turn_files(),
            )
        assert resp.status_code == 409
        assert (setup_session / "last_turn_response.corrupt.json").exists()

    @pytest.mark.asyncio
    async def test_failed_quarantine_write_keeps_bad_live_cache(
        self, client, setup_session
    ):
        cache_path = setup_session / "last_turn_response.json"
        bad_contents = "{{{not json"
        cache_path.write_text(bad_contents)
        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("no reprocess"))), \
             patch("main.write_json_atomic", new=AsyncMock(side_effect=OSError("disk"))):
            with pytest.raises(OSError):
                await client.post(
                    "/api/turn",
                    data={"session_id": "s1", "request_id": "req-q"},
                    files=_turn_files(),
                )
        # Copy-before-replace closes the crash/failure gap: the live path is
        # still invalid, so the next retry cannot treat it as a fresh cache.
        assert cache_path.read_text() == bad_contents
        assert (setup_session / "last_turn_response.corrupt.json").exists()

    @pytest.mark.asyncio
    async def test_done_with_invalid_schema_quarantines_409(
        self, client, setup_session
    ):
        from persistence import write_json_atomic
        await write_json_atomic(
            setup_session / "last_turn_response.json",
            {
                "seen_request_ids": ["req-old", "req-x"],
                "rehydration_required": False,
                "last": {"request_id": "req-x", "status": "done", "response": {"bogus": 1}},
            },
        )
        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("no reprocess"))):
            resp = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-x"},
                files=_turn_files(),
            )
        assert resp.status_code == 409
        assert (setup_session / "last_turn_response.corrupt.json").exists()
        cache = json.loads(
            (setup_session / "last_turn_response.json").read_text()
        )
        assert cache["seen_request_ids"] == ["req-old", "req-x"]
        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("no reprocess"))):
            old = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-old"},
                files=_turn_files(),
            )
        assert old.status_code == 409

    @pytest.mark.asyncio
    async def test_done_response_request_id_mismatch_quarantines_409(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        from persistence import write_json_atomic
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            first = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-x"},
                files=_turn_files(),
            )
        assert first.status_code == 200
        cache_path = setup_session / "last_turn_response.json"
        cache = json.loads(cache_path.read_text())
        cache["last"]["response"]["request_id"] = "req-other"
        await write_json_atomic(cache_path, cache)

        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("no reprocess"))):
            retry = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-x"},
                files=_turn_files(),
            )
        assert retry.status_code == 409
        assert (setup_session / "last_turn_response.corrupt.json").exists()

    @pytest.mark.asyncio
    async def test_same_request_id_after_quarantine_still_409(
        self, client, setup_session
    ):
        (setup_session / "last_turn_response.json").write_text("{{{not json")
        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("no reprocess"))):
            first = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-q"},
                files=_turn_files(),
            )
            again = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-q"},
                files=_turn_files(),
            )
        assert first.status_code == 409
        assert again.status_code == 409

    @pytest.mark.asyncio
    async def test_corruption_found_by_v1_forces_device_rehydration(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        (setup_session / "last_turn_response.json").write_text("{{{not json")
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            v1 = await client.post(
                "/api/turn",
                data={"session_id": "s1"},
                files=_turn_files(),
            )
        assert v1.status_code == 200

        # No id was available when corruption was found. The first device id
        # is therefore rejected and recorded, never assumed to be new.
        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("no reprocess"))):
            blocked = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-unknown"},
                files=_turn_files(),
            )
        assert blocked.status_code == 409

        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            fresh = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-after-rehydrate"},
                files=_turn_files(),
            )
        assert fresh.status_code == 200

    @pytest.mark.asyncio
    async def test_adult_resolve_supersedes_previous_replay(
        self, client, setup_session, mock_tts, mock_image_gen, monkeypatch
    ):
        import main
        from persistence import write_json
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            first = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-before-adult"},
                files=_turn_files(),
            )
        assert first.status_code == 200
        state = json.loads((setup_session / "state.json").read_text())
        state["adult_intervention_required"] = True
        await write_json(setup_session / "state.json", state)
        monkeypatch.setattr(main.settings, "adult_pin", "1234")

        resolved = await client.post(
            "/api/adult/resolve",
            json={"pin": "1234"},
        )
        assert resolved.status_code == 200
        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("no reprocess"))):
            stale = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-before-adult"},
                files=_turn_files(),
            )
        assert stale.status_code == 409

    @pytest.mark.asyncio
    async def test_adult_resolve_cache_failure_precedes_state_write(
        self, client, setup_session, mock_tts, mock_image_gen, monkeypatch
    ):
        import main
        from persistence import write_json
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            first = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-before-adult"},
                files=_turn_files(),
            )
        assert first.status_code == 200
        state = json.loads((setup_session / "state.json").read_text())
        state["adult_intervention_required"] = True
        await write_json(setup_session / "state.json", state)
        state_before = (setup_session / "state.json").read_bytes()
        monkeypatch.setattr(main.settings, "adult_pin", "1234")

        with patch("main.write_json_atomic", new=AsyncMock(side_effect=OSError("disk"))):
            with pytest.raises(OSError):
                await client.post(
                    "/api/adult/resolve",
                    json={"pin": "1234"},
                )
        assert (setup_session / "state.json").read_bytes() == state_before
        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("no reprocess"))):
            replay = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-before-adult"},
                files=_turn_files(),
            )
        assert replay.status_code == 200

    @pytest.mark.asyncio
    async def test_persisted_expiration_supersedes_previous_replay(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        from persistence import write_json
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            first = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-before-expiry"},
                files=_turn_files(),
            )
        assert first.status_code == 200
        state = json.loads((setup_session / "state.json").read_text())
        state["expires_at"] = "2000-01-01T00:00:00"
        await write_json(setup_session / "state.json", state)

        hydrated = await client.get("/api/state")
        assert hydrated.status_code == 200
        assert hydrated.json()["session_status"] == "expired"
        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("no reprocess"))):
            stale = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-before-expiry"},
                files=_turn_files(),
            )
        assert stale.status_code == 409

    @pytest.mark.asyncio
    async def test_expiration_cache_failure_precedes_state_write(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        from persistence import write_json
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            first = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-before-expiry"},
                files=_turn_files(),
            )
        assert first.status_code == 200
        state = json.loads((setup_session / "state.json").read_text())
        state["expires_at"] = "2000-01-01T00:00:00"
        await write_json(setup_session / "state.json", state)
        state_before = (setup_session / "state.json").read_bytes()

        with patch("main.write_json_atomic", new=AsyncMock(side_effect=OSError("disk"))):
            with pytest.raises(OSError):
                await client.get("/api/state")
        assert (setup_session / "state.json").read_bytes() == state_before
        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("no reprocess"))):
            replay = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-before-expiry"},
                files=_turn_files(),
            )
        assert replay.status_code == 200

    @pytest.mark.asyncio
    async def test_ledger_does_not_evict_old_request_ids(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        from persistence import write_json_atomic
        old_ids = [f"req-old-{i}" for i in range(201)]
        await write_json_atomic(
            setup_session / "last_turn_response.json",
            {
                "seen_request_ids": old_ids,
                "rehydration_required": False,
                "last": None,
            },
        )
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            fresh = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-new"},
                files=_turn_files(),
            )
        assert fresh.status_code == 200
        cache = json.loads(
            (setup_session / "last_turn_response.json").read_text()
        )
        assert cache["seen_request_ids"] == [*old_ids, "req-new"]
        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("no reprocess"))):
            stale = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": old_ids[0]},
                files=_turn_files(),
            )
        assert stale.status_code == 409

    @pytest.mark.asyncio
    async def test_failure_before_consolidation_keeps_previous_replay(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())), \
             patch("main.generate_image", new=AsyncMock(return_value=_png(800, 600))):
            first = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-a", "media": "url"},
                files=_turn_files(),
            )
        assert first.status_code == 200
        audio_url = first.json()["audio_url"]

        # Second turn dies while trying to replace the previous cache with
        # its intent marker. State/diary and the previous cache stay intact.
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())), \
             patch("main.generate_image", new=AsyncMock(return_value=_png(800, 600))), \
             patch("main.write_json_atomic", new=AsyncMock(side_effect=OSError("boom"))):
            with pytest.raises(OSError):
                await client.post(
                    "/api/turn",
                    data={"session_id": "s1", "request_id": "req-b", "media": "url"},
                    files=_turn_files(),
                )

        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("no reprocess"))):
            replay = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-a", "media": "url"},
                files=_turn_files(),
            )
        assert replay.status_code == 200
        assert (await client.get(audio_url)).status_code == 200

    @pytest.mark.asyncio
    async def test_media_url_without_request_id_supersedes_and_cleans(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())), \
             patch("main.generate_image", new=AsyncMock(return_value=_png(800, 600))):
            first = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-a", "media": "url"},
                files=_turn_files(),
            )
            old_audio_url = first.json()["audio_url"]
            second = await client.post(
                "/api/turn",
                data={"session_id": "s1", "media": "url"},
                files=_turn_files(),
            )
        assert second.status_code == 200
        assert second.json()["audio_url"] is not None
        # Old replay superseded (409), and only then its media was removed.
        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("no reprocess"))):
            stale = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-a", "media": "url"},
                files=_turn_files(),
            )
        assert stale.status_code == 409
        assert (await client.get(old_audio_url)).status_code == 404
        assert (await client.get(second.json()["audio_url"])).status_code == 200

    @pytest.mark.asyncio
    async def test_media_cleanup_runs_while_turn_lock_is_held(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        import main
        observations = []

        def record_cleanup(_media_dir, *, keep):
            cache = json.loads(
                (setup_session / "last_turn_response.json").read_text()
            )
            observations.append(
                (main._state_lock.locked(), cache["last"]["status"])
            )

        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())), \
             patch("main.generate_image", new=AsyncMock(return_value=_png(800, 600))), \
             patch("main.cleanup_turn_media", side_effect=record_cleanup):
            resp = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-lock", "media": "url"},
                files=_turn_files(),
            )
        assert resp.status_code == 200
        assert observations == [(True, "done")]
```

- [ ] **Step 8: Rodar testes + suíte inteira**

Run: `python -m pytest tests/test_turn_device.py tests/test_persistence.py -v && python -m pytest tests/ -q`
Expected: tudo PASS.

- [ ] **Step 9: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add backend/main.py backend/persistence.py \
  backend/tests/test_turn_device.py backend/tests/test_persistence.py
git commit -m "/api/turn: idempotência fail-safe por request_id"
```

## Task 9: Preparação página a página + golden v1 + publicação sob lock

**Files:**
- Modify: `backend/main.py` (refatorar `prepare_lesson` extraindo `_run_prepare`;
  3 endpoints novos)
- Test: `backend/tests/test_prepare_staged.py` (novo)

**Interfaces:**
- Consumes: `_supersede_turn_cache` e `write_json_atomic` (Task 8); pipeline v1.
- Produces: `_run_prepare(images, content_types) -> dict`;
  `POST /api/prepare/start`, `POST /api/prepare/page`, `POST /api/prepare/finish`.
- `shutil` já foi importado em `main.py` na Task 8 — NÃO duplicar o import.

- [ ] **Step 1: Escrever os testes (incluindo o golden v1 e supersessão)**

Criar `backend/tests/test_prepare_staged.py`:

```python
import json
from datetime import datetime
from unittest.mock import AsyncMock, patch
from uuid import UUID

import pytest
import pytest_asyncio

import main
from models import (
    LessonExtractionResponse, ExtractedItem, ExtractedTask, GeminiEvaluation,
)


PREPARE_V1_KEYS = {"status", "summary"}
SUMMARY_KEYS = {"lesson_id", "created_at", "source_pages", "items"}
STATE_V1_KEYS = {
    "session_id",
    "student_id",
    "student_name",
    "lesson_id",
    "session_status",
    "waiting_for_photo",
    "adult_intervention_required",
    "created_at",
    "updated_at",
    "expires_at",
    "current_item",
    "current_tarefa",
    "item_progress",
    "usage_counters",
}
EMPTY_TASK_PROGRESS = {
    "status": "pending",
    "image_uri": None,
    "wrong_answer_count": 0,
    "technical_failure_count": 0,
    "clarification_count": 0,
    "completion_source": None,
}


def _extraction_ok():
    return LessonExtractionResponse(
        items=[
            ExtractedItem(
                id="item_1",
                titulo="Exercicio 1",
                enunciado="Some as frações",
                tarefas=[ExtractedTask(id="item_1_task_1", enunciado="1/2 + 1/4 = ?")],
            )
        ]
    )


def _extraction_illegible():
    return LessonExtractionResponse(items=[], illegible_pages=[1])


def _turn_files():
    return {"audio": ("turn.webm", b"fake-audio", "audio/webm")}


def _teach_eval():
    return GeminiEvaluation(
        veredicto="teach",
        texto_explicacao="Vamos entender juntos.",
        prompt_visual="A simple educational diagram",
    )


@pytest.fixture
def mock_tts():
    """Local fixture: fixtures from test_turn_device.py are not cross-module."""
    mock = AsyncMock(return_value=b"fake-mp3-data")
    with patch("main.synthesize_speech", new=mock):
        yield mock


@pytest_asyncio.fixture
async def data_dir(tmp_path, monkeypatch):
    d = tmp_path / "data"
    d.mkdir(parents=True, exist_ok=True)
    monkeypatch.setattr(main, "DATA_DIR", d)
    return d


def _jpeg_file(name="p.jpg"):
    return {"file": (name, b"fake-jpeg-bytes", "image/jpeg")}


async def _start(client):
    resp = await client.post("/api/prepare/start")
    assert resp.status_code == 200
    return resp.json()["upload_id"]


class TestPrepareV1Parity:
    @pytest.mark.asyncio
    async def test_prepare_v1_response_and_artifacts_golden(self, client, data_dir):
        with patch("main.extract_lesson", new=AsyncMock(return_value=_extraction_ok())):
            resp = await client.post(
                "/api/prepare",
                files=[("files", ("p1.jpg", b"fake-jpeg-bytes", "image/jpeg"))],
            )
        assert resp.status_code == 200
        body = resp.json()
        assert set(body) == PREPARE_V1_KEYS
        assert body["status"] == "ready"
        assert set(body["summary"]) == SUMMARY_KEYS

        lesson = json.loads((data_dir / "lesson_tasks.json").read_text())
        assert lesson == body["summary"]
        UUID(lesson["lesson_id"])
        datetime.fromisoformat(lesson["created_at"])
        assert lesson["source_pages"] == [
            str(data_dir / "images" / "page_1.jpg")
        ]
        assert lesson["items"] == {
            "item_1": {
                "titulo": "Exercicio 1",
                "enunciado": "Some as frações",
                "tarefas": {
                    "item_1_task_1": {
                        "enunciado": "1/2 + 1/4 = ?",
                        "origem": None,
                        "disciplina": None,
                        "pagina": None,
                    }
                },
            }
        }

        state = json.loads((data_dir / "state.json").read_text())
        assert set(state) == STATE_V1_KEYS
        UUID(state["session_id"])
        assert state["lesson_id"] == lesson["lesson_id"]
        assert state["student_id"] == "default"
        assert state["student_name"] == "Aluno"
        assert state["session_status"] == "active"
        assert state["waiting_for_photo"] is False
        assert state["adult_intervention_required"] is False
        assert state["current_item"] == "item_1"
        assert state["current_tarefa"] == "item_1_task_1"
        assert state["created_at"] == lesson["created_at"]
        assert state["updated_at"] == lesson["created_at"]
        assert datetime.fromisoformat(state["expires_at"]) > datetime.fromisoformat(
            state["created_at"]
        )
        assert state["item_progress"] == {
            "item_1": {
                "status": "pending",
                "tarefas": {"item_1_task_1": EMPTY_TASK_PROGRESS},
            }
        }
        assert state["usage_counters"] == {
            "llm_calls": 0,
            "image_generation_calls": 0,
            "tts_chars_used": 0,
        }

        conv = json.loads((data_dir / "conversation.json").read_text())
        assert conv == {"lesson_id": lesson["lesson_id"], "turns": []}


class TestPrepareStaged:
    @pytest.mark.asyncio
    async def test_full_flow_ready(self, client, data_dir):
        upload_id = await _start(client)
        for i in range(2):
            resp = await client.post(
                "/api/prepare/page",
                data={"upload_id": upload_id, "index": str(i)},
                files=_jpeg_file(),
            )
            assert resp.status_code == 200
            assert resp.json()["pages_received"] == i + 1
        with patch("main.extract_lesson", new=AsyncMock(return_value=_extraction_ok())):
            resp = await client.post(
                "/api/prepare/finish", data={"upload_id": upload_id}
            )
        assert resp.status_code == 200
        assert resp.json()["status"] == "ready"
        assert (data_dir / "state.json").exists()
        assert not (data_dir / "prepare_staging").exists()

    @pytest.mark.asyncio
    async def test_replace_same_index_overwrites(self, client, data_dir):
        upload_id = await _start(client)
        await client.post(
            "/api/prepare/page",
            data={"upload_id": upload_id, "index": "0"},
            files=_jpeg_file("a.jpg"),
        )
        resp = await client.post(
            "/api/prepare/page",
            data={"upload_id": upload_id, "index": "0"},
            files=_jpeg_file("b.jpg"),
        )
        assert resp.json()["pages_received"] == 1

    @pytest.mark.asyncio
    async def test_missing_index_is_400(self, client, data_dir):
        upload_id = await _start(client)
        for i in (0, 2):
            await client.post(
                "/api/prepare/page",
                data={"upload_id": upload_id, "index": str(i)},
                files=_jpeg_file(),
            )
        resp = await client.post("/api/prepare/finish", data={"upload_id": upload_id})
        assert resp.status_code == 400
        assert "1" in resp.json()["detail"]

    @pytest.mark.asyncio
    async def test_illegible_keeps_staging_for_retry(self, client, data_dir):
        upload_id = await _start(client)
        for i in range(2):
            await client.post(
                "/api/prepare/page",
                data={"upload_id": upload_id, "index": str(i)},
                files=_jpeg_file(),
            )
        with patch(
            "main.extract_lesson", new=AsyncMock(return_value=_extraction_illegible())
        ):
            resp = await client.post(
                "/api/prepare/finish", data={"upload_id": upload_id}
            )
        assert resp.json() == {"status": "illegible", "illegible_pages": [1]}
        assert (data_dir / "prepare_staging").exists()
        # Replace the illegible page and finish again.
        await client.post(
            "/api/prepare/page",
            data={"upload_id": upload_id, "index": "1"},
            files=_jpeg_file("retake.jpg"),
        )
        with patch("main.extract_lesson", new=AsyncMock(return_value=_extraction_ok())):
            resp = await client.post(
                "/api/prepare/finish", data={"upload_id": upload_id}
            )
        assert resp.json()["status"] == "ready"

    @pytest.mark.asyncio
    async def test_unknown_upload_id_is_404(self, client, data_dir):
        resp = await client.post(
            "/api/prepare/page",
            data={"upload_id": "f" * 32, "index": "0"},
            files=_jpeg_file(),
        )
        assert resp.status_code == 404


class TestPrepareCacheSupersession:
    @pytest.mark.asyncio
    async def test_new_lesson_supersedes_previous_replay(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            first = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-a"},
                files=_turn_files(),
            )
        assert first.status_code == 200

        with patch("main.extract_lesson", new=AsyncMock(return_value=_extraction_ok())):
            prep = await client.post(
                "/api/prepare",
                files=[("files", ("p1.jpg", b"fake-jpeg-bytes", "image/jpeg"))],
            )
        assert prep.status_code == 200

        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("no reprocess"))):
            stale = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-a"},
                files=_turn_files(),
            )
        assert stale.status_code == 409

    @pytest.mark.asyncio
    async def test_cache_failure_precedes_new_lesson_publication(
        self, client, setup_session, mock_tts, mock_image_gen
    ):
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            first = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-a"},
                files=_turn_files(),
            )
        assert first.status_code == 200
        lesson_before = (setup_session / "lesson_tasks.json").read_bytes()
        state_before = (setup_session / "state.json").read_bytes()
        diary_before = (setup_session / "conversation.json").read_bytes()

        with patch("main.extract_lesson", new=AsyncMock(return_value=_extraction_ok())), \
             patch("main.write_json_atomic", new=AsyncMock(side_effect=OSError("disk"))):
            with pytest.raises(OSError):
                await client.post(
                    "/api/prepare",
                    files=[("files", ("p1.jpg", b"new-image", "image/jpeg"))],
                )

        assert (setup_session / "lesson_tasks.json").read_bytes() == lesson_before
        assert (setup_session / "state.json").read_bytes() == state_before
        assert (setup_session / "conversation.json").read_bytes() == diary_before
        with patch("main.evaluate_turn", new=AsyncMock(side_effect=AssertionError("no reprocess"))):
            replay = await client.post(
                "/api/turn",
                data={"session_id": "s1", "request_id": "req-a"},
                files=_turn_files(),
            )
        assert replay.status_code == 200

    @pytest.mark.asyncio
    async def test_prepare_publication_order_and_lock(self, client, data_dir):
        import main
        from persistence import write_json as real_write_json
        events = []

        async def record_supersession():
            events.append(("cache", main._state_lock.locked()))

        async def record_write(path, data):
            if path.name in {
                "lesson_tasks.json",
                "state.json",
                "conversation.json",
            }:
                events.append((path.name, main._state_lock.locked()))
            await real_write_json(path, data)

        with patch("main.extract_lesson", new=AsyncMock(return_value=_extraction_ok())), \
             patch("main._supersede_turn_cache", new=record_supersession), \
             patch("main.write_json", new=record_write):
            prep = await client.post(
                "/api/prepare",
                files=[("files", ("p1.jpg", b"new-image", "image/jpeg"))],
            )
        assert prep.status_code == 200
        assert events == [
            ("cache", True),
            ("lesson_tasks.json", True),
            ("state.json", True),
            ("conversation.json", True),
        ]
```

- [ ] **Step 2: Ordem TDD obrigatória**

1. Com o arquivo criado, rodar SOMENTE o golden contra a V1 atual:
   `python -m pytest "tests/test_prepare_staged.py::TestPrepareV1Parity::test_prepare_v1_response_and_artifacts_golden" -q`
   — deve **PASSAR** contra o código atual (prova pré-refatoração).
   (Exceção documentada: `test_prepare_publication_order_and_lock` e os testes
   de supersessão dependem da refatoração e só passam depois.)
2. Rodar o arquivo completo — deve FALHAR (rotas staged inexistentes).
3. Implementar (Steps 3–4).
4. Rodar o golden isolado de novo e depois a suíte completa.

- [ ] **Step 3: Refatorar `prepare_lesson` extraindo `_run_prepare`**

`prepare_lesson` mantém as validações v1 (contagem ≤20, MIME, ≤10 MB)
coletando `images` e `content_types = [f.content_type for f in files]`, e
termina com `return await _run_prepare(images, content_types)`. Criar,
imediatamente antes de `prepare_lesson`, a função completa (corpo movido de
`prepare_lesson`, com a publicação final sob lock e supersessão):

```python
async def _run_prepare(images: list[bytes], content_types: list[str]) -> dict:
    """Shared /api/prepare pipeline (v1 batch and v1.1 staged finish).

    Saves pages to data/images, extracts via Gemini, and publishes
    lesson_tasks.json + state.json + conversation.json under _state_lock,
    superseding any replayable turn response first.
    """
    image_paths: list[str] = []
    image_dir = DATA_DIR / "images"
    image_dir.mkdir(parents=True, exist_ok=True)
    for i, (content, ctype) in enumerate(zip(images, content_types)):
        ext = ".jpg" if ctype == "image/jpeg" else ".png"
        image_path = image_dir / f"page_{i+1}{ext}"
        async with aiofiles.open(image_path, "wb") as f:
            await f.write(content)
        image_paths.append(str(image_path))

    # Extract lesson via Gemini
    result = await extract_lesson(
        images=images,
        api_key=settings.google_api_key,
        model=settings.gemini_model,
    )

    # Handle illegible images (PREP-06)
    if result.illegible_pages:
        return {"status": "illegible", "illegible_pages": result.illegible_pages}

    # Reject empty extraction (PREP-07)
    if not result.items:
        raise HTTPException(422, detail="Could not extract any tasks from the provided images")

    # Generate shared identifiers
    lesson_id = str(uuid.uuid4())
    now = datetime.utcnow()

    # Convert array-based Gemini response to dict-keyed structures
    lesson_items: dict[str, ItemModel] = {}
    for item in result.items:
        task_dict: dict[str, TaskModel] = {}
        for t in item.tarefas:
            task_dict[t.id] = TaskModel(
                enunciado=t.enunciado,
                origem=t.origem,
                disciplina=t.disciplina,
                pagina=t.pagina,
            )
        lesson_items[item.id] = ItemModel(
            titulo=item.titulo,
            enunciado=item.enunciado,
            tarefas=task_dict,
        )

    # Build lesson_tasks.json (dict-keyed canonical format)
    lesson = LessonTasksFile(
        lesson_id=lesson_id,
        created_at=now.isoformat(),
        source_pages=image_paths,
        items=lesson_items,
    )

    # Initialize state.json with full canonical structure
    first_item_id = next(iter(lesson_items))
    first_tarefa_id = next(iter(lesson_items[first_item_id].tarefas))

    # Build item_progress from lesson items
    item_progress: dict[str, ItemProgress] = {}
    for item_id, item in lesson_items.items():
        item_progress[item_id] = ItemProgress(
            status="pending",
            tarefas={tid: TaskProgress() for tid in item.tarefas},
        )

    session = SessionState(
        lesson_id=lesson_id,
        current_item=first_item_id,
        current_tarefa=first_tarefa_id,
        created_at=now.isoformat(),
        updated_at=now.isoformat(),
        expires_at=(now + timedelta(hours=settings.session_ttl_hours)).isoformat(),
        item_progress=item_progress,
    )

    new_conversation = ConversationLog(lesson_id=lesson_id)

    # Extraction and in-memory construction stay outside the lock. Only the
    # consistency boundary is serialized with /api/turn. The old replay is
    # invalidated BEFORE the first canonical file of the new lesson changes.
    async with _state_lock:
        await _supersede_turn_cache()
        await write_json(
            DATA_DIR / "lesson_tasks.json",
            lesson.model_dump(),
        )
        await write_json(
            DATA_DIR / "state.json",
            session.model_dump(),
        )
        await write_json(
            DATA_DIR / "conversation.json",
            new_conversation.model_dump(),
        )

    return {
        "status": "ready",
        "summary": lesson.model_dump(),
    }
```

Notas normativas: os retornos `illegible` e 422 continuam antes do bloco de
publicação e não invalidam lição/cache atuais. As imagens-fonte continuam
sendo gravadas antes da extração, como na V1; não há promessa de transação
para esses arquivos. Garante-se: falha na supersessão não publica os três
JSONs canônicos; após supersessão bem-sucedida, nenhum `request_id` antigo
volta a ser aplicado. A publicação em três JSONs conserva a janela de crash
já existente na V1; o lock impede concorrência dentro do processo, não cria
transação multi-arquivo.

- [ ] **Step 4: Endpoints staged**

Após `prepare_lesson`, adicionar (`shutil` já importado na Task 8):

```python
_UPLOAD_ID_RE = re.compile(r"^[0-9a-f]{32}$")


def _staging_dir(upload_id: str) -> Path:
    return DATA_DIR / "prepare_staging" / upload_id


def _require_staging(upload_id: str) -> Path:
    if not _UPLOAD_ID_RE.fullmatch(upload_id) or not _staging_dir(upload_id).exists():
        raise HTTPException(404, detail="Unknown upload_id — call /api/prepare/start first")
    return _staging_dir(upload_id)


@app.post("/api/prepare/start")
async def prepare_start():
    """Open a page-by-page upload session (contrato v1.1). One at a time."""
    staging_root = DATA_DIR / "prepare_staging"
    if staging_root.exists():
        shutil.rmtree(staging_root)
    upload_id = uuid.uuid4().hex
    _staging_dir(upload_id).mkdir(parents=True, exist_ok=True)
    return {"status": "started", "upload_id": upload_id}


@app.post("/api/prepare/page")
async def prepare_page(
    upload_id: str = Form(...),
    index: int = Form(...),
    file: UploadFile = File(...),
):
    """Store (or replace) one staged page. Same v1 per-file limits."""
    staging = _require_staging(upload_id)
    if not (0 <= index <= 19):
        raise HTTPException(400, detail="index must be between 0 and 19")
    if file.content_type not in ("image/jpeg", "image/png"):
        raise HTTPException(400, detail="Only JPEG/PNG accepted")
    content = await file.read()
    if len(content) > 10 * 1024 * 1024:
        raise HTTPException(400, detail="Maximum 10MB per image")
    ext = ".jpg" if file.content_type == "image/jpeg" else ".png"
    for old in staging.glob(f"page_{index:02d}.*"):
        old.unlink()
    async with aiofiles.open(staging / f"page_{index:02d}{ext}", "wb") as f:
        await f.write(content)
    pages_received = len(list(staging.glob("page_*")))
    return {"status": "stored", "index": index, "pages_received": pages_received}


@app.post("/api/prepare/finish")
async def prepare_finish(upload_id: str = Form(...)):
    """Run the shared prepare pipeline over the staged pages, in index order.

    On "illegible" the staging is KEPT so the device replaces only the bad
    pages and finishes again; on "ready" the staging is cleared.
    """
    staging = _require_staging(upload_id)
    staged = sorted(staging.glob("page_*"))
    if not staged:
        raise HTTPException(400, detail="No pages staged")
    indices = [int(p.stem.split("_")[1]) for p in staged]
    if indices != list(range(len(staged))):
        missing = sorted(set(range(max(indices) + 1)) - set(indices))
        raise HTTPException(400, detail=f"Missing page indices: {missing}")
    images: list[bytes] = []
    content_types: list[str] = []
    for p in staged:
        async with aiofiles.open(p, "rb") as f:
            images.append(await f.read())
        content_types.append("image/jpeg" if p.suffix == ".jpg" else "image/png")
    result = await _run_prepare(images, content_types)
    if result["status"] == "ready":
        shutil.rmtree(DATA_DIR / "prepare_staging", ignore_errors=True)
    return result
```

- [ ] **Step 5: Rodar golden + arquivo + `test_prepare.py` + suíte inteira**

Run: `python -m pytest tests/test_prepare_staged.py tests/test_prepare.py -v && python -m pytest tests/ -q`
Expected: tudo PASS — a refatoração não pode alterar nenhum comportamento v1
(`test_prepare.py` é a suíte de regressão; o golden é a paridade explícita).

- [ ] **Step 6: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add backend/main.py backend/tests/test_prepare_staged.py
git commit -m "Preparação página a página + publicação da lição sob lock com supersessão"
```

## Task 10: Documentos e configuração no `licao_casa`

**Files:**
- Create: `CONTRATO-DISPOSITIVO.md` (raiz do `licao_casa`)
- Modify: `.env.example` (apenas ADICIONAR)
- Modify: `MIGRACAO-ESP32-P4.md` (apenas ADICIONAR seção ao final)

- [ ] **Step 1: Criar o ponteiro do contrato**

Criar `/Users/institutorecriare/VSCodeProjects/licao_casa/CONTRATO-DISPOSITIVO.md`:

```markdown
# Contrato de Dispositivo v1.1 (ponteiro)

O documento canônico vive no repositório do firmware:

`/Users/institutorecriare/VSCodeProjects/xiaozhi-esp32/docs/professor-virtual/contrato-dispositivo.md`

Resumo do que o backend implementa além da API v1 (tudo aditivo; frontend web
intacto): campos opcionais `request_id`/`media`/`audio_format`/`image_max_px`
no `POST /api/turn` (campos v1.1 na resposta somente no perfil v1.1);
idempotência fail-safe por `request_id`; `GET /api/media/{filename}`;
preparação página a página (`/api/prepare/start|page|finish`); token de
dispositivo obrigatório para clientes não-loopback (fail-closed,
`DEVICE_API_TOKEN`). Não edite o contrato aqui — edite o canônico.
```

- [ ] **Step 2: Adicionar variável ao `.env.example`**

Adicionar ao final:

```
# Token exigido de clientes fora do loopback (dispositivo ESP32).
# Fail-closed (D2): sem este token configurado, conexões remotas recebem 503;
# apenas loopback funciona. Configure antes de conectar o dispositivo.
DEVICE_API_TOKEN=

# Fish Audio TTS da v1.1. Nunca commite a chave real.
FISH_API_KEY=
FISH_VOICE_ID=c5a6cb585b094dedb241365e7e271973
FISH_TTS_MODEL=s2.1-pro-free
FISH_TTS_API_URL=https://api.fish.audio/v1/tts
FISH_TTS_TIMEOUT_SECONDS=120
FISH_TTS_LATENCY=normal
FISH_TTS_SPEED=1.0
```

Acrescentar comentário: `s2.1-pro-free` é configuração inicial de
desenvolvimento, sem SLA e sujeita a mudança; modelo/endpoint podem ser
trocados por ambiente sem alterar o contrato. As variáveis AWS permanecem
documentadas enquanto `polly.py` e seus testes legados existirem, mas o runtime
v1.1 não as usa para TTS.

- [ ] **Step 3: Nota na doc de migração**

Adicionar ao final de `MIGRACAO-ESP32-P4.md`:

```markdown
## Adendo (jul/2026): contrato de dispositivo v1.1 implementado

As adaptações discutidas neste documento foram consolidadas no perfil v1.1 do
backend (ver `CONTRATO-DISPOSITIVO.md` na raiz): mídia por URL em vez de
base64, WAV/PCM 16 kHz opcional no lugar do MP3, imagem já redimensionada para
a tela, idempotência fail-safe por `request_id`, preparação página a página e
token de dispositivo fail-closed. Fish Audio gera MP3 para o web e WAV para o
dispositivo; o firmware permanece neutro ao provedor. O contrato e o fluxo
pedagógico v1 seguem inalterados, com mudança intencional apenas da voz/TTS.
```

- [ ] **Step 4: Rodar a suíte completa**

Run: `python -m pytest tests/ -q`
Expected: tudo PASS.

- [ ] **Step 5: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add CONTRATO-DISPOSITIVO.md .env.example MIGRACAO-ESP32-P4.md
git commit -m "Docs: ponteiro do contrato v1.1, DEVICE_API_TOKEN fail-closed, adendo na migração"
```

## Task 11: Validação E2E com Fish MP3/WAV, Gemini WAV e fechamento

(O preflight Fish MP3+WAV e a aprovação humana já ocorreram na Task 3.9.)

Pré-requisito: `.env` local com `FISH_API_KEY` e credenciais Gemini reais.
Segredos não são enviados ao chat, registrados no contrato nem commitados.
Fazer chamadas reais em volume mínimo.

- [ ] **Step 1: Validação end-to-end do perfil de dispositivo**

Com o backend rodando (`uvicorn main:app --reload`) e uma lição preparada:

```bash
# Gravar ~5 s de fala infantil de teste em WAV 16 kHz mono (macOS):
#   afconvert entrada.m4a amostra.wav -d LEI16@16000 -c 1
curl -s -X POST http://127.0.0.1:8000/api/turn \
  -F session_id=validacao-v11 \
  -F 'audio=@amostra.wav;type=audio/wav' \
  -F request_id=req-validacao-$(uuidgen) \
  -F media=url -F audio_format=wav -F image_max_px=1280 | python3 -m json.tool
```

Expected: 200 com `veredicto` coerente; `audio_url`/`image_url` preenchidos;
baixar ambos (`curl -O http://127.0.0.1:8000<url>`) e conferir: Fish WAV com a
voz Itachi, RIFF/WAVE PCM s16le mono/16 kHz e audível; JPEG ≤1280 px. A
entrada WAV também comprova o caminho Gemini. **Pausar e chamar o
proprietário** para a audição e o acompanhamento do turno real.

- [ ] **Step 2: Registrar resultados no contrato canônico**

Em `/Users/institutorecriare/VSCodeProjects/xiaozhi-esp32/docs/professor-virtual/contrato-dispositivo.md`,
marcar os checkboxes de "Validações empíricas pendentes" com o resultado real
(data, endpoint, modelo Fish, voice id Itachi, formatos, parâmetros, tempos
observados, aprovação humana da voz/qualidade e observações da avaliação do
Gemini). Nunca registrar a chave.

Este step é um checkpoint/handoff humano e deve permanecer
`autonomous: false` no artefato importado pelo GSD. O executor do
`licao_casa` pausa e solicita que a alteração seja feita e commitada
separadamente no `xiaozhi-esp32`; ele não escreve nem commita autonomamente no
outro repositório.

- [ ] **Step 3: Suíte completa + lint dos arquivos tocados**

```bash
python -m pytest tests/ -v
ruff check main.py models.py fish_audio.py polly.py config.py persistence.py media_store.py media_utils.py tests/test_fish_audio.py tests/test_device_auth.py tests/test_turn_device.py tests/test_media_endpoint.py tests/test_media_utils.py tests/test_prepare_staged.py
```
Expected: pytest 100% PASS; ruff sem erros novos nos arquivos listados; os
testes de regressão de `polly.py` continuam verdes, mas nenhuma chamada
runtime da v1.1 usa Polly.

- [ ] **Step 4: Smoke manual do frontend web (regressão v1)**

Subir backend + frontend (`npm run dev` em `frontend/`) e executar um turno de
áudio pelo navegador: resposta v1 com exatamente as chaves atuais, Fish MP3
44,1 kHz/128 kbps com voz Itachi tocando e imagem exibida no mesmo fluxo
acoplado de hoje. **Pausar e chamar o proprietário** para ouvir e aprovar o
MP3.

- [ ] **Step 5: Commits finais**

```bash
cd /Users/institutorecriare/VSCodeProjects/xiaozhi-esp32
git add docs/professor-virtual/contrato-dispositivo.md
git commit -m "Contrato v1.1: registra validação Fish e Gemini WAV"
```

Este commit pertence ao handoff do Step 2 e ocorre no branch correto do
`xiaozhi-esp32`, fora do worktree/branch gerenciado pelo GSD no `licao_casa`.

---

# Revisão independente (obrigatória, após a Task 11)

Solicitar revisão independente do diff completo via `mcp__codex-council__codex`
(thread NOVA, distinta de qualquer thread de decisão), passando o diff do
`licao_casa` e o contrato. Corrigir findings relevantes e repetir a revisão no
máximo duas vezes.

---

# Parte 4 — Repositório `xiaozhi-esp32`: Task 12 (pós-validação)

### Task 12: Atualizar `plano-firmware.md` ao contrato v1.1

Executada NESTE repositório, após os resultados da Task 11 e ANTES de qualquer
fase do firmware que toque rede/mídia (F1, F3, F4, F7):

- [ ] 1. **Q3(a) (voz do tutor)**: marcar a decisão "decodificação MP3" do
  áudio do tutor como **superseded** pelo WAV/PCM validado do contrato v1.1.
  A Task 12 só é alcançada depois que um caminho WAV foi validado; não manter
  fallback MP3 acionável pelo preflight. Registrar Fish Audio/Itachi apenas
  como implementação empírica do backend; o requisito do firmware continua
  neutro ao provedor.
- [ ] 2. **F3**: substituir "base64→PNG" / "base64→MP3" por download via
  `audio_url`/`image_url` (+ `request_id`, `image_max_px=1280`, `audio_format`
  conforme validação); manter o fluxo 502/409/re-hidratação.
- [ ] 3. **Regra de retry completa**: sem `request_id`, retry automático de
  `POST /api/turn` é proibido. Com cliente único/serial, cache íntegro e o
  MESMO `request_id`, uma retransmissão pode processar após falha anterior ao
  marcador `processing`, devolver replay 200 quando há resposta `done` válida
  e replayável, ou 409 fail-safe quando o resultado é indeterminado,
  supersedido ou não replayável. Status HTTP isolado, inclusive 502, não
  autoriza retry automático; a garantia é não haver dupla aplicação
  silenciosa dentro dessas premissas. Após 409, descartar ids pendentes,
  re-hidratar `GET /api/state` + `GET /api/lesson` e iniciar turno lógico novo
  com UUID novo.
- [ ] 4. **F7**: substituir envio em lote único por
  `/api/prepare/start|page|finish` (estratégia de RAM: uma página por vez).
- [ ] 5. **Processo transversal**: substituir "O backend jamais é alterado"
  pela regra de duas zonas + referência ao contrato canônico.
- [ ] 6. **F1**: adicionar o token (`DEVICE_API_TOKEN`) ao provisionamento
  no namespace NVS **`"pv"`** (o módulo continua se chamando `pv_settings`) e
  o tratamento de `401`/`503` do middleware.
- [ ] 7. **Token em todas as chamadas**: enviar a credencial em toda chamada
  HTTP do firmware, incluindo `/api/prepare/*`, `/api/state`, `/api/lesson`,
  `/api/turn`, `/api/media/...` e `/api/health`.
- [ ] 8. **Varredura de referências antigas**: procurar por `retry`, `MP3`,
  `base64` e `lote` em todo o `plano-firmware.md`, cobrindo árvore de
  arquitetura (`pv_backend_client ... SEM retry`), Q3(a), decisões
  complementares, F3, F7, tabela de riscos e Processo transversal. Classificar
  cada ocorrência antes de editar: referências ao MP3 do áudio do tutor podem
  ser obsoletas, mas referências válidas a MP3/OGG de sons locais não são
  substituídas cegamente.
- [ ] 9. Registrar a validação empírica Fish MP3/WAV (modelo, voice id,
  formatos, tempos e aprovação humana), sem transformar o provedor em
  requisito do firmware. Registrar somente fallbacks que sobreviveram à
  validação e são compatíveis com o contrato aprovado.
- [ ] 10. Commit:

```bash
cd /Users/institutorecriare/VSCodeProjects/xiaozhi-esp32
git add docs/professor-virtual/plano-firmware.md
git commit -m "plano-firmware: consome o contrato v1.1 (URLs, WAV, request_id, prepare paginado, token)"
```

---

# Fora do escopo deste plano

- Qualquer mudança no firmware além da Task 12 (as fases F0–F9 consomem o
  contrato depois).
- Qualquer mudança no frontend web do `licao_casa`.
- Streaming do Gemini, TTS por WebSocket, desacoplamento de áudio/imagem,
  cache de áudio, tags emocionais automáticas e remoção de Polly/boto3 legado
  ficam deliberadamente ADIADOS para milestone posterior. A troca do provedor
  ativo para Fish Audio por HTTP com resposta completa está dentro deste
  plano. TLS, multiusuário e quotas de uso: adiados, a decidir caso a caso em
  milestone próprio.
