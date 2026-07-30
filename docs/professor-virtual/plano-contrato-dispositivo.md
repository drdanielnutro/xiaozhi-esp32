# Contrato de Dispositivo v1.1 — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Adicionar ao backend `licao_casa` um perfil de contrato para o dispositivo embarcado (mídia por URL, áudio WAV/PCM, imagem redimensionada, idempotência por `request_id`, preparação página a página e token de autenticação), preservando 100% do comportamento v1 usado pelo frontend web — e registrar o contrato e as novas regras nos documentos deste repositório.

**Architecture:** Todas as mudanças de backend são **aditivas na borda de transporte** (campos opcionais de formulário, endpoints novos, campos novos de resposta). O miolo pedagógico (`gemini.py`, `session_engine.py`, transições de veredito em `main.py`) não é tocado. Requisições sem os campos novos produzem exatamente as respostas v1 de hoje; a suíte de testes existente continua passando sem edição.

**Tech Stack:** FastAPI + pydantic v2 + aiofiles (backend existente), boto3/Polly (TTS), Pillow (novo, redimensionamento), pytest + pytest-asyncio + httpx ASGITransport (testes).

## Repositórios-alvo (LEIA ANTES DE COMEÇAR)

| Etapa | Repositório | Caminho absoluto |
|---|---|---|
| Etapa A (Tasks 1–2): contrato + CLAUDE.md | **xiaozhi-esp32** (este repo) | `/Users/institutorecriare/VSCodeProjects/xiaozhi-esp32` |
| Etapa B (Tasks 3–11): implementação backend | **licao_casa** | `/Users/institutorecriare/VSCodeProjects/licao_casa` |

**Ordem de execução: Etapa A primeiro, depois Etapa B inteira, e só então o firmware (fases F1+ do `plano-firmware.md`) consome o contrato.** Decisão registrada em 30/07/2026: o backend é implementado e validado ANTES do firmware, porque (a) o contrato precisa estar estável antes de o firmware codificar contra ele; (b) o backend é testável no desktop em minutos, sem hardware; (c) a validação empírica de WAV com Polly/Gemini (Task 11) pode mudar o contrato, e isso tem que acontecer antes da F1/F4 do firmware. A fase F0 do firmware (fundações, sem rede) pode rodar em paralelo se desejado — ela não depende do contrato.

**Nota sobre o GSD:** o repo `licao_casa` tem um workflow próprio (GSD). Este plano é executado diretamente por decisão explícita do usuário (30/07/2026), fora do fluxo GSD.

## Global Constraints

- **v1 intocada:** requisições sem os campos novos retornam byte-a-byte o comportamento atual. Nenhum teste existente pode ser editado ou removido (apenas ADICIONAR testes). A suíte completa (`python -m pytest tests/ -v`) deve passar ao fim de cada task.
- **Miolo pedagógico intocável:** zero edições em `gemini.py`, `session_engine.py`, `celebration.py`, `image_gen.py` (exceto nenhuma) e nas transições de veredito/estado do `/api/turn` (passos 1–9 e 11–13b do handler).
- **Todos os campos novos são opcionais** (`Optional[...] = Form(None)`); ausentes ⇒ caminho v1 exato.
- Token e PIN jamais aparecem em corpo de resposta ou log.
- Ambiente de testes: `cd /Users/institutorecriare/VSCodeProjects/licao_casa/backend && source venv/bin/activate` antes de qualquer `pytest`/`pip`.
- Commits no `licao_casa`: mensagens curtas em português, um commit por task, feitos na raiz do repo `licao_casa` (não deste repo).
- Estilo: seguir o código existente (async + aiofiles, docstrings/comentários em inglês, pydantic v2 `model_copy`/`model_dump`).

---

## Etapa A — repositório `xiaozhi-esp32` (documentação)

### Task 1: Documento canônico do contrato v1.1

**Files:**
- Create: `docs/professor-virtual/contrato-dispositivo.md` (neste repo)

**Interfaces:**
- Produces: o documento que as Tasks 3–9 implementam e que as fases F1/F3/F4/F7 do firmware consomem. Nomes de campos e endpoints aqui são normativos.

- [ ] **Step 1: Escrever o documento com o conteúdo integral abaixo**

````markdown
# Contrato de Dispositivo — Perfil v1.1 (aditivo sobre a API v1)

**Canônico neste arquivo.** Cópia-ponteiro em `licao_casa/CONTRATO-DISPOSITIVO.md`.
Complementa a seção 7 de `DOCUMENTACAO-APP.md`; em conflito, este documento vence
para os campos/endpoints v1.1.

## Princípios

1. **Aditivo:** nada da API v1 muda. Campos novos são opcionais; endpoints novos
   têm rotas novas. O frontend web continua funcionando sem alteração.
2. **Cliente burro:** o dispositivo continua sem decidir nada pedagógico
   (princípio da seção 2 da especificação).
3. Regra de retry revisada: retry de `POST /api/turn` continua PROIBIDO sem
   `request_id`. COM `request_id`, reenviar o MESMO `request_id` após timeout é
   seguro: o backend devolve a resposta armazenada sem criar novo turno.

## Autenticação (token de dispositivo)

- Config: variável de ambiente `DEVICE_API_TOKEN` no backend (`.env`).
- Quando definida, TODA requisição cuja origem não é loopback exige o header
  `Authorization: Bearer <token>` (alternativa: `X-Api-Token: <token>`).
  Falta/erro ⇒ `401 {"detail": "Token inválido ou ausente"}`.
- Loopback (127.0.0.1/::1) é isento — preserva o frontend web local (proxy Vite).
- Quando não definida, comportamento v1 (sem auth) — aceitável só em loopback.

## `POST /api/turn` — campos opcionais novos (multipart form)

| Campo | Tipo | Valores | Efeito |
|---|---|---|---|
| `request_id` | str | livre (recomendado: UUID) | chave de idempotência (ver abaixo) |
| `media` | str | `url` | resposta traz `audio_url`/`image_url`; `audio_base64`/`image_base64` vêm vazios (`""`) |
| `audio_format` | str | `mp3` (default) \| `wav` | `wav` ⇒ Polly PCM 16 kHz mono s16le com header WAV (44 bytes) |
| `image_max_px` | int | 64–4096 | imagem reduzida para caber em `image_max_px` no lado maior (nunca amplia) e reencodada **JPEG** q85 |

Valores fora dos aceitos ⇒ `400`. Campos são ortogonais e combináveis.
Para a tela do dispositivo (1280×800), enviar `image_max_px=1280`.

### Campos novos na resposta (`TutoringResponse`)

```json
{
  "audio_url": "/api/media/turn_<hex32>_audio.wav",
  "image_url": "/api/media/turn_<hex32>_image.jpg",
  "audio_format": "wav",
  "image_format": "jpg",
  "request_id": "<eco do enviado ou null>"
}
```
`audio_url`/`image_url` são **relativas** à base do backend; presentes só com
`media=url` (senão `null`). `audio_format`: `mp3`|`wav`. `image_format`:
`png`|`jpg` (`jpg` quando `image_max_px` foi usado). Clientes v1 ignoram os
campos extras.

### Idempotência (`request_id`)

- O backend guarda a última resposta 200 com `request_id` em
  `data/last_turn_response.json` e os arquivos de mídia correspondentes.
- Turno novo com o MESMO `request_id` ⇒ replay: resposta armazenada, sem chamar
  Gemini/Polly, sem alterar estado ou contadores.
- Só respostas 200 são armazenadas: retry após 4xx/5xx reprocessa (estado não
  avançou — cliente deve re-hidratar `GET /api/state` como na v1).
- Guarda apenas o ÚLTIMO turno (dispositivo único, turnos serializados).

## `GET /api/media/{filename}` (novo)

- Serve os arquivos do último turno gerado com `media=url`.
- `filename` validado por regex estrita (`turn_<hex32>_(audio|image).<ext>`);
  qualquer outro nome ⇒ 404. Content-Type por extensão
  (mp3⇒audio/mpeg, wav⇒audio/wav, png⇒image/png, jpg⇒image/jpeg).
- Arquivos de turnos anteriores são apagados quando um novo turno gera mídia.
  O dispositivo deve baixar logo após o turno (e pode rebaixar para replay
  enquanto nenhum turno novo ocorrer).

## Preparação página a página (novos endpoints)

Fluxo do dispositivo (RAM limitada): fotografa → envia → libera → repete.

1. `POST /api/prepare/start` → `{"status":"started","upload_id":"<hex32>"}`.
   Limpa qualquer staging anterior (um upload ativo por vez).
2. `POST /api/prepare/page` — form: `upload_id`, `index` (0–19); file: `file`
   (JPEG/PNG, ≤10 MB) → `{"status":"stored","index":N,"pages_received":M}`.
   Mesmo `index` de novo ⇒ substitui (refazer foto). `upload_id` desconhecido ⇒ 404.
3. `POST /api/prepare/finish` — form: `upload_id` → mesma resposta do
   `/api/prepare` v1 (`ready`+`summary` | `illegible`+`illegible_pages` | 422).
   Índices devem ser contíguos 0..N-1 (senão `400` com os faltantes).
   Em `illegible` o staging é MANTIDO: reenviar só as páginas ilegíveis via
   `/api/prepare/page` e chamar `finish` de novo. Em `ready` o staging é limpo.

`POST /api/prepare` v1 (lote único) permanece intacto.

## Formato WAV (referência para o firmware)

PCM s16le, mono, 16000 Hz, header WAV canônico de 44 bytes (RIFF/fmt/data).
O firmware pode pular os 44 bytes e alimentar o I2S direto (com resample
16k→24k do codec da placa, já existente no XiaoZhi).

## Áudio de subida (dispositivo → backend)

Sem mudança de contrato: `/api/turn` repassa bytes+MIME ao Gemini. O
dispositivo envia **WAV 16 kHz mono** (`audio/wav`) — pendente de validação
empírica (abaixo). WebM/Opus NÃO é exigido.

## Validações empíricas pendentes (Task 11 do plano)

- [ ] Polly `generative` + `OutputFormat=pcm` + `SampleRate=16000` funciona com
      a voz Camila (contingência: engine `neural` para WAV, ou manter MP3 e o
      firmware habilita o decoder MP3 do esp_audio_codec).
- [ ] Gemini aceita `audio/wav` no turno multimodal com qualidade de avaliação
      equivalente ao WebM/Opus.

Resultados devem ser registrados NESTE documento ao concluir a Task 11.
````

- [ ] **Step 2: Commit (neste repo xiaozhi-esp32)**

```bash
cd /Users/institutorecriare/VSCodeProjects/xiaozhi-esp32
git add docs/professor-virtual/contrato-dispositivo.md
git commit -m "Adiciona contrato de dispositivo v1.1 (perfil aditivo do backend)"
```

### Task 2: Atualizar CLAUDE.md deste repo (regras do backend)

**Files:**
- Modify: `CLAUDE.md` (raiz deste repo, seção "Missão deste fork")

- [ ] **Step 1: Substituir o bullet do backend intocável**

Localizar exatamente este trecho:

```markdown
- O backend em `/Users/institutorecriare/VSCodeProjects/licao_casa/backend/` é **intocável**: fonte de
  verdade pedagógica. O firmware se adapta ao contrato HTTP vigente (seção 7 da
  especificação); nunca faça retry automático de `POST /api/turn`.
```

Substituir por:

```markdown
- Contrato do dispositivo: `docs/professor-virtual/contrato-dispositivo.md`
  (perfil v1.1 aditivo — mídia por URL, WAV, imagem redimensionada,
  idempotência, prepare paginado, token). O firmware implementa o cliente
  desse contrato.
- O backend em `/Users/institutorecriare/VSCodeProjects/licao_casa/backend/` tem duas zonas: o **miolo
  pedagógico é intocável** (prompts, vereditos, máquina de estados da sessão,
  failsafe — `gemini.py`, `session_engine.py` e as transições de estado de
  `main.py`); a **borda de transporte aceita mudanças aditivas** definidas no
  contrato do dispositivo, sempre preservando o comportamento v1 (frontend web
  e testes existentes intactos).
- Retry de `POST /api/turn`: proibido sem `request_id`; com `request_id`
  (contrato v1.1), reenviar o MESMO `request_id` após timeout é seguro — o
  backend devolve a resposta armazenada sem criar novo turno.
```

- [ ] **Step 2: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/xiaozhi-esp32
git add CLAUDE.md
git commit -m "CLAUDE.md: backend com zonas (miolo intocável, borda aditiva) e retry idempotente"
```

---

## Etapa B — repositório `licao_casa` (backend)

Todas as tasks abaixo rodam em `/Users/institutorecriare/VSCodeProjects/licao_casa/backend/` com o venv ativo:

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa/backend && source venv/bin/activate
```

### Task 3: Token de dispositivo (Settings + middleware)

**Files:**
- Modify: `backend/config.py` (classe `Settings`)
- Modify: `backend/main.py` (imports + middleware após `app = FastAPI(...)`)
- Test: `backend/tests/test_device_auth.py` (novo)

**Interfaces:**
- Consumes: `main.settings` (instância própria de `main.py:46`, monkeypatchável nos testes — mesmo padrão de `adult_pin`).
- Produces: header `Authorization: Bearer <token>` (ou `X-Api-Token`) exigido de clientes não-loopback quando `settings.device_api_token` estiver definido; `401` caso contrário.

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
    """Token guard for non-loopback clients (contrato v1.1)."""

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
    async def test_unset_token_allows_remote(self, monkeypatch):
        monkeypatch.setattr(main.settings, "device_api_token", "")
        async with _client(("192.168.0.42", 5555)) as ac:
            resp = await ac.get("/api/health")
        assert resp.status_code == 200
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `python -m pytest tests/test_device_auth.py -v`
Expected: FAIL — `AttributeError` (settings sem `device_api_token`) e/ou 200 onde se espera 401.

- [ ] **Step 3: Implementar**

Em `backend/config.py`, dentro de `Settings`, após `adult_pin: str = ""`:

```python
    device_api_token: str = ""
```

Em `backend/main.py`: adicionar aos imports do topo `import secrets` e, na linha do
fastapi, `Request`; adicionar `from fastapi.responses import JSONResponse`:

```python
from fastapi import FastAPI, UploadFile, File, Form, HTTPException, Request
from fastapi.responses import JSONResponse
```

Logo após `_state_lock = asyncio.Lock()` (linha ~97):

```python
# Hosts exempt from the device token: the web frontend reaches the backend via
# the Vite proxy on the same machine, so loopback keeps the v1 no-auth behavior.
_LOOPBACK_HOSTS = {"127.0.0.1", "::1", "localhost", None}


@app.middleware("http")
async def device_token_guard(request: Request, call_next):
    """Require the shared device token from non-loopback clients (contrato v1.1).

    Reads settings.device_api_token at request time (tests monkeypatch it,
    same pattern as adult_pin). Unset token == v1 behavior (no auth).
    """
    token = settings.device_api_token
    if token:
        client_host = request.client.host if request.client else None
        if client_host not in _LOOPBACK_HOSTS:
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

- [ ] **Step 4: Rodar os testes novos e a suíte inteira**

Run: `python -m pytest tests/test_device_auth.py -v && python -m pytest tests/ -q`
Expected: tudo PASS (o `client` do conftest usa loopback default do ASGITransport, então nada quebra).

- [ ] **Step 5: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add backend/config.py backend/main.py backend/tests/test_device_auth.py
git commit -m "Token de dispositivo: exige Bearer/X-Api-Token de clientes não-loopback"
```

### Task 4: Polly em WAV (PCM 16 kHz + header WAV)

**Files:**
- Modify: `backend/polly.py`
- Test: `backend/tests/test_polly.py` (APENAS adicionar testes; não editar os existentes)

**Interfaces:**
- Produces: `synthesize_speech(text, voice_id=None, engine=None, output_format="mp3") -> bytes`. `output_format="wav"` ⇒ bytes WAV (RIFF, 44 bytes de header + PCM s16le mono 16 kHz). Default ⇒ MP3 24 kHz (comportamento v1 intacto). Helper `_wav_from_pcm(pcm, sample_rate=16000) -> bytes`.

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

Em `backend/polly.py`, adicionar `import struct` ao topo e substituir `_synthesize`
e `synthesize_speech` por:

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

- [ ] **Step 4: Rodar testes**

Run: `python -m pytest tests/test_polly.py -v && python -m pytest tests/ -q`
Expected: tudo PASS.

- [ ] **Step 5: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add backend/polly.py backend/tests/test_polly.py
git commit -m "Polly: saída WAV opcional (PCM 16 kHz mono + header) para o dispositivo"
```

### Task 5: Redimensionamento de imagem (Pillow)

**Files:**
- Create: `backend/media_utils.py`
- Modify: `backend/requirements.txt`
- Test: `backend/tests/test_media_utils.py` (novo)

**Interfaces:**
- Produces: `resize_image_for_device(image_bytes: bytes, max_px: int) -> bytes` — sempre JPEG RGB q85, encaixa no quadrado `max_px`, nunca amplia. Síncrona (as Tasks 7 usam via `run_in_executor`).

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

### Task 6: Armazenamento de mídia do turno + `GET /api/media/{filename}`

**Files:**
- Create: `backend/media_store.py`
- Modify: `backend/main.py` (novo endpoint + imports)
- Test: `backend/tests/test_media_endpoint.py` (novo)

**Interfaces:**
- Produces: `save_turn_media(base_dir: Path, audio_bytes: bytes, image_bytes: bytes, audio_ext: str, image_ext: str) -> tuple[str, str]` (retorna URLs relativas `/api/media/...`; apaga mídia de turnos anteriores). Endpoint `GET /api/media/{filename}` servindo de `DATA_DIR / "media"`. A Task 7 consome ambos.

- [ ] **Step 1: Escrever os testes que falham**

Criar `backend/tests/test_media_endpoint.py`:

```python
import pytest
import pytest_asyncio

import main
from media_store import save_turn_media


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
    async def test_clears_previous_turn_files(self, data_dir):
        media = data_dir / "media"
        media.mkdir(parents=True)
        stale = media / ("turn_" + "0" * 32 + "_audio.mp3")
        stale.write_bytes(b"old")
        await save_turn_media(media, b"AAA", b"III", "mp3", "png")
        assert not stale.exists()
        assert len(list(media.iterdir())) == 2


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

Only the latest turn's files are kept on disk — exactly the files the stored
idempotent response (data/last_turn_response.json) may still reference.
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

    Clears files from previous turns first: a device downloads right after the
    turn, and the idempotent replay only ever references the latest turn.
    """
    media_dir.mkdir(parents=True, exist_ok=True)
    for old in media_dir.iterdir():
        old.unlink()
    turn_id = uuid.uuid4().hex
    audio_name = f"turn_{turn_id}_audio.{audio_ext}"
    image_name = f"turn_{turn_id}_image.{image_ext}"
    async with aiofiles.open(media_dir / audio_name, "wb") as f:
        await f.write(audio_bytes)
    async with aiofiles.open(media_dir / image_name, "wb") as f:
        await f.write(image_bytes)
    return f"/api/media/{audio_name}", f"/api/media/{image_name}"
```

- [ ] **Step 4: Implementar o endpoint**

Em `backend/main.py`: no import de `fastapi.responses`, incluir `FileResponse`
(`from fastapi.responses import FileResponse, JSONResponse`); adicionar
`from media_store import save_turn_media, MEDIA_CONTENT_TYPES` junto aos imports
locais. Após o endpoint `get_lesson` (linha ~149), adicionar:

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

- [ ] **Step 5: Rodar testes**

Run: `python -m pytest tests/test_media_endpoint.py -v && python -m pytest tests/ -q`
Expected: tudo PASS.

- [ ] **Step 6: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add backend/media_store.py backend/main.py backend/tests/test_media_endpoint.py
git commit -m "Mídia do turno em disco + GET /api/media/{filename} (contrato v1.1)"
```

### Task 7: Campos de dispositivo no `POST /api/turn` (`media`, `audio_format`, `image_max_px`)

**Files:**
- Modify: `backend/models.py` (`TutoringResponse`)
- Modify: `backend/main.py` (assinatura e corpo de `tutoring_turn`)
- Test: `backend/tests/test_turn_device.py` (novo)

**Interfaces:**
- Consumes: `synthesize_speech(..., output_format=)` (Task 4), `resize_image_for_device` (Task 5), `save_turn_media` (Task 6).
- Produces: resposta com `audio_url`, `image_url`, `audio_format` (`"mp3"|"wav"`), `image_format` (`"png"|"jpg"`), `request_id` (eco; a idempotência em si é a Task 8).

- [ ] **Step 1: Escrever os testes que falham**

Criar `backend/tests/test_turn_device.py`:

```python
import io
from unittest.mock import AsyncMock, patch

import pytest
from PIL import Image

from models import GeminiEvaluation


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
    async def test_v1_request_unchanged(
        self, client, setup_session, mock_polly, mock_image_gen
    ):
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            resp = await client.post(
                "/api/turn", data={"session_id": "s1"}, files=_turn_files()
            )
        assert resp.status_code == 200
        body = resp.json()
        assert body["audio_base64"] != ""
        assert body["image_base64"] != ""
        assert body["audio_url"] is None
        assert body["image_url"] is None
        assert body["audio_format"] == "mp3"
        assert body["image_format"] == "png"

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

(a) Imports: adicionar `from media_utils import resize_image_for_device` (o import de
`save_turn_media` veio na Task 6).

(b) Assinatura de `tutoring_turn` (linha ~259):

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

(c) Logo após a validação "exactly one input" (linha ~272-273), adicionar:

```python
    # Contrato v1.1: optional device-profile fields. Absent fields keep the
    # exact v1 path; invalid values fail fast before touching any state.
    if media not in (None, "url"):
        raise HTTPException(400, detail="media must be 'url' when provided")
    if audio_format not in (None, "mp3", "wav"):
        raise HTTPException(400, detail="audio_format must be 'mp3' or 'wav'")
    if image_max_px is not None and not (64 <= image_max_px <= 4096):
        raise HTTPException(400, detail="image_max_px must be between 64 and 4096")
    wants_wav = audio_format == "wav"
```

(d) No passo 10 (geração de mídia), substituir APENAS o bloco `try` (mantendo o
`except` CR-01 byte a byte como está):

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

(e) Substituir as duas linhas de encode base64 (linhas ~491-492) por:

```python
        if media == "url":
            audio_b64 = image_b64 = ""
        else:
            audio_b64 = base64.b64encode(audio_bytes).decode()
            image_b64 = base64.b64encode(image_bytes).decode()
```

(f) No passo 15, acrescentar os campos novos ao `TutoringResponse(...)` retornado
(manter os existentes intactos):

```python
        audio_url=audio_url,
        image_url=image_url,
        audio_format="wav" if wants_wav else "mp3",
        image_format=image_media_format,
        request_id=request_id,
```

- [ ] **Step 5: Rodar testes novos + suíte inteira**

Run: `python -m pytest tests/test_turn_device.py -v && python -m pytest tests/ -q`
Expected: tudo PASS (os testes v1 de `/api/turn` não enviam os campos novos e caem no caminho v1 exato).

- [ ] **Step 6: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add backend/models.py backend/main.py backend/tests/test_turn_device.py
git commit -m "/api/turn: perfil de dispositivo (media=url, audio_format=wav, image_max_px)"
```

### Task 8: Idempotência por `request_id`

**Files:**
- Modify: `backend/main.py` (handler `tutoring_turn`)
- Test: `backend/tests/test_turn_device.py` (adicionar classe)

**Interfaces:**
- Consumes: campo `request_id` e resposta estendida (Task 7).
- Produces: replay de `data/last_turn_response.json` para `request_id` repetido, sem chamadas a Gemini/Polly e sem mudança de estado.

- [ ] **Step 1: Escrever os testes que falham**

Adicionar ao final de `backend/tests/test_turn_device.py`:

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

        # Retry with the SAME request_id: Gemini must NOT be called again.
        must_not_run = AsyncMock(side_effect=AssertionError("evaluate_turn called on replay"))
        with patch("main.evaluate_turn", new=must_not_run):
            second = await client.post("/api/turn", data=data, files=_turn_files())
        assert second.status_code == 200
        assert second.json() == first.json()

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
        assert first.status_code == 200
        assert second.status_code == 200
        assert second.json()["request_id"] == "req-2"

    @pytest.mark.asyncio
    async def test_failed_turn_is_not_stored_for_replay(
        self, client, setup_session, mock_polly, mock_image_gen
    ):
        data = {"session_id": "s1", "request_id": "req-fail"}
        with patch("main.evaluate_turn", new=AsyncMock(side_effect=RuntimeError("boom"))):
            first = await client.post("/api/turn", data=data, files=_turn_files())
        assert first.status_code == 502
        # Same request_id after a failure must PROCESS (not replay a 502).
        with patch("main.evaluate_turn", new=AsyncMock(return_value=_teach_eval())):
            second = await client.post("/api/turn", data=data, files=_turn_files())
        assert second.status_code == 200
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `python -m pytest tests/test_turn_device.py -v`
Expected: FAIL — o replay chama `evaluate_turn` (AssertionError vira 500) no primeiro teste.

- [ ] **Step 3: Implementar**

Em `backend/main.py`, dentro de `tutoring_turn`:

(a) Primeira coisa DENTRO de `async with _state_lock:` (antes do passo 2 "Read
state.json"):

```python
        # Contrato v1.1: idempotent replay. Checked before any validation so a
        # client that lost the response still gets it even if the session has
        # since completed. Only successful (200) turns are ever stored.
        if request_id:
            stored = await read_json(DATA_DIR / "last_turn_response.json")
            if stored and stored.get("request_id") == request_id:
                return TutoringResponse.model_validate(stored["response"])
```

(b) No passo 15, capturar a resposta em vez de retornar direto, gravar e retornar
(substituir `return TutoringResponse(` por `response = TutoringResponse(` e, após o
fechamento do construtor, adicionar):

```python
    if request_id:
        # Written outside the lock: the device serializes turns and the replay
        # read happens under the lock — benign race accepted for the
        # single-user MVP.
        await write_json(DATA_DIR / "last_turn_response.json", {
            "request_id": request_id,
            "response": response.model_dump(),
        })
    return response
```

- [ ] **Step 4: Rodar testes novos + suíte inteira**

Run: `python -m pytest tests/test_turn_device.py -v && python -m pytest tests/ -q`
Expected: tudo PASS.

- [ ] **Step 5: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add backend/main.py backend/tests/test_turn_device.py
git commit -m "/api/turn: idempotência por request_id (replay da última resposta 200)"
```

### Task 9: Preparação página a página (`/api/prepare/start|page|finish`)

**Files:**
- Modify: `backend/main.py` (refatorar `prepare_lesson` extraindo `_run_prepare`; 3 endpoints novos)
- Test: `backend/tests/test_prepare_staged.py` (novo)

**Interfaces:**
- Consumes: pipeline v1 de `/api/prepare` (validações e construção de lesson/state/conversation).
- Produces: `_run_prepare(images: list[bytes], content_types: list[str]) -> dict` compartilhado; endpoints `POST /api/prepare/start`, `POST /api/prepare/page`, `POST /api/prepare/finish` conforme o contrato (Task 1).

- [ ] **Step 1: Escrever os testes que falham**

Criar `backend/tests/test_prepare_staged.py`:

```python
from unittest.mock import AsyncMock, patch

import pytest
import pytest_asyncio

import main
from models import LessonExtractionResponse, ExtractedItem, ExtractedTask


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
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `python -m pytest tests/test_prepare_staged.py -v`
Expected: FAIL — 404 (rotas inexistentes).

- [ ] **Step 3: Refatorar `prepare_lesson` extraindo `_run_prepare`**

Em `backend/main.py`, adicionar `import shutil` ao topo. Criar, imediatamente antes
de `prepare_lesson` (linha ~152), a função com o corpo MOVIDO (não copiado) do
endpoint atual — da gravação em `data/images` até o `return {"status": "ready", ...}`:

```python
async def _run_prepare(images: list[bytes], content_types: list[str]) -> dict:
    """Shared /api/prepare pipeline (v1 batch and v1.1 staged finish).

    Moved verbatim from prepare_lesson: saves pages to data/images, extracts
    via Gemini, and writes lesson_tasks.json + state.json + conversation.json.
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

    result = await extract_lesson(
        images=images,
        api_key=settings.google_api_key,
        model=settings.gemini_model,
    )

    if result.illegible_pages:
        return {"status": "illegible", "illegible_pages": result.illegible_pages}
    if not result.items:
        raise HTTPException(422, detail="Could not extract any tasks from the provided images")

    # (segue o corpo atual de prepare_lesson, inalterado: lesson_id, lesson_items,
    #  LessonTasksFile, SessionState, ConversationLog e o return {"status": "ready",
    #  "summary": lesson.model_dump()})
```

`prepare_lesson` vira: validações v1 atuais (contagem ≤20, MIME, ≤10 MB) coletando
`images` e `content_types = [f.content_type for f in files]`, e termina com
`return await _run_prepare(images, content_types)`.

- [ ] **Step 4: Implementar os endpoints staged**

Após `prepare_lesson`, adicionar:

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

- [ ] **Step 5: Rodar testes novos + suíte inteira (atenção a `test_prepare.py`)**

Run: `python -m pytest tests/test_prepare_staged.py tests/test_prepare.py -v && python -m pytest tests/ -q`
Expected: tudo PASS — a refatoração de `prepare_lesson` não pode alterar nenhum comportamento v1.

- [ ] **Step 6: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add backend/main.py backend/tests/test_prepare_staged.py
git commit -m "Preparação página a página: /api/prepare/start|page|finish (contrato v1.1)"
```

### Task 10: Documentos e configuração no `licao_casa`

**Files:**
- Create: `CONTRATO-DISPOSITIVO.md` (raiz do `licao_casa`)
- Modify: `.env.example` (raiz do `licao_casa`; apenas ADICIONAR linhas)
- Modify: `MIGRACAO-ESP32-P4.md` (apenas ADICIONAR seção ao final)

- [ ] **Step 1: Criar o ponteiro do contrato**

Criar `/Users/institutorecriare/VSCodeProjects/licao_casa/CONTRATO-DISPOSITIVO.md`:

```markdown
# Contrato de Dispositivo v1.1 (ponteiro)

O documento canônico vive no repositório do firmware:

`/Users/institutorecriare/VSCodeProjects/xiaozhi-esp32/docs/professor-virtual/contrato-dispositivo.md`

Resumo do que o backend implementa além da API v1 (tudo aditivo; frontend web
intacto): campos opcionais `request_id`/`media`/`audio_format`/`image_max_px`
no `POST /api/turn`; `GET /api/media/{filename}`; preparação página a página
(`/api/prepare/start|page|finish`); token de dispositivo (`DEVICE_API_TOKEN`)
para clientes não-loopback. Não edite o contrato aqui — edite o canônico.
```

- [ ] **Step 2: Adicionar variável ao `.env.example`**

Adicionar ao final de `/Users/institutorecriare/VSCodeProjects/licao_casa/.env.example`:

```
# Token exigido de clientes fora do loopback (dispositivo ESP32). Vazio = sem auth (v1).
DEVICE_API_TOKEN=
```

- [ ] **Step 3: Nota na doc de migração**

Adicionar ao final de `/Users/institutorecriare/VSCodeProjects/licao_casa/MIGRACAO-ESP32-P4.md`:

```markdown
## Adendo (jul/2026): contrato de dispositivo v1.1 implementado

As adaptações discutidas neste documento foram consolidadas no perfil v1.1 do
backend (ver `CONTRATO-DISPOSITIVO.md` na raiz): mídia por URL em vez de
base64, WAV/PCM 16 kHz opcional no lugar do MP3, imagem já redimensionada para
a tela, idempotência por `request_id`, preparação página a página e token de
dispositivo. O firmware consome esse perfil; o fluxo v1 do frontend web segue
inalterado.
```

- [ ] **Step 4: Commit**

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add CONTRATO-DISPOSITIVO.md .env.example MIGRACAO-ESP32-P4.md
git commit -m "Docs: ponteiro do contrato v1.1, DEVICE_API_TOKEN no .env.example, adendo na migração"
```

### Task 11: Validação com serviços reais (Polly PCM + Gemini WAV) e fechamento

**Files:**
- Create: `backend/scripts/check_polly_pcm.py`
- Modify: `docs/professor-virtual/contrato-dispositivo.md` (neste repo xiaozhi — marcar checkboxes de validação com o resultado)

Pré-requisito: `.env` do `licao_casa` com credenciais reais (AWS + Google). Estes passos chamam APIs pagas — volumes mínimos (1 frase TTS, 1 turno).

- [ ] **Step 1: Script de validação do Polly**

Criar `backend/scripts/check_polly_pcm.py`:

```python
"""Manual check: Polly generative voice + pcm 16k (contrato v1.1, Task 11).

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

- [ ] **Step 2: Rodar a validação do Polly**

Run: `python scripts/check_polly_pcm.py`
Expected: imprime `channels=1 rate=16000 bits=16 ...` e o WAV é audível.
**Contingência se o engine `generative` rejeitar `pcm`:** repetir com
`synthesize_speech(..., engine="neural", output_format="wav")` no script; se
funcionar, registrar no contrato que WAV usa engine `neural`; se nada de PCM
funcionar, registrar que o dispositivo usa MP3 (decoder `esp_audio_codec` no
firmware) e remover a opção `wav` do contrato — decisão documentada, não
silenciosa.

- [ ] **Step 3: Validação end-to-end com WAV no turno (Gemini)**

Com o backend rodando (`uvicorn main:app --reload`) e uma lição preparada
(fluxo normal do frontend web ou `POST /api/prepare` via curl com uma foto de
lição real):

```bash
# Gravar ~5 s de fala infantil de teste em WAV 16 kHz mono (macOS):
#   say -o /dev/null  # (ou gravar com QuickTime e converter)
#   afconvert entrada.m4a amostra.wav -d LEI16@16000 -c 1
curl -s -X POST http://127.0.0.1:8000/api/turn \
  -F session_id=validacao-v11 \
  -F 'audio=@amostra.wav;type=audio/wav' \
  -F request_id=validacao-0001 \
  -F media=url -F audio_format=wav -F image_max_px=1280 | python3 -m json.tool
```

Expected: 200 com `veredicto` coerente com a fala, `audio_url`/`image_url`
preenchidos; baixar ambos com `curl -O http://127.0.0.1:8000<url>` e conferir
(WAV audível; JPEG ≤1280 px).

- [ ] **Step 4: Registrar resultados no contrato**

Em `/Users/institutorecriare/VSCodeProjects/xiaozhi-esp32/docs/professor-virtual/contrato-dispositivo.md`,
marcar os dois checkboxes de "Validações empíricas pendentes" com o resultado
real (data, engine usado, observações de qualidade da avaliação do Gemini).

- [ ] **Step 5: Suíte completa + lint dos arquivos tocados**

```bash
python -m pytest tests/ -v
ruff check main.py models.py polly.py config.py media_store.py media_utils.py tests/test_device_auth.py tests/test_turn_device.py tests/test_media_endpoint.py tests/test_media_utils.py tests/test_prepare_staged.py
```
Expected: pytest 100% PASS; ruff sem erros novos nos arquivos listados.

- [ ] **Step 6: Smoke manual do frontend web (regressão v1)**

Subir backend + frontend (`npm run dev` em `frontend/`) e executar um turno de
áudio pelo navegador: resposta com áudio MP3 tocando e imagem exibida, como hoje.

- [ ] **Step 7: Commits finais**

```bash
cd /Users/institutorecriare/VSCodeProjects/licao_casa
git add backend/scripts/check_polly_pcm.py
git commit -m "Script de validação manual do Polly PCM 16 kHz"
cd /Users/institutorecriare/VSCodeProjects/xiaozhi-esp32
git add docs/professor-virtual/contrato-dispositivo.md
git commit -m "Contrato v1.1: registra resultados das validações Polly PCM e Gemini WAV"
```

---

## Revisão independente (obrigatória, após a Etapa B)

Conforme a prática deste projeto: ao terminar a implementação, solicitar uma
revisão independente via `mcp__codex-council__codex` (thread NOVA, distinta de
qualquer thread usada para decisões), passando o diff das mudanças do
`licao_casa` e o contrato. Corrigir findings relevantes e repetir a revisão no
máximo duas vezes.

## Fora do escopo deste plano

- Qualquer mudança no firmware (fases F0–F9 do `plano-firmware.md` consomem o
  contrato depois).
- Qualquer mudança no frontend web do `licao_casa`.
- Streaming/WebSocket, TLS, multiusuário, quotas de uso — decisões já
  descartadas ou adiadas (ver contrato, seção Princípios).
