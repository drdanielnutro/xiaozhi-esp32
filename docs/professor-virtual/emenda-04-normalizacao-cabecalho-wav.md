# Emenda 04 — Normalização do cabeçalho WAV do Fish Audio

**Data:** 31/07/2026

**Base:** `plano-contrato-dispositivo-consolidado.md`, commit `c9ca7c7`

**Natureza:** correção documental aditiva; nenhuma implementação executada

**Estado da execução no `licao_casa`:** parada antes da Task 4, no commit
`c2872c5`.

## 1. Precedência e alcance

Esta emenda prevalece somente sobre:

- D4;
- as instruções de WAV da Task 3.9 (preflight);
- as instruções de WAV da Task 4 (`fish_audio.py` e seus testes);
- as instruções de WAV da Task 11 (validação E2E).

Como consequência direta da decisão 16, a seção "Formatos de áudio" do contrato
canônico passa a declarar o WAV como arquivo finito. Nenhum outro trecho do
contrato é tocado; as chaves, os endpoints e a semântica JSON permanecem
idênticos.

Todo o restante permanece inalterado: D1, D2, D3, D5, autenticação,
idempotência, mídia por URL, preparação página a página, miolo pedagógico,
transições CR-01, contrato JSON e as demais tasks. Nenhuma outra task é
replanejada.

## 2. Fato observado

O preflight real do Fish Audio foi executado e **aprovado auditivamente** em
MP3 e WAV: voz Itachi correta, qualidade e ritmo satisfatórios.

O WAV recebido, porém, não é um arquivo finito:

| Item | Valor observado |
|---|---|
| Tamanho total do arquivo | 1.121.194 bytes |
| PCM real (após o cabeçalho) | 1.121.150 bytes |
| RIFF ChunkSize declarado | 4.294.967.076 (`0xFFFFFF24`) |
| `data` Subchunk2Size declarado | 4.294.967.040 (`0xFFFFFF00`) |

São placeholders de streaming. A implementação planejada da Task 4 devolveria
esses bytes sem normalização; o firmware, que deve confiar nos tamanhos
declarados no cabeçalho RIFF/WAVE, esperaria aproximadamente 4 GB de PCM.
Isso caracteriza bloqueio técnico D4 e é o objeto desta emenda.

## 3. Decisões fechadas

1. O backend continua aguardando a resposta HTTP completa do Fish Audio.
2. Para WAV, **antes de validar, persistir ou devolver** o áudio, o backend
   normaliza o cabeçalho para um arquivo finito.
3. O parser localiza os chunks RIFF **percorrendo a estrutura**. É proibido
   presumir que o chunk `data` começa no offset 36 ou que o PCM começa no byte
   44.
4. São aceitos apenas dois casos:
   - cabeçalho finito já coerente; ou
   - o par de placeholders de streaming em que RIFF ChunkSize **e** `data`
     Subchunk2Size são ambos maiores ou iguais a `0xFFFFFF00`.
5. Divergências de tamanho que não sejam o par reconhecido de placeholders são
   rejeitadas.
6. No caso de placeholders:
   - RIFF ChunkSize é reescrito como `len(audio) - 8`;
   - `data` Subchunk2Size é reescrito como a quantidade real de bytes depois do
     cabeçalho do chunk `data`;
   - ambos em uint32 little-endian.
7. São rejeitados: RIFF/WAVE truncado, chunk `data` ausente, PCM vazio, tamanho
   acima de uint32 e quantidade de PCM incompatível com amostras s16le.
8. Depois da normalização, o áudio é validado como PCM linear, mono, 16 bits e
   16 kHz.
9. A função interna do backend chama-se `_normalize_wav_header(data: bytes) -> bytes`.
10. `fish_audio.synthesize_speech` devolve o WAV normalizado, nunca os
    placeholders originais.
11. O preflight continua independente de `fish_audio.py`, mas aplica a mesma
    normalização localmente antes de gravar `fish_itachi_preflight.wav`.
12. O preflight valida também RIFF ChunkSize == tamanho final do arquivo − 8 e
    `data` Subchunk2Size == bytes reais de PCM, além da reprodução e do layout
    já exigidos.
13. O preflight informa se houve normalização e os tamanhos declarados antes e
    depois, sem registrar chave nem texto integral.
14. A Task 4 escreve testes explícitos para os seis casos da seção 6.
15. A Task 11 confere os tamanhos RIFF/`data` no WAV real baixado do backend.
16. O contrato público declara que o WAV entregue pelo backend é um arquivo
    finito: RIFF corresponde ao tamanho físico e `data` corresponde ao PCM
    efetivamente entregue. Placeholders de streaming não chegam ao dispositivo.
17. A validação empírica permanece **pendente**: a audição passou, mas o
    preflight só é considerado concluído depois de reexecutado com
    normalização.
18. Esta emenda prevalece somente sobre D4 e sobre as instruções WAV das Tasks
    3.9, 4 e 11 (seção 1). Todo o restante permanece inalterado.
19. Voz, modelo e formatos permanecem: Itachi, `reference_id`
    `c5a6cb585b094dedb241365e7e271973`, modelo `s2.1-pro-free`, MP3 44,1 kHz/
    128 kbps e WAV PCM s16le mono/16 kHz.
20. Não são introduzidos streaming, WebSocket, fallback, retry, alteração
    pedagógica nem mudança no contrato JSON.

## 4. Consequências explícitas do parser

- O PCM entregue é tudo o que existe depois do cabeçalho do chunk `data` até o
  fim do arquivo. Um arquivo com chunks **depois** do `data` produziria
  `Subchunk2Size != PCM real` e cai na regra 5 (rejeição), porque não é o par
  de placeholders reconhecido. Isso é intencional e fail-closed.
- Chunks **antes** do `data` (`LIST`, `fact`, etc.) são suportados: o parser
  os percorre respeitando o byte de padding de alinhamento par.
- Um cabeçalho finito já coerente é devolvido **byte a byte igual**; a
  normalização nunca reescreve o que já está correto.

## 5. Código final

### 5.1 `backend/fish_audio.py` (Task 4)

```python
_WAV_PLACEHOLDER_MIN = 0xFFFFFF00
_UINT32_MAX = 0xFFFFFFFF


def _find_wav_data_chunk(data: bytes) -> int:
    """Return the data chunk payload offset by walking the RIFF chunk list."""
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise FishAudioError("Fish Audio returned an invalid WAV")
    offset = 12
    while offset + 8 <= len(data):
        chunk_id = data[offset : offset + 4]
        chunk_size = int.from_bytes(data[offset + 4 : offset + 8], "little")
        payload = offset + 8
        if chunk_id == b"data":
            return payload
        if chunk_size > len(data) - payload:
            raise FishAudioError("Fish Audio returned a truncated WAV")
        offset = payload + chunk_size + (chunk_size % 2)
    raise FishAudioError("Fish Audio returned a WAV without a data chunk")


def _normalize_wav_header(data: bytes) -> bytes:
    """Return a WAV whose RIFF/data sizes describe a finite file.

    Fish Audio may answer with the streaming placeholders observed in the
    preflight (RIFF 0xFFFFFF24 and data 0xFFFFFF00). A coherent finite header
    is returned unchanged, the recognized placeholder pair is rewritten with
    the real sizes and any other mismatch is rejected.
    """
    payload = _find_wav_data_chunk(data)
    if len(data) - 8 > _UINT32_MAX:
        raise FishAudioError("Fish Audio returned an oversized WAV")

    riff_size = int.from_bytes(data[4:8], "little")
    data_size = int.from_bytes(data[payload - 4 : payload], "little")
    pcm_bytes = len(data) - payload
    if pcm_bytes == 0:
        raise FishAudioError("Fish Audio returned a WAV without PCM data")
    if pcm_bytes % 2:
        raise FishAudioError("Fish Audio returned a WAV with a partial s16le sample")

    expected_riff = len(data) - 8
    if riff_size == expected_riff and data_size == pcm_bytes:
        return data
    if riff_size < _WAV_PLACEHOLDER_MIN or data_size < _WAV_PLACEHOLDER_MIN:
        raise FishAudioError("Fish Audio returned inconsistent WAV sizes")

    normalized = bytearray(data)
    normalized[4:8] = expected_riff.to_bytes(4, "little")
    normalized[payload - 4 : payload] = pcm_bytes.to_bytes(4, "little")
    return bytes(normalized)


def _validate_wav(data: bytes) -> None:
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise FishAudioError("Fish Audio returned an invalid WAV")
    try:
        with wave.open(io.BytesIO(data), "rb") as wav_file:
            values = (
                wav_file.getnchannels(),
                wav_file.getframerate(),
                wav_file.getsampwidth(),
                wav_file.getcomptype(),
            )
    except (EOFError, wave.Error) as exc:
        raise FishAudioError("Fish Audio returned an invalid WAV") from exc
    if values != (1, 16000, 2, "NONE"):
        raise FishAudioError("Fish Audio returned an unexpected WAV layout")
```

Em `synthesize_speech`, o trecho final passa a ser:

```python
    audio = bytes(response.content)
    if output_format == "wav":
        audio = _normalize_wav_header(audio)
        _validate_wav(audio)
    return audio
```

A ordem é obrigatória: normalizar, validar, devolver. `_validate_wav` sozinho
não detecta os placeholders porque o módulo `wave` só lê o tamanho declarado do
chunk `data` quando frames são efetivamente lidos.

### 5.2 `backend/scripts/check_fish_tts.py` (Task 3.9)

O preflight duplica deliberadamente a lógica para continuar standalone (não
importa `fish_audio.py`), com mensagens em português e `RuntimeError`:

```python
_WAV_PLACEHOLDER_MIN = 0xFFFFFF00
_UINT32_MAX = 0xFFFFFFFF


def _find_wav_data_chunk(data: bytes) -> int:
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise RuntimeError("WAV sem RIFF/WAVE válido")
    offset = 12
    while offset + 8 <= len(data):
        chunk_id = data[offset : offset + 4]
        chunk_size = int.from_bytes(data[offset + 4 : offset + 8], "little")
        payload = offset + 8
        if chunk_id == b"data":
            return payload
        if chunk_size > len(data) - payload:
            raise RuntimeError("WAV truncado antes do chunk data")
        offset = payload + chunk_size + (chunk_size % 2)
    raise RuntimeError("WAV sem chunk data")


def _declared_sizes(data: bytes) -> tuple[int, int, int]:
    """Return (RIFF ChunkSize, data Subchunk2Size, PCM bytes reais)."""
    payload = _find_wav_data_chunk(data)
    return (
        int.from_bytes(data[4:8], "little"),
        int.from_bytes(data[payload - 4 : payload], "little"),
        len(data) - payload,
    )


def _normalize_wav_header(data: bytes) -> bytes:
    payload = _find_wav_data_chunk(data)
    if len(data) - 8 > _UINT32_MAX:
        raise RuntimeError("WAV acima do limite de uint32")

    riff_size, data_size, pcm_bytes = _declared_sizes(data)
    if pcm_bytes == 0:
        raise RuntimeError("WAV sem PCM")
    if pcm_bytes % 2:
        raise RuntimeError("WAV com amostra s16le incompleta")

    expected_riff = len(data) - 8
    if riff_size == expected_riff and data_size == pcm_bytes:
        return data
    if riff_size < _WAV_PLACEHOLDER_MIN or data_size < _WAV_PLACEHOLDER_MIN:
        raise RuntimeError(
            f"WAV com tamanhos inconsistentes: riff={riff_size} data={data_size}"
        )

    normalized = bytearray(data)
    normalized[4:8] = expected_riff.to_bytes(4, "little")
    normalized[payload - 4 : payload] = pcm_bytes.to_bytes(4, "little")
    return bytes(normalized)


def _validate_finite_sizes(data: bytes) -> None:
    riff_size, data_size, pcm_bytes = _declared_sizes(data)
    if riff_size != len(data) - 8:
        raise RuntimeError(
            f"RIFF ChunkSize {riff_size} != tamanho do arquivo - 8 ({len(data) - 8})"
        )
    if data_size != pcm_bytes:
        raise RuntimeError(f"data Subchunk2Size {data_size} != PCM real ({pcm_bytes})")
```

O laço de `main()` passa a normalizar antes de gravar e a relatar os tamanhos:

```python
            data, elapsed = _synthesize(client, output_format)
            note = ""
            if output_format == "wav":
                before = _declared_sizes(data)
                data = _normalize_wav_header(data)
                after = _declared_sizes(data)
                _validate_finite_sizes(data)
                _validate_wav(data)
                note = (
                    f" normalizado={'sim' if after[:2] != before[:2] else 'nao'}"
                    f" riff={before[0]}->{after[0]}"
                    f" data={before[1]}->{after[1]} pcm={after[2]}"
                )
            else:
                _validate_mp3(data)
```

O relatório contém apenas formato, modelo, voz, tempos, caminho e tamanhos.
Nunca a chave nem o texto sintetizado.

## 6. Testes obrigatórios da Task 4

Adicionar a `backend/tests/test_fish_audio.py`, sem editar os 152 testes
existentes:

1. **WAV finito correto permanece byte a byte igual** — `_normalize_wav_header`
   devolve exatamente o mesmo objeto/bytes de um WAV coerente.
2. **Placeholders observados no Fish são corrigidos** — entrada com RIFF
   `4294967076` e `data` `4294967040` produz `len(audio) - 8` e o PCM real, em
   uint32 little-endian, preservando todo o resto do arquivo.
3. **Chunk opcional antes de `data`** — um `LIST` antes do `data` prova que não
   existe offset fixo: a reescrita acontece na posição correta e o offset 36/44
   não é usado.
4. **Divergência não-placeholder é rejeitada** — RIFF ou `data` incoerentes,
   mas abaixo de `0xFFFFFF00`, levantam `FishAudioError`.
5. **RIFF/`data` ausente ou truncado é rejeitado** — arquivo curto demais, sem
   `RIFF`/`WAVE`, sem chunk `data`, com PCM vazio e com número ímpar de bytes
   de PCM levantam `FishAudioError`.
6. **`synthesize_speech` devolve os tamanhos finitos corretos** — com o
   transporte simulado devolvendo o WAV de placeholders, o retorno declara
   `len(audio) - 8` e o PCM real, e nunca os valores originais.

## 7. Task 11

Além do já previsto, conferir no WAV real baixado do backend que
RIFF ChunkSize == tamanho do arquivo − 8 e que `data` Subchunk2Size == bytes
reais de PCM, percorrendo os chunks (sem offset fixo).

## 8. Contrato público

A seção "Formatos de áudio (referência para o firmware)" passa a declarar que o
WAV entregue pelo backend é um arquivo finito e que placeholders de streaming
não chegam ao dispositivo. A exigência de interpretar a estrutura RIFF/WAVE
(sem presumir 44 bytes) permanece.

## 9. Estado da validação empírica

A audição humana de MP3 e WAV foi aprovada em 31/07/2026, mas o preflight
**não** está concluído: os checkboxes de "Validações empíricas pendentes" do
contrato permanecem desmarcados até que o preflight seja reexecutado com a
normalização desta emenda e produza um WAV finito.

## 10. Fora do escopo

- Streaming, WebSocket, fallback, retry e cache de áudio;
- alteração pedagógica ou de prompts;
- mudança no contrato JSON, nas chaves da resposta ou na idempotência;
- reescrita de qualquer campo do WAV além de RIFF ChunkSize e `data`
  Subchunk2Size;
- suporte a chunks posteriores ao chunk `data`;
- replanejamento de qualquer outra task.
