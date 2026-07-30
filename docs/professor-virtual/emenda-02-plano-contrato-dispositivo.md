# Emenda 02 ao plano "Contrato de dispositivo v1.1" (cumulativa sobre a Emenda 01)

**Data:** 30/07/2026
**Status:** correção direta revisada e apta à consolidação documental —
NENHUMA implementação, reconciliação GSD, branch, tag ou edição de
`AGENTS.md`/decision policy/contrato/plano original foi iniciada.

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
  "seen_request_ids": ["<ids reservados ou aplicados nesta instalação>"],
  "rehydration_required": false,
  "last": {
    "request_id": "<id do último turno replayável>",
    "status": "processing | done | indeterminate",
    "response": { "...": "presente apenas quando status=done" }
  }
}
```

`last: null` = não há turno replayável (supersedido por mutação posterior).
Com cache íntegro, `seen_request_ids` impede reprocessamento silencioso de ids
antigos.
`rehydration_required: true` = o histórico ficou indeterminado num caminho
que não reserva o `request_id` corrente; a primeira tentativa futura recebe
409, fica registrada como `indeterminate`, e só um id novo pode prosseguir.
Na v1.1, o ledger **não é truncado**: expulsar um id faria uma repetição muito
antiga voltar a ser processada. Qualquer compactação futura exige outro
mecanismo durável (por exemplo, namespaces por instalação/lição) e mudança
explícita de protocolo; não pode ser introduzida como otimização interna.

### (a) Helpers em `main.py` (adicionar `import contextlib` e `import shutil`
ao topo)

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

Como `shutil` passa a entrar em `main.py` na Task 8, a instrução de importá-lo
novamente na Task 9 do plano original já estará satisfeita; a consolidação não
deve duplicar o import.

### (b) Replay fail-safe — primeira coisa dentro de `async with _state_lock:`

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

### (c) Barreira de supersessão — após o sucesso do passo 10, antes do passo 11

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

Para requisição v1.1, essa barreira é o marcador de intenção. Para requisição
v1 sem `request_id`, é a supersessão prévia de qualquer resposta do dispositivo.
Uma falha posterior pode tornar a resposta anterior indisponível, mas nunca
deixa um estado novo coexistir com replay antigo — escolha conservadora
fail-safe.

### (d) Consolidação + limpeza — DENTRO do lock, após o passo 13b

(A construção do `response` dentro do lock permanece como na E2.6(d) da
Emenda 01; muda o que vem depois dela:)

```python
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
        # Cleanup INSIDE the lock (E02.2): after the cache is durably updated,
        # or was superseded at the barrier, and never concurrent with the next
        # turn's save_turn_media. cleanup_turn_media is fully best-effort.
        if media == "url":
            cleanup_turn_media(
                DATA_DIR / "media",
                keep={audio_url.rsplit("/", 1)[1], image_url.rsplit("/", 1)[1]},
            )
    # ---- lock released here ----
```

O passo 14 (debug) e o retorno final (E2.5(c) da Emenda 01) permanecem fora
do lock, inalterados. Nenhuma limpeza de mídia ocorre fora do lock.

### (e) Mutações de estado fora de `/api/turn`

Uma resposta só pode permanecer replayável enquanto nenhum outro caminho
tiver alterado o estado que ela descreve. Portanto, a Task 8 também:

1. SUBSTITUI o final de `update_state`, ainda dentro de `_state_lock`, por:

```python
        new_state = transform_fn(state)
        new_state.updated_at = datetime.utcnow().isoformat()
        # adult/resolve (único consumidor atual de update_state) supersedes
        # any tutoring response before publishing its state transition.
        await _supersede_turn_cache()
        await write_json(DATA_DIR / "state.json", new_state.model_dump())
        return new_state
```

2. SUBSTITUI integralmente `get_state` para que a transição persistente para
`expired` seja revalidada sob o mesmo lock e também superseda o cache antes
da gravação. Leituras que não expiram a sessão continuam sem esperar o lock:

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

3. Nos dois handlers de erro 502 de `/api/turn` — exceção de Gemini/LLM no
passo 8 e exceção de mídia no passo 10 — ADICIONA
`await _supersede_turn_cache()` imediatamente antes do respectivo
`await write_json(...state.json...)`:

```python
            # This failed attempt persists a technical-failure mutation, so
            # any response from an earlier successful turn is now stale.
            # Do not reserve the current request_id: failure happened before
            # its processing marker, and that same id remains eligible.
            await _supersede_turn_cache()
            await write_json(DATA_DIR / "state.json", state.model_dump())
```

O helper E02.1(a) deve ser posicionado acima de `update_state`, para estar
disponível tanto a esses caminhos quanto a `/api/turn` e `_run_prepare`.
Falha ao superseder impede a persistência da mutação externa; nunca se publica
estado novo mantendo replay antigo.

### (f) Nova lição

A supersessão de cache e a publicação da nova lição pertencem à **Task 9**,
pois dependem da refatoração de `_run_prepare`. O bloco normativo e seus testes
foram movidos para E02.4; nada referente a `_run_prepare` entra na Task 8.

### (g) Testes (em `tests/test_turn_device.py`, classe `TestTurnIdempotency`)

Dois testes herdados da Emenda 01 precisam ser substituídos para o schema
novo. Em `test_processing_marker_fails_safe_409`, a gravação do cache passa a
ser:

```python
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
```

Em `test_corrupt_cache_fails_safe_and_quarantines`, a asserção
`assert not (setup_session / "last_turn_response.json").exists()` é
SUBSTITUÍDA por (a quarentena agora grava um registro `indeterminate`):

```python
        assert (setup_session / "last_turn_response.corrupt.json").exists()
        cache = json.loads((setup_session / "last_turn_response.json").read_text())
        assert cache["last"]["status"] == "indeterminate"
        assert cache["rehydration_required"] is False
```

Em `test_same_request_id_replays_without_processing`, SUBSTITUIR a asserção
isolada de `llm_calls` por todos os efeitos persistentes protegidos:

```python
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
```

O teste da Emenda 01 `test_corrupt_cache_...` continua cobrindo "id NOVO
processa normalmente após quarentena". O teste de supersessão por nova lição
fica exclusivamente na Task 9 (E02.4), eliminando a dependência futura que
quebraria a suíte ao final da Task 8.

### (h) Files e commit da Task 8

SUBSTITUI a lista **Files** e o Step 5 da Task 8 original, incorporando o
pré-requisito de persistência introduzido pela Emenda 01:

- Modify: `backend/main.py`
- Modify: `backend/persistence.py`
- Test: `backend/tests/test_turn_device.py`
- Test: `backend/tests/test_persistence.py`

Depois dos testes da Task 8 e da suíte completa verdes:

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add backend/main.py backend/persistence.py \
  backend/tests/test_turn_device.py backend/tests/test_persistence.py
git commit -m "/api/turn: idempotência fail-safe por request_id"
```

## E02.2 — Consistência cache × mídia

A mudança de código central (limpeza dentro do lock, após `done` ou após a
supersessão v1) já está no bloco E02.1(d). `save_turn_media` permanece como
na E2.4 da Emenda 01 (escrita nunca apaga; limpeza separada).

SUBSTITUI a implementação de `cleanup_turn_media` da Emenda 01 para que
**toda** a limpeza seja best-effort — inclusive enumeração, inspeção e
remoção. Depois que estado/cache foram publicados, uma falha de housekeeping
jamais pode transformar o turno em erro:

```python
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

Alocação obrigatória na consolidação: a implementação acima e os testes
**(a)/(b)** entram na **Task 6**, junto de `media_store.py`; os testes de
integração **(c)/(d)/(e)** entram na **Task 8**, quando cache e barreira já
existem. Assim, a suíte ao fim de cada task não depende de código futuro.

Testes NOVOS:

**(a)** Em `tests/test_media_endpoint.py` — enumeração inacessível não vaza
erro depois da consolidação (adicionar `from pathlib import Path` caso ainda
não exista):

```python
    def test_cleanup_swallows_directory_enumeration_failure(
        self, tmp_path, monkeypatch
    ):
        media = tmp_path / "media"
        media.mkdir()

        def fail_iterdir(_self):
            raise OSError("I/O error")

        monkeypatch.setattr(Path, "iterdir", fail_iterdir)
        cleanup_turn_media(media, keep=set())  # Must not raise.
```

**(b)** Em `tests/test_media_endpoint.py` — falha na escrita do segundo
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

**(c)** Em `tests/test_turn_device.py` — falha entre a mídia e a consolidação
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
```

**(d)** Em `tests/test_turn_device.py` — `media=url` SEM `request_id`
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

**(e)** Em `tests/test_turn_device.py` — proteção estrutural para que a
limpeza não seja movida acidentalmente para fora do lock:

```python
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

(O teste unitário "limpeza preserva exatamente os referenciados" já existe na
E2.4 da Emenda 01: `test_cleanup_removes_only_unreferenced`.)

Nota de semântica: um turno `media=url` sem `request_id` remove a mídia do
replay anterior **somente depois** de gravar `last: null` — o registro
supersedido deixa de ser replayável no mesmo instante em que perde a mídia,
dentro do lock. A invariante "nenhuma resposta armazenada (replayável) aponta
para mídia removida" vale em qualquer ponto observável.

## E02.3 — Contrato normativo completo de idempotência

SUBSTITUI **integralmente** o item E1.2.2 da Emenda 01 e os trechos
conflitantes do contrato atual. A consolidação não deve apenas acrescentar
parágrafos.

### (a) Princípio de retry

No item 3 de `## Princípios`, usar literalmente:

> 3. **Retry de turno:** retry de `POST /api/turn` continua PROIBIDO sem
> `request_id`. Com cache íntegro e cliente serial conforme este contrato,
> uma retransmissão do mesmo turno não é aplicada silenciosamente duas vezes:
> ela devolve o replay 200 quando a última resposta ainda está disponível, ou
> 409 quando o resultado é indeterminado/supersedido. Após 409, o dispositivo
> descarta todos os ids pendentes anteriores, re-hidrata `GET /api/state` +
> `GET /api/lesson` e só então inicia um turno lógico novo com UUID novo.

### (b) Regra do identificador

Na tabela de campos de `POST /api/turn`, SUBSTITUIR a linha de `request_id`
por:

```markdown
| `request_id` | str | 1–128 caracteres, não branco; único durante toda a instalação (UUID v4 recomendado) | chave de idempotência (ver abaixo) |
```

Texto normativo logo abaixo da tabela:

> O cliente gera um `request_id` novo para cada turno lógico novo e jamais
> reutiliza um id para outro conteúdo, outra lição ou outro momento da
> instalação. O mesmo id só pode reaparecer com os mesmos campos e bytes de
> mídia do turno original quando a resposta se perdeu. O backend rejeita
> `request_id` composto apenas por espaços ou com mais de 128 caracteres com
> 400. Por normalização da stack de formulários, valor vazio
> (`request_id=`) equivale a campo omitido: não ativa idempotência nem o
> perfil v1.1.

Essa obrigação de unicidade é necessária porque o ledger atravessa turnos e
lições. “String livre” não significa reutilizável.

### (c) Seção `### Idempotência (request_id)`

SUBSTITUIR a seção atual inteira por:

> ### Idempotência (`request_id`)
>
> O cache `data/last_turn_response.json` contém:
>
> - `seen_request_ids`: ledger dos ids já reservados/aplicados;
> - `rehydration_required`: sentinela de recuperação após corrupção descoberta
>   sem um id corrente;
> - `last`: o único registro cuja resposta ainda pode ser replayada
>   (`processing`, `done`, `indeterminate` ou `null`).
>
> O ledger íntegro não é truncado na v1.1. Remover um id antigo permitiria
> que ele fosse aplicado novamente. Compactação futura só pode ocorrer com
> mecanismo durável equivalente e mudança explícita de protocolo.
>
> Para uma requisição com `request_id`, antes de LLM/TTS/geração de imagem e de
> qualquer mutação de estado:
>
> - se `last` tem o mesmo id e status `done`, o backend devolve exatamente a
>   resposta armazenada (200), sem LLM, TTS, imagem, contador ou transição,
>   desde que ela valide e seu campo `request_id` ecoe o mesmo id; divergência
>   é cache inválido e segue o fluxo 409;
> - se o id consta em `seen_request_ids`, mas não existe resposta replayável,
>   ou se `last` tem status `processing`/`indeterminate`, o backend devolve 409;
> - se `rehydration_required=true`, a primeira tentativa futura é registrada
>   como `indeterminate` e recebe 409; somente outro id, criado depois da
>   re-hidratação, pode prosseguir;
> - somente um id ainda não visto entra no pipeline.
>
> Depois que LLM e mídia terminam, mas antes dos efeitos persistentes do
> caminho de sucesso, o backend grava atomicamente `processing`. Em seguida
> persiste transições/contadores em `state.json` e o append em
> `conversation.json`; ao final grava atomicamente `done` + resposta.
>
> O marcador não cobre os arquivos de mídia já escritos (inertes até serem
> referenciados por um `done`) nem os incrementos de falha técnica dos
> caminhos 502, que mantêm a semântica por tentativa da v1. Antes de persistir
> esse incremento técnico, o backend supersede qualquer replay anterior, mas
> não reserva o id da tentativa que falhou. Falha antes de `processing` deixa
> esse mesmo id elegível para processamento; falha depois de `processing`
> deixa o id em 409 fail-safe. Portanto, o status HTTP isolado não autoriza
> retry automático.
>
> Qualquer mutação de estado que não pertence à própria resposta armazenada
> grava a supersessão (`last: null`) **antes** de persistir o novo estado.
> Isso inclui: turno v1 sem `request_id`, publicação de nova lição,
> `POST /api/adult/resolve` bem-sucedido e expiração persistida por
> `GET /api/state`, além dos incrementos técnicos dos caminhos 502 anteriores
> ao marcador. Os ids conhecidos permanecem no ledger e retornam 409; a
> resposta anterior deixa de ser replayável. Falha ao gravar a supersessão
> impede a mutação subsequente.
>
> Cache ilegível ou semanticamente inválido nunca faz o backend assumir que o
> id corrente (ou o primeiro id futuro) é novo. O arquivo é copiado para
> `last_turn_response.corrupt.json` e substituído atomicamente. Quando existe
> um id corrente, ele fica `indeterminate` e recebe 409; quando a corrupção é
> descoberta por uma mutação que não reserva o id corrente (turno v1, nova
> lição, `adult/resolve`, expiração ou caminho 502 pré-marcador),
> `rehydration_required=true` força 409 na primeira tentativa futura. Depois
> do 409, o dispositivo re-hidrata e usa id novo.
>
> **Limite sob corrupção integral:** ids que existiam somente no arquivo
> ilegível não podem ser reconstruídos. Por isso, a garantia nesse cenário
> depende do cliente único/serial obedecer ao 409: descartar qualquer id
> anterior e gerar UUID novo após re-hidratar. Um cliente fora do protocolo
> que, depois do 409, envie outro id histórico perdido pode fazê-lo parecer
> novo. Eliminar esse limite exigiria ledger separado com durabilidade
> independente e fica fora da v1.1.
>
> O 409 significa “a resposta não está disponível e o turno pode já ter sido
> aplicado”; não é resposta pedagógica e não deve ser reproduzido como turno.

### (d) Validação do `request_id` na Task 7

ADICIONA à Task 7, imediatamente após a validação “exactly one input” e antes
de calcular o perfil v1.1:

```python
    if request_id is not None and (
        not request_id.strip() or len(request_id) > 128
    ):
        raise HTTPException(
            400,
            detail="request_id must be non-blank and at most 128 characters",
        )
```

ADICIONA a `tests/test_turn_device.py`, na classe
`TestTurnDeviceProfile` da Task 7:

```python
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
```

ADICIONA à classe `TestV1Parity` o golden da normalização de campo vazio:

```python
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

IDs legados dos testes (`req-a`, `req-x` etc.) continuam válidos: o servidor
aceita como id qualquer string efetiva não branca com até 128 caracteres;
vazio é omissão. UUID v4 é uma obrigação/recomendação do consumidor, não um
parser rígido no backend.

## E02.4 — Task 9: golden v1 + publicação da nova lição sob lock

### (a) Golden específico de `/api/prepare`

ADICIONA ao Step 1 da Task 9 (arquivo `tests/test_prepare_staged.py`; requer
`import json`, `from datetime import datetime` e `from uuid import UUID` no
topo). O teste compara a resposta com `lesson_tasks.json`, fixa o conteúdo
determinístico e valida as relações dos IDs/tempos dinâmicos em
`state.json`/`conversation.json`; não é apenas um teste de existência.

```python
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
```

**Ordem TDD obrigatória dentro da Task 9:**

1. criar `test_prepare_staged.py` com os testes do plano e este golden;
2. antes de alterar `_run_prepare`, executar somente:
   `python -m pytest tests/test_prepare_staged.py::TestPrepareV1Parity::test_prepare_v1_response_and_artifacts_golden -q`
   — deve passar contra a V1 atual;
3. executar o arquivo completo — deve falhar porque as rotas staged ainda não
   existem;
4. implementar a Task 9;
5. executar novamente o golden isolado e depois a suíte completa.

Assim, a expectativa de falha dos endpoints novos não mascara a prova
pré-refatoração do caminho v1.

### (b) Publicação da nova lição e supersessão do cache

SUBSTITUI, no corpo integral de `_run_prepare` da E2.7 da Emenda 01, a
publicação intercalada dos três arquivos. A construção de `lesson`,
`item_progress`, `session` e `new_conversation` acontece toda em memória.
Remover o `await write_json(...lesson_tasks.json...)` que aparece logo após a
construção de `lesson`; depois de construir `session`, publicar assim:

```python
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

Os retornos `illegible` e 422 continuam antes desse bloco e não invalidam a
lição/cache atuais. As imagens-fonte continuam sendo gravadas antes da
extração, como na V1; esta emenda não promete transação para esses arquivos.
O que ela garante é: falha na supersessão não publica
`lesson_tasks.json`/`state.json`/`conversation.json`, e, depois de uma
supersessão bem-sucedida, nenhum `request_id` antigo volta a ser aplicado.
A publicação em três JSONs conserva a janela de crash já existente na V1;
o lock impede concorrência dentro do processo, não cria uma transação
multi-arquivo.

### (c) Testes de nova lição — somente na Task 9

No topo de `tests/test_prepare_staged.py`, incluir `GeminiEvaluation` no
import de `models` e adicionar helpers locais (sem importar outro módulo de
teste):

```python
def _turn_files():
    return {"audio": ("turn.webm", b"fake-audio", "audio/webm")}


def _teach_eval():
    return GeminiEvaluation(
        veredicto="teach",
        texto_explicacao="Vamos entender juntos.",
        prompt_visual="A simple educational diagram",
    )
```

Adicionar:

```python
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
   `seen_request_ids`/`rehydration_required`/`last`, supersessão por turno v1,
   nova lição, resolução adulta e expiração, status `indeterminate` e o texto
   do E02.3; seção de mídia com a invariante do E02.2.
2. Parte 1 da Emenda 01 (governança) e Parte 3 (GSD) permanecem pendentes e
   inalteradas.
3. Consolidação mecânica: plano + Emenda 01 + Emenda 02 → documento executável
   único. Esta Emenda 02 já passou por revisão independente; a consolidação
   ainda não foi executada.
