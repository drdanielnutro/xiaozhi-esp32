# Emenda 02 ao plano "Contrato de dispositivo v1.1" (cumulativa sobre a Emenda 01)

**Data:** 30/07/2026
**Status:** aguardando revisão do auditor — NENHUMA implementação, reconciliação
GSD, branch, tag ou edição de `AGENTS.md`/decision policy/contrato/plano
original foi iniciada.

**Declarações de precedência:**
- A Emenda 01 continua válida em tudo que esta emenda não substitui.
- Em conflito, a **Emenda 02 prevalece** sobre a Emenda 01 e sobre o plano.
- D1 (v1 literal), D2 (fail-closed 401/503) e D3 (baseline condicionada)
  permanecem como registrados na Emenda 01.
- O plano executável futuro será a **consolidação mecânica** de: plano
  original + Emenda 01 + Emenda 02 (nenhum redesenho).

---

## E02.1 — Ciclo de vida completo do cache idempotente

SUBSTITUI os blocos E2.6(b), E2.6(c) e a parte de consolidação/limpeza de
E2.6(d) da Emenda 01.

### Novo schema do cache (`data/last_turn_response.json`)

```json
{
  "seen_request_ids": ["<ids já aplicados nesta instalação, máx. 200>"],
  "last": {
    "request_id": "<id do último turno replayável>",
    "status": "processing | done | indeterminate",
    "response": { "...": "presente apenas quando status=done" }
  }
}
```

`last: null` = não há turno replayável (supersedido por turno v1 ou por nova
lição). `seen_request_ids` impede reprocessamento silencioso de ids antigos.

### (a) Helpers em `main.py` (adicionar `import contextlib` ao topo)

```python
_TURN_CACHE_NAME = "last_turn_response.json"

_FAILSAFE_409 = (
    "Turno com este request_id pode já ter sido aplicado — "
    "re-hidrate /api/state e envie um novo turno com request_id novo"
)


def _turn_cache_path() -> Path:
    return DATA_DIR / _TURN_CACHE_NAME


async def _quarantine_turn_cache(request_id: str) -> None:
    """Replace an unreadable/invalid cache with an 'indeterminate' record.

    The id that hit the bad cache stays blocked (it may be the very id the
    lost record referred to), so retrying it keeps failing safe; any NEW
    request_id processes normally. Known bound (documented): ids recorded
    ONLY in the unreadable file are forgotten — a protocol-following device
    never retries them (it re-hydrates and generates a new id after 409).
    """
    path = _turn_cache_path()
    with contextlib.suppress(OSError):
        path.replace(path.with_suffix(".corrupt.json"))
    await write_json_atomic(path, {
        "seen_request_ids": [request_id],
        "last": {"request_id": request_id, "status": "indeterminate"},
    })
```

### (b) Replay fail-safe — primeira coisa dentro de `async with _state_lock:`

```python
        seen: list[str] = []
        if request_id:
            try:
                cache = await read_json(_turn_cache_path())
            except Exception:
                await _quarantine_turn_cache(request_id)
                raise HTTPException(409, detail=_FAILSAFE_409)
            last = (cache or {}).get("last") or {}
            seen = (cache or {}).get("seen_request_ids") or []
            if last.get("request_id") == request_id:
                if last.get("status") == "done":
                    try:
                        return TutoringResponse.model_validate(last["response"])
                    except Exception:
                        # Valid JSON, invalid schema: same quarantine, no 500.
                        await _quarantine_turn_cache(request_id)
                        raise HTTPException(409, detail=_FAILSAFE_409)
                # "processing" / "indeterminate": may already be applied.
                raise HTTPException(409, detail=_FAILSAFE_409)
            if request_id in seen:
                # Applied by an older turn; its response is no longer stored.
                raise HTTPException(409, detail=_FAILSAFE_409)
```

### (c) Marcador de intenção — após o sucesso do passo 10, antes do passo 11

```python
        if request_id:
            seen = (seen + [request_id])[-200:]
            await write_json_atomic(_turn_cache_path(), {
                "seen_request_ids": seen,
                "last": {"request_id": request_id, "status": "processing"},
            })
```

### (d) Consolidação + supersessão + limpeza — DENTRO do lock, após o passo 13b

(A construção do `response` dentro do lock permanece como na E2.6(d) da
Emenda 01; muda o que vem depois dela:)

```python
        if request_id:
            await write_json_atomic(_turn_cache_path(), {
                "seen_request_ids": seen,
                "last": {
                    "request_id": request_id,
                    "status": "done",
                    "response": response.model_dump(),
                },
            })
        else:
            # A v1 (web) turn advanced the session: any previous device
            # response is now stale and must never replay. seen ids are kept
            # so stale retries fail safe instead of reprocessing.
            try:
                cache = await read_json(_turn_cache_path())
            except Exception:
                cache = {}
            if cache:
                await write_json_atomic(_turn_cache_path(), {
                    "seen_request_ids": cache.get("seen_request_ids") or [],
                    "last": None,
                })
        # Cleanup INSIDE the lock (E02.2): after the cache is durably updated,
        # and never concurrent with the next turn's save_turn_media.
        if media == "url":
            cleanup_turn_media(
                DATA_DIR / "media",
                keep={audio_url.rsplit("/", 1)[1], image_url.rsplit("/", 1)[1]},
            )
    # ---- lock released here ----
```

O passo 14 (debug) e o retorno final (E2.5(c) da Emenda 01) permanecem fora
do lock, inalterados. Nenhuma limpeza de mídia ocorre fora do lock.

### (e) Nova lição invalida o cache — adicionar ao final de `_run_prepare`
(antes do `return {"status": "ready", ...}`)

```python
    # New lesson supersedes any replayable turn response. seen ids are
    # preserved so a stale device retry from the old lesson fails safe (409)
    # instead of silently creating a turn on the NEW lesson.
    cache_path = DATA_DIR / _TURN_CACHE_NAME
    try:
        cache = await read_json(cache_path)
    except Exception:
        cache = {}
    await write_json_atomic(cache_path, {
        "seen_request_ids": (cache.get("seen_request_ids") or []) if cache else [],
        "last": None,
    })
```

### (f) Testes (em `tests/test_turn_device.py`, classe `TestTurnIdempotency`)

Ajuste a testes da Emenda 01: em `test_corrupt_cache_fails_safe_and_quarantines`,
a asserção `assert not (setup_session / "last_turn_response.json").exists()`
é SUBSTITUÍDA por (a quarentena agora grava um registro `indeterminate`):

```python
        assert (setup_session / "last_turn_response.corrupt.json").exists()
        cache = json.loads((setup_session / "last_turn_response.json").read_text())
        assert cache["last"]["status"] == "indeterminate"
```

Testes NOVOS:

```python
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
    async def test_new_lesson_supersedes_previous_replay(
        self, client, setup_session, mock_polly, mock_image_gen
    ):
        from test_prepare_staged import _extraction_ok
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
    async def test_done_with_invalid_schema_quarantines_409(
        self, client, setup_session
    ):
        from persistence import write_json_atomic
        await write_json_atomic(
            setup_session / "last_turn_response.json",
            {
                "seen_request_ids": ["req-x"],
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
```

(`test_new_lesson_...` reutiliza `_extraction_ok` de `test_prepare_staged.py`;
o teste da Emenda 01 `test_corrupt_cache_...` já cobre "id NOVO processa
normalmente após quarentena".)

## E02.2 — Consistência cache × mídia

A mudança de código central (limpeza dentro do lock, após o `done`) já está
no bloco E02.1(d). `save_turn_media`/`cleanup_turn_media` permanecem como na
E2.4 da Emenda 01 (escrita nunca apaga; limpeza separada). Testes NOVOS:

**(a)** Em `tests/test_media_endpoint.py` — falha na escrita do segundo
arquivo preserva o conjunto anterior:

```python
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
```

**(b)** Em `tests/test_turn_device.py` — falha entre a mídia e a consolidação
mantém o replay anterior íntegro e baixável (cobre também "replay anterior
ainda baixável"):

```python
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

        # Second turn dies at the intent marker (before any cache change).
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
```

**(c)** Em `tests/test_turn_device.py` — `media=url` SEM `request_id`
supersede e limpa de forma consistente (nenhuma resposta armazenada aponta
para mídia removida):

```python
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
```

(O teste unitário "limpeza preserva exatamente os referenciados" já existe na
E2.4 da Emenda 01: `test_cleanup_removes_only_unreferenced`.)

Nota de semântica: um turno `media=url` sem `request_id` remove a mídia do
replay anterior **somente depois** de gravar `last: null` — o registro
supersedido deixa de ser replayável no mesmo instante em que perde a mídia,
dentro do lock. A invariante "nenhuma resposta armazenada (replayável) aponta
para mídia removida" vale em qualquer ponto observável.

## E02.3 — Marcador de intenção: semântica precisa (texto para a consolidação)

Na consolidação (Parte 1 da Emenda 01, item E1.2.2 — ainda NÃO aplicar), o
texto do contrato sobre o marcador passa a ser exatamente:

> O marcador `processing` é gravado após a geração de mídia e **antes dos
> efeitos persistentes do caminho de sucesso do turno**: transições de
> veredito e contadores em `state.json`, appendice no `conversation.json` e o
> registro `done`. Ficam explicitamente FORA da sua proteção (semântica por
> tentativa, idêntica à v1): os arquivos de mídia já escritos em disco
> (inertes até serem referenciados por um `done`) e os incrementos de falha
> técnica persistidos pelos caminhos de erro (502). Falha ANTES do marcador ⇒
> retry com o mesmo `request_id` reprocessa; falha DEPOIS ⇒ `409` fail-safe.

## E02.4 — Golden específico do `/api/prepare` v1 (Task 9)

ADICIONA ao Step 1 da Task 9 (arquivo `tests/test_prepare_staged.py`; requer
`import json` no topo). **Ordem TDD obrigatória:** escrever e rodar este teste
ANTES da refatoração `_run_prepare` (deve passar contra o código atual), e
rodá-lo novamente depois — é a evidência de paridade do `/api/prepare`; o
golden de `/api/turn` (E2.5 da Emenda 01) não vale como evidência aqui.

```python
PREPARE_V1_KEYS = {"status", "summary"}
SUMMARY_KEYS = {"lesson_id", "created_at", "source_pages", "items"}


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
        assert set(body.keys()) == PREPARE_V1_KEYS
        assert body["status"] == "ready"
        assert set(body["summary"].keys()) == SUMMARY_KEYS
        state = json.loads((data_dir / "state.json").read_text())
        assert state["lesson_id"] == body["summary"]["lesson_id"]
        assert state["session_status"] == "active"
        conv = json.loads((data_dir / "conversation.json").read_text())
        assert conv == {"lesson_id": body["summary"]["lesson_id"], "turns": []}
        assert (data_dir / "lesson_tasks.json").exists()
```

(Exceção de ordem: a invalidação de cache do E02.1(e) entra em `_run_prepare`
**junto com** a refatoração; este golden não cobre `last_turn_response.json`,
coberto por `test_new_lesson_supersedes_previous_replay`.)

## E02.5 — Autenticação: origem desconhecida é fail-closed; Task 10 corrigida

**(a)** SUBSTITUI, no middleware da E2.1 da Emenda 01, a constante e a
checagem de origem por:

```python
# Loopback exempt (web frontend via Vite proxy). An ABSENT/unknown origin is
# NOT loopback: fail-closed (Emenda 02).
_LOOPBACK_HOSTS = {"127.0.0.1", "::1", "localhost"}


def _is_loopback(client_host: str | None) -> bool:
    return client_host is not None and client_host in _LOOPBACK_HOSTS
```

e, no corpo do middleware, `if client_host not in _LOOPBACK_HOSTS:` passa a
ser `if not _is_loopback(client_host):`.

**(b)** ADICIONA a `tests/test_device_auth.py`:

```python
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

**(c)** SUBSTITUI, na Task 10 (Step 2, `.env.example`), o bloco por:

```
# Token exigido de clientes fora do loopback (dispositivo ESP32).
# Fail-closed (D2): sem este token configurado, conexões remotas recebem 503;
# apenas loopback funciona. Configure antes de conectar o dispositivo.
DEVICE_API_TOKEN=
```

e, no ponteiro `CONTRATO-DISPOSITIVO.md` (Step 1), a menção ao token passa a
"token de dispositivo obrigatório para clientes não-loopback (fail-closed)".

---

## Registro de pendências para a consolidação (nada disso é aplicado agora)

1. Contrato canônico: seção de idempotência reescrita com o schema
   `seen_request_ids`/`last`, supersessão por turno v1 e por nova lição,
   status `indeterminate` e o texto do E02.3; seção de mídia com a invariante
   do E02.2.
2. Parte 1 da Emenda 01 (governança) e Parte 3 (GSD) permanecem pendentes e
   inalteradas.
3. Consolidação mecânica: plano + Emenda 01 + Emenda 02 → documento executável
   único (após aprovação do auditor).
