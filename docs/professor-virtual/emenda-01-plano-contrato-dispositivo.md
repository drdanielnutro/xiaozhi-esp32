# Emenda 01 ao plano "Contrato de dispositivo v1.1"

**Data:** 30/07/2026
**Status:** aguardando revisão final do auditor — NENHUMA implementação iniciada
**Plano emendado:** `docs/professor-virtual/plano-contrato-dispositivo.md`
**Origem:** auditoria (`licao_casa/AUDITORIA-PLANO-CONTRATO-DISPOSITIVO-V1.1.md`),
resposta do autor (`licao_casa/RESPOSTA-AUDITORIA-...md`) e réplica do auditor
com três refinamentos e as decisões D1–D3.

**Decisões incorporadas (proprietário, via Codex Decision Proxy):**
- **D1 = opção 1:** requisição v1 recebe **literalmente** o payload v1 (mesmo
  conjunto de chaves de hoje). Campos v1.1 aparecem SOMENTE quando algum campo
  do perfil v1.1 é enviado na requisição.
- **D2 = fail-closed com erros distintos:** token configurado + credencial
  ausente/errada ⇒ `401`; servidor SEM `DEVICE_API_TOKEN` recebendo conexão
  não-loopback ⇒ `503` (falha de configuração do servidor).
- **D3:** `5588b3e` confirmado como baseline do **código** V1, condicionado à
  reconciliação e arquivamento corretos do GSD antes de tag/branch; arquivos
  não rastreados não entram na tag.

**Separação de repositórios (exigência do auditor):** as Partes 1 e 4 desta
emenda pertencem ao repositório `xiaozhi-esp32` e NÃO entram nas Tasks 3–11
executadas no `licao_casa`. A Parte 2 altera exclusivamente tasks do
`licao_casa`. A Parte 3 é operacional (GSD), executada no `licao_casa` pelo
proprietário/sessão local antes do milestone.

Convenção: "SUBSTITUI" = o bloco do plano original deixa de valer e é trocado
pelo bloco daqui. "ADICIONA" = conteúdo novo sem remoção.

---

## Parte 1 — Repositório `xiaozhi-esp32` (governança e contrato; ANTES da execução)

### E1.1 — Task 2 ampliada: alinhar `AGENTS.md` e `decision-policy.md` (AUD-04)

ADICIONA à Task 2 do plano (mesmo commit ou commit próprio neste repo):

**(a) `AGENTS.md`** — substituir, na seção "Seu papel neste repositório", o
trecho que declara o backend "intocável" por:

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

— substituir, em "Restrições obrigatórias", `sem retry automático de turno` por:

```markdown
  retry de `POST /api/turn` proibido sem `request_id`; com `request_id`
  (contrato v1.1), reenviar o MESMO `request_id` é seguro (replay);
```

— substituir, em "Fora de escopo", `alterar o backend por conveniência do firmware` por:

```markdown
- **Fora de escopo:** alterar o miolo pedagógico do backend; mudanças de
  transporte fora do contrato v1.1 aprovado; soluções em nuvem; duplicar a
  fonte de verdade no dispositivo; manter qualquer parte da experiência do
  usuário no desktop.
```

— na seção "Preservação do backend", ADICIONAR ao final:

```markdown
Exceção aprovada pelo proprietário (30/07/2026): as mudanças aditivas de
transporte do contrato v1.1 (`docs/professor-virtual/contrato-dispositivo.md`)
estão pré-autorizadas nos termos do plano + emenda 01. Mudanças além desse
escopo continuam sujeitas às 5 condições e ao escalonamento.
```

— na seção "Invariantes do Professor Virtual", substituir o bullet
"**Nunca** faça retry automático de `POST /api/turn` ..." por:

```markdown
- Retry de `POST /api/turn` sem `request_id`: **nunca** (pode virar outro
  turno). Com `request_id` (v1.1): reenvio do MESMO `request_id` é replay
  seguro; resposta `409` de indeterminação exige re-hidratar e usar um
  `request_id` NOVO.
```

**(b) `.claude/autonomy/decision-policy.md`** — na seção "Missão Professor
Virtual — regras duras", substituir os bullets "Preserve o contrato HTTP…",
"Nunca aprove retry…" e "Mudança no backend…" por:

```markdown
- Preserve o contrato HTTP: seção 7 da especificação (v1) + perfil v1.1 de
  `docs/professor-virtual/contrato-dispositivo.md`. Não invente endpoints,
  campos, estados ou capacidades fora deles.
- Retry de `POST /api/turn`: nunca aprove retry sem `request_id`. Replay com
  o MESMO `request_id` (v1.1) é seguro e permitido. Após `409` de
  indeterminação: re-hidratar e novo `request_id`.
- Nunca aprove mover regra pedagógica, contador ou decisão de avanço para o
  dispositivo: o backend é a única fonte de verdade pedagógica.
- Mudança no backend: as mudanças aditivas de transporte do contrato v1.1
  estão pré-aprovadas pelo proprietário (30/07/2026, plano + emenda 01).
  Qualquer mudança FORA desse escopo — em especial no miolo pedagógico —
  mantém as 5 condições do `AGENTS.md` e `escalate: true`.
```

**(c) `decision-log.jsonl`** — registrar uma entrada com a decisão do
proprietário (data, D1–D3, referência ao plano + emenda).

### E1.2 — Contrato canônico: emendas (AUD-01/02/03/10/12, D1, D2)

ADICIONA/SUBSTITUI em `docs/professor-virtual/contrato-dispositivo.md`:

1. **Resposta v1 literal (D1)** — substituir a frase "Clientes v1 ignoram os
   campos extras" e adjacências por:

   > Requisições **sem** nenhum campo v1.1 recebem **exatamente** o payload
   > v1 atual (mesmo conjunto de chaves; nenhuma chave nova). Os campos
   > `audio_url`, `image_url`, `audio_format`, `image_format` e `request_id`
   > aparecem na resposta **somente** quando a requisição envia ao menos um
   > campo do perfil v1.1 (`request_id`, `media`, `audio_format`,
   > `image_max_px`).

2. **Idempotência (AUD-01)** — acrescentar à seção "Idempotência":

   > O backend grava um marcador de intenção (`status: "processing"`) antes
   > de persistir qualquer efeito do turno e o troca por
   > `status: "done"` + resposta ao concluir (escritas atômicas). Replay:
   > `done` ⇒ resposta armazenada; `processing` ⇒ `409` "turno pode já ter
   > sido aplicado" — o dispositivo re-hidrata `GET /api/state` e envia um
   > turno NOVO com `request_id` NOVO (nunca reprocessamento silencioso).
   > Cache ilegível ⇒ `409` idêntico + quarentena do arquivo; o turno novo
   > seguinte processa normalmente.

3. **Ciclo de vida da mídia (AUD-03)** — substituir "Arquivos de turnos
   anteriores são apagados quando um novo turno gera mídia." por:

   > A mídia nova é escrita SEM remover a anterior; a remoção da mídia não
   > referenciada acontece apenas DEPOIS da consolidação da nova resposta
   > idempotente, em limpeza best-effort (órfãos tolerados; mídia ainda
   > referenciada nunca é apagada).

4. **Token (D2)** — substituir as regras da seção "Autenticação" por:

   > Não-loopback com `DEVICE_API_TOKEN` configurado e credencial
   > ausente/incorreta ⇒ `401`. Não-loopback com token NÃO configurado ⇒
   > `503 {"detail": "DEVICE_API_TOKEN não configurado no servidor"}`
   > (fail-closed; falha de configuração do servidor). Loopback sempre
   > isento. Não existe modo remoto sem token.

5. **Neutralidade de TTS (AUD-12)** — na tabela de `audio_format` e na seção
   "Formato WAV", remover as menções a "Polly" da interface normativa
   (descrever apenas: WAV = PCM s16le mono 16 kHz com header de 44 bytes;
   MP3 24 kHz mono). Polly permanece citada SOMENTE na seção "Validações
   empíricas pendentes" como detalhe de implementação.

### E1.3 — Fora de escopo do plano: redação (AUD-11)

SUBSTITUI o item de fora-de-escopo do plano por:

```markdown
- Streaming do Gemini, TTS por WebSocket e troca do provedor de TTS ficam
  deliberadamente ADIADOS para milestone posterior; este plano não toma
  decisão definitiva sobre eles. TLS, multiusuário e quotas de uso: adiados,
  a decidir caso a caso em milestone próprio.
```

---

## Parte 2 — Repositório `licao_casa` (correções às Tasks 3–11)

### E2.1 — Task 3 corrigida: middleware fail-closed com 401/503 (AUD-10, D2)

SUBSTITUI o corpo do middleware (Task 3, Step 3) por:

```python
@app.middleware("http")
async def device_token_guard(request: Request, call_next):
    """Device-token guard (contrato v1.1, fail-closed — D2).

    Loopback is always exempt (web frontend via Vite proxy). For any other
    client: missing/failed credential with a configured token -> 401; server
    without DEVICE_API_TOKEN receiving a remote connection -> 503 (server
    misconfiguration, mirroring the adult_pin 503 convention).
    """
    client_host = request.client.host if request.client else None
    if client_host not in _LOOPBACK_HOSTS:
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

SUBSTITUI, em `tests/test_device_auth.py` (Task 3, Step 1), o teste
`test_unset_token_allows_remote` por:

```python
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
```

### E2.2 — Nova Task 3.9 (preflight): validação Polly PCM ANTES da Task 4 (AUD-08)

ADICIONA task entre a 3 e a 4. Conteúdo: os passos 1–2 da Task 11 original
(criar `backend/scripts/check_polly_pcm.py` — código idêntico ao do plano — e
rodá-lo com credenciais reais), MOVIDOS para cá. Gate: requer autorização do
proprietário para 1 chamada real de custo mínimo; o resultado (engine
aprovado ou contingência: `neural`, ou MP3-only com remoção do `wav` do
contrato) é registrado no contrato canônico ANTES de implementar a Task 4.
A Task 11 mantém apenas a validação E2E Gemini+WAV e o fechamento.

### E2.3 — Task 4: sem mudança de código; dependência nova

O código da Task 4 permanece o do plano, condicionado ao resultado da Task
3.9 (se a contingência mudar engine/formato, aplicar a variação registrada no
contrato antes de implementar).

### E2.4 — Task 6 corrigida: mídia escreve-sem-apagar + limpeza separada (AUD-03)

SUBSTITUI `backend/media_store.py` (Task 6, Step 3) por:

```python
"""Turn media files served by GET /api/media (contrato v1.1).

Writing NEVER deletes previous files: the previous idempotent response may
still reference them. Cleanup is a separate, best-effort step the caller runs
only AFTER the new turn's response is durably stored (AUD-03).
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
    """Best-effort removal of media files not in `keep`.

    Called only after the new idempotent response is durably stored. Orphans
    from failed cleanup are tolerated; referenced media is never deleted here
    because `keep` always contains the just-stored turn's filenames.
    """
    if not media_dir.exists():
        return
    for f in media_dir.iterdir():
        if f.name not in keep:
            try:
                f.unlink()
            except OSError:
                pass
```

SUBSTITUI, em `tests/test_media_endpoint.py` (Task 6, Step 1), o teste
`test_clears_previous_turn_files` por:

```python
    @pytest.mark.asyncio
    async def test_save_does_not_delete_previous_files(self, data_dir):
        media = data_dir / "media"
        media.mkdir(parents=True)
        stale = media / ("turn_" + "0" * 32 + "_audio.mp3")
        stale.write_bytes(b"old")
        await save_turn_media(media, b"AAA", b"III", "mp3", "png")
        assert stale.exists()
        assert len(list(media.iterdir())) == 3

    def test_cleanup_removes_only_unreferenced(self, tmp_path):
        from media_store import cleanup_turn_media
        media = tmp_path / "media"
        media.mkdir(parents=True)
        keep_name = "turn_" + "a" * 32 + "_audio.wav"
        (media / keep_name).write_bytes(b"new")
        (media / ("turn_" + "0" * 32 + "_audio.mp3")).write_bytes(b"old")
        cleanup_turn_media(media, keep={keep_name})
        assert (media / keep_name).exists()
        assert len(list(media.iterdir())) == 1
```

(import no topo do arquivo de teste: `from media_store import save_turn_media,
cleanup_turn_media`.)

### E2.5 — Task 7 corrigida: resposta v1 literal (AUD-02, D1)

O modelo `TutoringResponse` ganha os campos v1.1 como no plano (Step 3
inalterado). MUDA a serialização e os testes:

**(a)** ADICIONA em `backend/main.py`, junto às constantes do módulo:

```python
# Response keys that exist only in the v1.1 device profile (D1: a plain v1
# request must receive the literal v1 payload — exactly today's key set).
_V11_RESPONSE_KEYS = {"audio_url", "image_url", "audio_format", "image_format", "request_id"}
```

**(b)** Na validação dos campos (Task 7, Step 4c), ADICIONA ao final:

```python
    is_device_profile = any(
        v is not None for v in (request_id, media, audio_format, image_max_px)
    )
```

**(c)** SUBSTITUI o retorno final do handler (plano Task 7, Step 4f / Task 8):

```python
    if not is_device_profile:
        # D1: literal v1 payload — same key set as before v1.1 existed.
        return JSONResponse(content=response.model_dump(exclude=_V11_RESPONSE_KEYS))
    return response
```

(Starlette serializa `JSONResponse` com `ensure_ascii=False`, compacto — o
mesmo render do caminho padrão do FastAPI; o payload v1 permanece literal.)

**(d)** SUBSTITUI, em `tests/test_turn_device.py`, o teste
`test_v1_request_unchanged` por um golden de conjunto de chaves:

```python
V1_KEYS = {
    "veredicto", "texto_explicacao", "audio_base64", "image_base64",
    "session_status", "current_item", "current_tarefa",
    "wrong_answer_count", "adult_intervention_required",
}


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
```

### E2.6 — Task 8 REESCRITA: idempotência com marcador de intenção (AUD-01)

SUBSTITUI integralmente os Steps 1–3 da Task 8 do plano.

**(a) Pré-requisito — `backend/persistence.py`** (função ADITIVA; nada
existente muda):

```python
import contextlib
import os
import tempfile


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

Teste (adicionar a `tests/test_persistence.py` — só ADIÇÃO):

```python
@pytest.mark.asyncio
async def test_write_json_atomic_roundtrip(tmp_path):
    from persistence import read_json, write_json_atomic
    target = tmp_path / "cache.json"
    await write_json_atomic(target, {"a": 1})
    assert await read_json(target) == {"a": 1}
    assert list(tmp_path.glob("*.tmp")) == []
```

**(b) Replay fail-safe** — primeira coisa DENTRO de `async with _state_lock:`:

```python
        # Contrato v1.1 (AUD-01): idempotent replay with fail-safe recovery.
        # "done"       -> return the stored response (no reprocessing).
        # "processing" -> a previous attempt may have mutated state/diary but
        #                 never stored its response. NEVER reprocess silently:
        #                 409 tells the device to re-hydrate and start a NEW
        #                 turn with a NEW request_id.
        # unreadable   -> outcome unknown: quarantine the file, same 409. The
        #                 next NEW request_id then processes normally.
        if request_id:
            cache_path = DATA_DIR / "last_turn_response.json"
            try:
                stored = await read_json(cache_path)
            except Exception:
                cache_path.replace(cache_path.with_suffix(".corrupt.json"))
                raise HTTPException(
                    409,
                    detail="Último turno indeterminado — re-hidrate /api/state e envie um novo turno",
                )
            if stored and stored.get("request_id") == request_id:
                if stored.get("status") == "done":
                    return TutoringResponse.model_validate(stored["response"])
                raise HTTPException(
                    409,
                    detail="Turno com este request_id pode já ter sido aplicado — re-hidrate /api/state e envie um novo turno",
                )
```

**(c) Marcador de intenção** — imediatamente APÓS o bloco try/except do passo
10 (mídia gerada com sucesso) e ANTES do passo 11 (contadores/persistência):

```python
        # Intent marker (AUD-01): from here on this turn mutates persistent
        # files. If we die before the "done" record below, a retry gets the
        # fail-safe 409 above instead of a silent second application.
        if request_id:
            await write_json_atomic(DATA_DIR / "last_turn_response.json", {
                "request_id": request_id,
                "status": "processing",
            })
```

**(d) Consolidação DENTRO do lock + limpeza de mídia DEPOIS** — o passo 15 do
plano muda: a construção do `TutoringResponse` move para DENTRO do lock,
imediatamente após o passo 13b, seguida da gravação do cache e da captura dos
nomes a preservar:

```python
        # Build the response and store the idempotent record INSIDE the lock
        # (AUD-01): a queued retry must find "done" the moment the lock frees.
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
            await write_json_atomic(DATA_DIR / "last_turn_response.json", {
                "request_id": request_id,
                "status": "done",
                "response": response.model_dump(),
            })
        media_keep = (
            {audio_url.rsplit("/", 1)[1], image_url.rsplit("/", 1)[1]}
            if media == "url"
            else None
        )
    # ---- lock released here ----
    # Cleanup only AFTER the new response is durably stored (AUD-03).
    if media_keep is not None:
        cleanup_turn_media(DATA_DIR / "media", keep=media_keep)
```

O passo 14 (debug) permanece após o lock, e o retorno final é o bloco da
E2.5(c). Import em `main.py`: `from media_store import save_turn_media,
cleanup_turn_media, MEDIA_CONTENT_TYPES` e `from persistence import read_json,
write_json, write_json_atomic`.

**(e) Testes** — SUBSTITUI a classe `TestTurnIdempotency` do plano por:

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
        assert state["usage_counters"]["llm_calls"] == 1

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
            {"request_id": "req-stuck", "status": "processing"},
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
        assert not (setup_session / "last_turn_response.json").exists()
        assert (setup_session / "last_turn_response.corrupt.json").exists()
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
```

(imports adicionais no topo de `test_turn_device.py`: `import json`.)

Nota de semântica registrada: falhas ANTES do marcador (Gemini/mídia, passos
8–10) continuam reprocessáveis com o mesmo `request_id` — nada do turno de
sucesso foi persistido (os contadores de falha técnica persistidos nesses
caminhos são, como hoje, por tentativa). Falhas APÓS o marcador caem no 409
fail-safe.

### E2.7 — Task 9: corpo integral de `_run_prepare` (AUD-09)

SUBSTITUI o Step 3 da Task 9. `prepare_lesson` mantém as validações v1
(contagem ≤20, MIME, ≤10 MB) coletando `images` e `content_types`, e termina
com `return await _run_prepare(images, content_types)`. A função completa
(corpo movido de `main.py:170-256`, com a única adaptação do loop de
gravação):

```python
async def _run_prepare(images: list[bytes], content_types: list[str]) -> dict:
    """Shared /api/prepare pipeline (v1 batch and v1.1 staged finish).

    Body moved verbatim from prepare_lesson (AUD-09): saves pages to
    data/images, extracts via Gemini, and writes lesson_tasks.json +
    state.json + conversation.json.
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
    await write_json(DATA_DIR / "lesson_tasks.json", lesson.model_dump())

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
    await write_json(DATA_DIR / "state.json", session.model_dump())

    # Zero the conversation diary for the new lesson (D-06)
    await write_json(
        DATA_DIR / "conversation.json",
        ConversationLog(lesson_id=lesson_id).model_dump(),
    )

    return {
        "status": "ready",
        "summary": lesson.model_dump(),
    }
```

Terminologia corrigida: `test_prepare.py` é a **suíte de regressão** do
`/api/prepare` (roda obrigatoriamente no Step 5); a **paridade v1 explícita**
do turno é o golden de chaves da E2.5(d).

### E2.8 — Task 11 reduzida

Sem os passos 1–2 (movidos para a Task 3.9). Mantém: validação E2E
Gemini+WAV com lição real, registro dos resultados no contrato canônico,
suíte completa + ruff, smoke do frontend web, commits finais.

---

## Parte 3 — Pré-condições operacionais GSD/Git no `licao_casa` (AUD-05, AUD-06, D3)

Executar ANTES de abrir o milestone (sessão local do `licao_casa`, com o
proprietário):

1. Reconciliar `STATE.md` × `ROADMAP.md` × summaries/UATs da V1 (fase 5
   consta "executing" num e "completa" no outro).
2. Auditar/arquivar o milestone V1 no GSD.
3. Baseline: `5588b3e` confirmada (D3) como baseline do código V1 — a tag só
   é criada após a reconciliação acima; arquivos não rastreados ficam fora.
4. Criar branch dedicada (ex.: `milestone/device-contract-v1.1`) — a config
   atual (`branching_strategy: "none"`) commitaria em `main`.
5. Verificar/instalar as definições de agentes GSD (`agents_installed=false`
   reportado) ou escolher modo de execução suportado.
6. Converter MECANICAMENTE as Tasks 3–11 (com esta emenda aplicada) em
   artefatos `PLAN.md` válidos do GSD — sem redesenho por planner/roadmapper;
   o conteúdo técnico é o do plano + emenda, verbatim.
7. Preservar todos os arquivos não rastreados existentes (auditoria, resposta,
   notas).

---

## Parte 4 — Repositório `xiaozhi-esp32`: nova task pós-validação (AUD-07)

### E4.1 — Nova Task 12: atualizar `plano-firmware.md` ao contrato v1.1

Executada NESTE repositório, imediatamente após os resultados da Task 11 e
ANTES de qualquer fase do firmware que toque rede/mídia (F1, F3, F4, F7):

1. **Q3(a) (voz do tutor)**: marcar a decisão "decodificação MP3" como
   **superseded** pelo WAV/PCM do contrato v1.1 (mantendo MP3 documentado
   como contingência se a Task 3.9/11 a tiver acionado).
2. **F3**: substituir "base64→PNG" / "base64→MP3" por download via
   `audio_url`/`image_url` (+ `request_id`, `image_max_px=1280`,
   `audio_format` conforme validação); manter o fluxo 502/409/re-hidratação.
3. **Regra de retry**: substituir "sem retry" por "retry apenas com o mesmo
   `request_id`; 409 de indeterminação ⇒ re-hidratar + novo request_id".
4. **F7**: substituir envio em lote único por `/api/prepare/start|page|finish`
   (a estratégia de RAM muda de "lote em PSRAM" para "uma página por vez").
5. **Processo transversal**: substituir "O backend jamais é alterado" pela
   regra de duas zonas + referência ao contrato canônico.
6. **F1**: adicionar o token (`DEVICE_API_TOKEN`) ao provisionamento
   (NVS `pv_settings`) e o tratamento de `401`/`503` do middleware.
7. Registrar fallbacks sobreviventes da validação empírica.

---

## Ordem de execução consolidada

1. Parte 1 (xiaozhi: governança + contrato + redação) — commits neste repo.
2. Parte 3 (licao_casa: pré-condições GSD/Git, com o proprietário).
3. Conversão mecânica → `PLAN.md` (Parte 3, item 6).
4. Tasks 3 → 3.9 (preflight, com autorização) → 4 → 5 → 6 → 7 → 8 → 9 → 10 →
   11, todas com as correções desta emenda, suíte completa por task.
5. Revisão independente final (thread codex nova).
6. Parte 4 (xiaozhi: Task 12, plano-firmware) — só então F1+ do firmware.
