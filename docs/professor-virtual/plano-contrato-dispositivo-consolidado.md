# Plano Consolidado — Contrato de Dispositivo v1.1

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Data da consolidação:** 30/07/2026
**Este documento é a consolidação mecânica de:** `plano-contrato-dispositivo.md`
+ `emenda-01-plano-contrato-dispositivo.md` + `emenda-02-plano-contrato-dispositivo.md`
(versão corrigida pelo auditor, commit `64f0ba3`). **Para execução, este
documento prevalece sobre os três**; eles permanecem como histórico. Nenhum
conteúdo novo foi projetado aqui — apenas fusão.

**Goal:** Adicionar ao backend `licao_casa` um perfil de contrato para o
dispositivo embarcado (mídia por URL, áudio WAV/PCM, imagem redimensionada,
idempotência fail-safe por `request_id`, preparação página a página e token de
autenticação fail-closed), preservando **literalmente** o comportamento v1 do
frontend web — e alinhar contrato, governança e plano do firmware nos
documentos do repositório `xiaozhi-esp32`.

**Architecture:** Mudanças aditivas na borda de transporte (campos opcionais,
endpoints novos, campos novos de resposta que só aparecem no perfil v1.1). O
miolo pedagógico (`gemini.py`, `session_engine.py`, transições de veredito) não
é tocado. Requisições sem campos v1.1 recebem exatamente o payload v1 atual.

**Tech Stack:** FastAPI + pydantic v2 + aiofiles, boto3/Polly, Pillow (novo),
pytest + pytest-asyncio + httpx ASGITransport.

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

## Estado atual

| Item | Estado |
|---|---|
| Contrato canônico (`contrato-dispositivo.md`) criado | FEITO (commit `caa3ba8`) — emendas da Parte 1 abaixo PENDENTES |
| CLAUDE.md (zonas + retry) | FEITO (commit `3873025`) — ajuste de redação na Parte 1 PENDENTE |
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

**Ordem de execução:** Parte 1 → Parte 2 → conversão mecânica das tasks da
Parte 3 em `PLAN.md` do GSD (sem redesenho) → Tasks 3 → 3.9 → 4 → 5 → 6 → 7 →
8 → 9 → 10 → 11 → revisão independente final → Parte 4 → só então fases F1+ do
firmware.

## Global Constraints

- **v1 literal (D1):** requisições sem campos v1.1 retornam exatamente as
  chaves v1 de hoje, com semântica e valores inalterados. A suíte existente
  (152 testes) não pode ser editada nem removida — apenas ADICIONAR testes.
  `python -m pytest tests/ -q` verde ao fim de CADA task.
- **Miolo pedagógico intocável:** zero edições em `gemini.py`,
  `session_engine.py`, `celebration.py`, `image_gen.py` e nas transições de
  veredito do `/api/turn` (passos 1–9 e 11–13b, exceto os pontos de
  supersessão/marcador explicitamente definidos na Task 8).
- Todos os campos novos são opcionais; ausentes ⇒ caminho v1 exato.
- Token e PIN jamais em corpo de resposta ou log.
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
  retry de `POST /api/turn` proibido sem `request_id`; com `request_id`
  (contrato v1.1), a retransmissão do mesmo turno devolve replay 200 ou 409
  fail-safe — nunca aplica o turno duas vezes;
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
  turno). Com `request_id` (v1.1): a retransmissão devolve replay 200 quando a
  última resposta está disponível, ou 409 quando o resultado é
  indeterminado/supersedido; após 409, descartar ids pendentes, re-hidratar
  `GET /api/state` + `GET /api/lesson` e iniciar turno novo com UUID novo.
```

- [ ] **Step 2: `.claude/autonomy/decision-policy.md`** — na seção "Missão
Professor Virtual — regras duras", substituir os bullets "Preserve o contrato
HTTP…", "Nunca aprove retry…" e "Mudança no backend…" por:

```markdown
- Preserve o contrato HTTP: seção 7 da especificação (v1) + perfil v1.1 de
  `docs/professor-virtual/contrato-dispositivo.md`. Não invente endpoints,
  campos, estados ou capacidades fora deles.
- Retry de `POST /api/turn`: nunca aprove retry sem `request_id`. Com
  `request_id` (v1.1), a retransmissão do mesmo turno é segura: replay 200 ou
  409 fail-safe. Após 409: re-hidratar e novo `request_id`.
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
  (contrato v1.1), a retransmissão do MESMO turno é segura — devolve replay
  200 ou 409 fail-safe, nunca aplica o turno duas vezes. Após 409:
  re-hidratar e usar `request_id` novo.
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
> uma retransmissão do mesmo turno não é aplicada silenciosamente duas vezes:
> ela devolve o replay 200 quando a última resposta ainda está disponível, ou
> 409 quando o resultado é indeterminado/supersedido. Após 409, o dispositivo
> descarta todos os ids pendentes anteriores, re-hidrata `GET /api/state` +
> `GET /api/lesson` e só então inicia um turno lógico novo com UUID novo.

- [ ] **Step 2: Resposta v1 literal (D1)** — substituir "Clientes v1 ignoram
os campos extras" e adjacências por:

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
inteira pelo texto normativo completo do E02.3(c) da Emenda 02 (o bloco
"### Idempotência (`request_id`)" com: schema
`seen_request_ids`/`rehydration_required`/`last`; ledger sem truncamento;
regras de replay/409; marcador `processing`→`done` e o que ele NÃO cobre;
supersessão antes de toda mutação externa — turno v1, nova lição,
`adult/resolve`, expiração, 502 pré-marcador; quarentena copy-before-replace;
limite sob corrupção integral; semântica do 409). Copiar verbatim de
`emenda-02-plano-contrato-dispositivo.md`, seção E02.3(c).

- [ ] **Step 5: Ciclo de vida da mídia** — substituir "Arquivos de turnos
anteriores são apagados quando um novo turno gera mídia." por:

> A mídia nova é escrita SEM remover a anterior; a remoção da mídia não
> referenciada acontece apenas DEPOIS da consolidação da nova resposta
> idempotente (ou da supersessão), dentro da mesma fronteira protegida do
> turno, em limpeza best-effort (órfãos tolerados; mídia ainda referenciada
> por resposta replayável nunca é apagada).

- [ ] **Step 6: Autenticação (D2)** — substituir as regras da seção por:

> Não-loopback com `DEVICE_API_TOKEN` configurado e credencial
> ausente/incorreta ⇒ `401`. Não-loopback com token NÃO configurado ⇒
> `503 {"detail": "DEVICE_API_TOKEN não configurado no servidor"}`
> (fail-closed; falha de configuração do servidor). Loopback sempre isento;
> origem ausente/desconhecida NÃO é loopback. Não existe modo remoto sem
> token.

- [ ] **Step 7: Neutralidade de TTS** — na tabela de `audio_format` e na seção
"Formato WAV", remover as menções a "Polly" da interface normativa (descrever
apenas: WAV = PCM s16le mono 16 kHz com header de 44 bytes; MP3 24 kHz mono).
Polly permanece citada SOMENTE na seção "Validações empíricas pendentes".

- [ ] **Step 8: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/xiaozhi-esp32
git add docs/professor-virtual/contrato-dispositivo.md
git commit -m "Contrato v1.1: idempotência fail-safe completa, v1 literal, token fail-closed, TTS neutro"
```

---

# Parte 2 — Pré-condições operacionais GSD/Git no `licao_casa`

Executar ANTES de abrir o milestone (sessão local do `licao_casa`, com o
proprietário):

1. Reconciliar `STATE.md` × `ROADMAP.md` × summaries/UATs da V1 (fase 5 consta
   "executing" num e "completa" no outro).
2. Auditar/arquivar o milestone V1 no GSD.
3. Baseline: `5588b3e` confirmada (D3) — a tag só é criada após a
   reconciliação; arquivos não rastreados ficam fora.
4. Criar branch dedicada (ex.: `milestone/device-contract-v1.1`) — a config
   atual (`branching_strategy: "none"`) commitaria em `main`.
5. Verificar/instalar as definições de agentes GSD (`agents_installed=false`
   reportado) ou escolher modo de execução suportado.
6. Converter MECANICAMENTE as Tasks 3–11 desta Parte 3 em artefatos `PLAN.md`
   válidos do GSD — sem redesenho por planner/roadmapper; o conteúdo técnico é
   o deste documento, verbatim.
7. Preservar todos os arquivos não rastreados existentes (auditoria, resposta,
   notas).

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

Em `backend/config.py`, dentro de `Settings`, após `adult_pin: str = ""`:

```python
    device_api_token: str = ""
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
git commit -m "Token de dispositivo fail-closed: 401 credencial, 503 sem token, loopback isento"
```

## Task 3.9 (preflight): Validação real do Polly PCM — ANTES da Task 4

**Files:**
- Create: `backend/scripts/check_polly_pcm.py`

**Gate:** requer autorização do proprietário para 1 chamada real de custo
mínimo. O resultado é registrado no contrato canônico ANTES da Task 4.

- [ ] **Step 1: Criar o script**

Criar `backend/scripts/check_polly_pcm.py`:

```python
"""Manual check: Polly generative voice + pcm 16k (contrato v1.1, preflight).

Run from backend/ with the venv active:  python scripts/check_polly_pcm.py
Writes polly_pcm_check.wav next to this script for a listening check.
"""

import asyncio
import io
import sys
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))

from polly import synthesize_speech  # noqa: E402


async def main() -> None:
    data = await synthesize_speech(
        "Oi! Vamos fazer a lição de casa juntos?", output_format="wav"
    )
    with wave.open(io.BytesIO(data)) as w:
        print(
            f"channels={w.getnchannels()} rate={w.getframerate()} "
            f"bits={w.getsampwidth() * 8} frames={w.getnframes()}"
        )
    out = Path(__file__).parent / "polly_pcm_check.wav"
    out.write_bytes(data)
    print(f"OK — ouça {out}")


asyncio.run(main())
```

Nota: o script depende da Task 4 (`output_format`). Ordem prática: criar o
script agora; implementar a Task 4 no branch; rodar o preflight ANTES do
commit da Task 4 e registrar o resultado no contrato. Se o preflight reprovar,
aplicar a contingência ANTES de commitar a Task 4.

- [ ] **Step 2: Rodar (com autorização do proprietário e `.env` real)**

Run: `python scripts/check_polly_pcm.py`
Expected: `channels=1 rate=16000 bits=16 ...` e WAV audível.
**Contingência se o engine `generative` rejeitar `pcm`:** repetir com
`engine="neural"`; se funcionar, registrar no contrato que WAV usa engine
`neural`; se nada de PCM funcionar, registrar que o dispositivo usa MP3
(decoder `esp_audio_codec` no firmware) e remover a opção `wav` do contrato —
decisão documentada, não silenciosa.

- [ ] **Step 3: Registrar o resultado no contrato canônico e commitar**

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add backend/scripts/check_polly_pcm.py
git commit -m "Preflight: script de validação do Polly PCM 16 kHz"
cd /Users/institutorecriare/VSCodeProjects/xiaozhi-esp32
git add docs/professor-virtual/contrato-dispositivo.md
git commit -m "Contrato v1.1: resultado do preflight Polly PCM"
```

## Task 4: Polly em WAV (PCM 16 kHz + header WAV)

Condicionada ao resultado da Task 3.9 (aplicar a variação registrada no
contrato, se houver).

**Files:**
- Modify: `backend/polly.py`
- Test: `backend/tests/test_polly.py` (APENAS adicionar testes)

**Interfaces:**
- Produces: `synthesize_speech(text, voice_id=None, engine=None, output_format="mp3") -> bytes`;
  `output_format="wav"` ⇒ WAV (RIFF 44 bytes + PCM s16le mono 16 kHz); default
  ⇒ MP3 24 kHz (v1 intacto). Helper `_wav_from_pcm(pcm, sample_rate=16000) -> bytes`.

- [ ] **Step 1: Escrever os testes que falham**

Adicionar ao FINAL de `backend/tests/test_polly.py`:

```python
import struct

from polly import _wav_from_pcm


class TestPollyWav:
    """WAV output for the device profile (contrato v1.1)."""

    def test_wav_header_layout(self):
        pcm = b"\x01\x02\x03\x04"
        wav = _wav_from_pcm(pcm, sample_rate=16000)
        assert wav[0:4] == b"RIFF"
        assert wav[8:12] == b"WAVE"
        assert struct.unpack("<H", wav[20:22])[0] == 1       # PCM
        assert struct.unpack("<H", wav[22:24])[0] == 1       # mono
        assert struct.unpack("<I", wav[24:28])[0] == 16000   # sample rate
        assert struct.unpack("<H", wav[34:36])[0] == 16      # bits
        assert struct.unpack("<I", wav[40:44])[0] == len(pcm)
        assert wav[44:] == pcm

    @pytest.mark.asyncio
    async def test_synthesize_wav_calls_polly_pcm_16k(self, mock_polly):
        result = await synthesize_speech("Ola", output_format="wav")
        kwargs = mock_polly.synthesize_speech.call_args.kwargs
        assert kwargs["OutputFormat"] == "pcm"
        assert kwargs["SampleRate"] == "16000"
        assert result[0:4] == b"RIFF"
        assert result[44:] == b"fake-mp3-data"  # mock stream passes through as pcm

    @pytest.mark.asyncio
    async def test_synthesize_default_still_mp3(self, mock_polly):
        await synthesize_speech("Ola")
        kwargs = mock_polly.synthesize_speech.call_args.kwargs
        assert kwargs["OutputFormat"] == "mp3"
        assert kwargs["SampleRate"] == "24000"
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `python -m pytest tests/test_polly.py -v`
Expected: FAIL — `ImportError: cannot import name '_wav_from_pcm'`.

- [ ] **Step 3: Implementar**

Em `backend/polly.py`, adicionar `import struct` ao topo e substituir
`_synthesize` e `synthesize_speech` por:

```python
def _wav_from_pcm(pcm: bytes, sample_rate: int = 16000) -> bytes:
    """Wrap raw PCM s16le mono in a canonical 44-byte WAV header."""
    channels, bits = 1, 16
    byte_rate = sample_rate * channels * bits // 8
    block_align = channels * bits // 8
    header = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE"
    header += b"fmt " + struct.pack(
        "<IHHIIHH", 16, 1, channels, sample_rate, byte_rate, block_align, bits
    )
    header += b"data" + struct.pack("<I", len(pcm))
    return header + pcm


def _synthesize(text: str, voice_id: str, engine: str, output_format: str) -> bytes:
    """Synchronous Polly synthesis (runs in executor thread).

    output_format "mp3" keeps the v1 behavior (MP3 24 kHz). "wav" asks Polly
    for raw PCM s16le mono 16 kHz and wraps it in a WAV header for the device.
    """
    client = boto3.client(
        "polly",
        aws_access_key_id=settings.aws_access_key_id,
        aws_secret_access_key=settings.aws_secret_access_key,
        region_name=settings.aws_region,
    )
    if output_format == "wav":
        response = client.synthesize_speech(
            Engine=engine,
            LanguageCode="pt-BR",
            OutputFormat="pcm",
            SampleRate="16000",
            Text=text,
            TextType="text",
            VoiceId=voice_id,
        )
        return _wav_from_pcm(response["AudioStream"].read())
    response = client.synthesize_speech(
        Engine=engine,
        LanguageCode="pt-BR",
        OutputFormat="mp3",
        SampleRate="24000",
        Text=text,
        TextType="text",
        VoiceId=voice_id,
    )
    return response["AudioStream"].read()


async def synthesize_speech(
    text: str,
    voice_id: str | None = None,
    engine: str | None = None,
    output_format: str = "mp3",
) -> bytes:
    """Synthesize speech via AWS Polly. output_format: "mp3" (v1) or "wav"."""
    voice_id = voice_id or settings.polly_voice_id
    engine = engine or settings.polly_engine

    loop = asyncio.get_event_loop()
    return await loop.run_in_executor(
        None, partial(_synthesize, text, voice_id, engine, output_format)
    )
```

- [ ] **Step 4: Rodar testes + suíte, e o preflight da Task 3.9**

Run: `python -m pytest tests/test_polly.py -v && python -m pytest tests/ -q`
Depois: `python scripts/check_polly_pcm.py` (com autorização; registrar
resultado no contrato antes de commitar).

- [ ] **Step 5: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add backend/polly.py backend/tests/test_polly.py
git commit -m "Polly: saída WAV opcional (PCM 16 kHz mono + header) para o dispositivo"
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

Run: `python -m pytest tests/test_media_utils.py -v`
Expected: PASS.

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


class TestTurnDeviceProfile:
    @pytest.mark.asyncio
    async def test_media_url_returns_urls_and_empty_base64(
        self, client, setup_session, mock_polly
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
        self, client, setup_session, mock_polly, mock_image_gen
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
        self, client, setup_session, mock_polly, mock_image_gen
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
        self, client, setup_session, mock_polly, mock_image_gen
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
        self, client, setup_session, mock_polly, mock_image_gen
    ):
        data = {"session_id": "s1", "request_id": "req-abc-1"}
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            first = await client.post("/api/turn", data=data, files=_turn_files())
        assert first.status_code == 200
        assert mock_polly.synthesize_speech.call_count == 1
        gen_calls = mock_image_gen.aio.models.generate_content.call_count

        must_not_run = AsyncMock(side_effect=AssertionError("evaluate_turn on replay"))
        with patch("main.evaluate_turn", new=must_not_run):
            second = await client.post("/api/turn", data=data, files=_turn_files())
        assert second.status_code == 200
        assert second.json() == first.json()
        # Exactly-once: no second Polly/image call, no counter double-apply.
        assert mock_polly.synthesize_speech.call_count == 1
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
        self, client, setup_session, mock_polly, mock_image_gen
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
        self, client, setup_session, mock_polly, mock_image_gen
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
        self, client, setup_session, mock_polly, mock_image_gen
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
        self, client, setup_session, mock_polly, mock_image_gen
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
        self, client, setup_session, mock_polly, mock_image_gen
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
        self, client, setup_session, mock_polly, mock_image_gen
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
        self, client, setup_session, mock_polly, mock_image_gen, failure_stage
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
        self, client, setup_session, mock_polly, mock_image_gen
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
        self, client, setup_session, mock_polly, mock_image_gen
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
        self, client, setup_session, mock_polly, mock_image_gen, monkeypatch
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
        self, client, setup_session, mock_polly, mock_image_gen, monkeypatch
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
        self, client, setup_session, mock_polly, mock_image_gen
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
        self, client, setup_session, mock_polly, mock_image_gen
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
        self, client, setup_session, mock_polly, mock_image_gen
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
        self, client, setup_session, mock_polly, mock_image_gen
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
        self, client, setup_session, mock_polly, mock_image_gen
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
        self, client, setup_session, mock_polly, mock_image_gen
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
        self, client, setup_session, mock_polly, mock_image_gen
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
        self, client, setup_session, mock_polly, mock_image_gen
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
```

- [ ] **Step 3: Nota na doc de migração**

Adicionar ao final de `MIGRACAO-ESP32-P4.md`:

```markdown
## Adendo (jul/2026): contrato de dispositivo v1.1 implementado

As adaptações discutidas neste documento foram consolidadas no perfil v1.1 do
backend (ver `CONTRATO-DISPOSITIVO.md` na raiz): mídia por URL em vez de
base64, WAV/PCM 16 kHz opcional no lugar do MP3, imagem já redimensionada para
a tela, idempotência fail-safe por `request_id`, preparação página a página e
token de dispositivo fail-closed. O firmware consome esse perfil; o fluxo v1
do frontend web segue inalterado.
```

- [ ] **Step 4: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add CONTRATO-DISPOSITIVO.md .env.example MIGRACAO-ESP32-P4.md
git commit -m "Docs: ponteiro do contrato v1.1, DEVICE_API_TOKEN fail-closed, adendo na migração"
```

## Task 11: Validação E2E com serviços reais (Gemini WAV) e fechamento

(O preflight do Polly já ocorreu na Task 3.9.)

Pré-requisito: `.env` com credenciais reais. Chamadas pagas em volume mínimo.

- [ ] **Step 1: Validação end-to-end com WAV no turno (Gemini)**

Com o backend rodando (`uvicorn main:app --reload`) e uma lição preparada:

```bash
# Gravar ~5 s de fala infantil de teste em WAV 16 kHz mono (macOS):
#   afconvert entrada.m4a amostra.wav -d LEI16@16000 -c 1
curl -s -X POST http://127.0.0.1:8000/api/turn \
  -F session_id=validacao-v11 \
  -F 'audio=@amostra.wav;type=audio/wav' \
  -F request_id=validacao-0001 \
  -F media=url -F audio_format=wav -F image_max_px=1280 | python3 -m json.tool
```

Expected: 200 com `veredicto` coerente; `audio_url`/`image_url` preenchidos;
baixar ambos (`curl -O http://127.0.0.1:8000<url>`) e conferir (WAV audível;
JPEG ≤1280 px). **Pausar e chamar o proprietário** para a audição e o
acompanhamento do turno real.

- [ ] **Step 2: Registrar resultados no contrato canônico**

Em `/Users/institutorecriare/VSCodeProjects/xiaozhi-esp32/docs/professor-virtual/contrato-dispositivo.md`,
marcar os checkboxes de "Validações empíricas pendentes" com o resultado real
(data, engine usado, observações de qualidade da avaliação do Gemini).

- [ ] **Step 3: Suíte completa + lint dos arquivos tocados**

```bash
python -m pytest tests/ -v
ruff check main.py models.py polly.py config.py persistence.py media_store.py media_utils.py tests/test_device_auth.py tests/test_turn_device.py tests/test_media_endpoint.py tests/test_media_utils.py tests/test_prepare_staged.py
```
Expected: pytest 100% PASS; ruff sem erros novos nos arquivos listados.

- [ ] **Step 4: Smoke manual do frontend web (regressão v1)**

Subir backend + frontend (`npm run dev` em `frontend/`) e executar um turno de
áudio pelo navegador: resposta com áudio MP3 tocando e imagem exibida, como
hoje.

- [ ] **Step 5: Commits finais**

```bash
cd /Users/institutorecriare/VSCodeProjects/xiaozhi-esp32
git add docs/professor-virtual/contrato-dispositivo.md
git commit -m "Contrato v1.1: registra resultado da validação Gemini WAV"
```

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

- [ ] 1. **Q3(a) (voz do tutor)**: marcar a decisão "decodificação MP3" como
  **superseded** pelo WAV/PCM do contrato v1.1 (mantendo MP3 documentado como
  contingência se a Task 3.9/11 a tiver acionado).
- [ ] 2. **F3**: substituir "base64→PNG" / "base64→MP3" por download via
  `audio_url`/`image_url` (+ `request_id`, `image_max_px=1280`, `audio_format`
  conforme validação); manter o fluxo 502/409/re-hidratação.
- [ ] 3. **Regra de retry**: substituir "sem retry" por "retransmissão apenas
  com o mesmo `request_id` (replay 200 ou 409 fail-safe); após 409,
  re-hidratar + `request_id` novo".
- [ ] 4. **F7**: substituir envio em lote único por
  `/api/prepare/start|page|finish` (estratégia de RAM: uma página por vez).
- [ ] 5. **Processo transversal**: substituir "O backend jamais é alterado"
  pela regra de duas zonas + referência ao contrato canônico.
- [ ] 6. **F1**: adicionar o token (`DEVICE_API_TOKEN`) ao provisionamento
  (NVS `pv_settings`) e o tratamento de `401`/`503` do middleware.
- [ ] 7. Registrar fallbacks sobreviventes da validação empírica.
- [ ] 8. Commit:

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
- Streaming do Gemini, TTS por WebSocket e troca do provedor de TTS ficam
  deliberadamente ADIADOS para milestone posterior; este plano não toma
  decisão definitiva sobre eles. TLS, multiusuário e quotas de uso: adiados, a
  decidir caso a caso em milestone próprio.
