# Emenda 03 — Migração da Polly para Fish Audio na v1.1

**Data:** 30/07/2026

**Base:** `plano-contrato-dispositivo-consolidado.md`, commit `cd2e287`

**Natureza:** correção documental aditiva; nenhuma implementação executada

**Referências oficiais verificadas em 30/07/2026:**

- <https://docs.fish.audio/api-reference/endpoint/openapi-v1/text-to-speech>
- <https://fish.audio/pt/blog/s2-1-pro-free-api/>

## 1. Precedência e alcance

Esta emenda prevalece somente sobre:

- D4;
- Task 3.9;
- Task 4;
- configuração de TTS adicionada à Task 3;
- fixture/nomenclatura de TTS planejada nas Tasks 7 e 8;
- documentação/configuração da Task 10;
- validações reais da Task 11;
- referências ao provedor na Task 12 e no bloco “Fora do escopo”.

D1–D3, D5, autenticação, idempotência, mídia, preparação página a página,
miolo pedagógico, transições CR-01 e contrato público permanecem inalterados.

## 2. Decisões fechadas

1. Na v1.1, Fish Audio é o único provedor TTS ativo tanto para o frontend web
   quanto para o perfil de dispositivo.
2. A V1 exata com Polly permanece recuperável pela tag `baseline-v1.0`.
3. O contrato v1 continua com as mesmas chaves e semântica; a mudança
   intencional é o provedor/voz e, consequentemente, os bytes de
   `audio_base64`.
4. Voz padrão: **Itachi**, `reference_id`
   `c5a6cb585b094dedb241365e7e271973`.
5. Modelo inicial configurável: `s2.1-pro-free`.
6. Integração via `POST https://api.fish.audio/v1/tts`, aguardando a resposta
   completa. WebSocket, streaming e desacoplamento áudio/imagem ficam fora.
7. Frontend web: MP3 mono, 44,1 kHz, 128 kbps.
8. Dispositivo: WAV PCM s16le, mono, 16 kHz.
9. Não existe retry automático nem fallback silencioso para Polly.
10. O texto pedagógico é enviado sem tags emocionais automáticas. A versão
    “plain” aprovada é a referência.
11. `FISH_API_KEY` é segredo local: nunca entra em documento, commit, corpo de
    resposta ou log.

## 3. Configuração

Adicionar a `Settings` na Task 3:

```python
    fish_api_key: str = ""
    fish_voice_id: str = "c5a6cb585b094dedb241365e7e271973"
    fish_tts_model: str = "s2.1-pro-free"
    fish_tts_api_url: str = "https://api.fish.audio/v1/tts"
    fish_tts_timeout_seconds: float = Field(default=120.0, gt=0)
    fish_tts_latency: str = "normal"
    fish_tts_speed: float = Field(default=1.0, gt=0)
```

Na Task 10, documentar as mesmas variáveis em `.env.example`, deixando
`FISH_API_KEY=` vazio e explicando que o modelo gratuito é provisório e sem
SLA. Não remover as variáveis AWS porque `polly.py` e seus testes permanecem
como legado inativo da V1.

## 4. Task 3.9 substituta — preflight Fish

Substituir integralmente o preflight Polly por
`backend/scripts/check_fish_tts.py`, standalone em relação à Task 4.

O script:

- usa `httpx.Client` diretamente;
- lê configuração via `settings`;
- exige `FISH_API_KEY` local;
- faz exatamente duas chamadas com o mesmo texto mentor “plain” aprovado em
  `licao_casa/scripts/fish_voice_mentor_ab_plain.txt`, incorporado
  literalmente ao script para mantê-lo standalone:
  MP3 44,1 kHz/128 kbps e WAV 16 kHz;
- usa o modelo e a voz configurados;
- mede cada chamada com `time.perf_counter`;
- grava os dois arquivos em `tempfile.gettempdir()`;
- rejeita resposta vazia ou JSON;
- valida MP3 por assinatura ID3/frame sync;
- valida WAV RIFF/WAVE, mono, 16 bits e 16 kHz com `wave`;
- pausa para audição humana;
- interrompe antes da Task 4 se formato, voz ou qualidade falhar;
- nunca chama Polly.

O resultado real registrado no contrato deve conter data, endpoint, modelo,
voice id, formato, parâmetros, tempos observados e aprovação humana. A chave
jamais é registrada.

## 5. Task 4 substituta — cliente Fish Audio

Criar `backend/fish_audio.py` e `backend/tests/test_fish_audio.py`. Alterar em
`main.py` somente o import do TTS:

```python
from fish_audio import synthesize_speech
```

Interface:

```python
async def synthesize_speech(
    text: str,
    output_format: Literal["mp3", "wav"] = "mp3",
    voice_id: str | None = None,
    model: str | None = None,
) -> bytes
```

Comportamento:

- `httpx.AsyncClient`, sem SDK adicional;
- Bearer token e header `model`;
- `reference_id` Itachi por default;
- MP3: `format=mp3`, `sample_rate=44100`, `mp3_bitrate=128`;
- WAV: `format=wav`, `sample_rate=16000`;
- `latency=normal`, `normalize=true`, prosódia neutra;
- texto enviado byte semanticamente igual ao `texto_explicacao`, sem tags;
- resposta completa em bytes;
- WAV validado antes de retornar;
- erros sanitizados, sem chave, texto integral ou corpo remoto;
- zero retry e zero fallback;
- CR-01 existente continua responsável pelo 502 e por descartar transições.

`polly.py`, `test_polly.py`, fixture `mock_polly` atual e boto3 não são
editados ou removidos; ficam como legado inativo e coberto pelos testes
existentes. A remoção posterior exige milestone próprio.

## 6. Ajustes mecânicos nas Tasks 7, 8, 10, 11 e 12

- No novo `test_turn_device.py`, criar fixture local `mock_tts` que substitui
  `main.synthesize_speech`, retorna MP3 para o caminho default e WAV RIFF
  válido para `output_format="wav"`.
- Trocar todas as referências planejadas `mock_polly` por `mock_tts` e
  “Polly/image call” por “TTS/image call”.
- Na Task 7, atualizar somente a terminologia dos três comentários/docstrings
  legados de `main.py` (“Polly MP3”, “Polly TTS + Nano Banana 2 image” e
  “Polly/image”) para termos neutros de TTS, sem alterar lógica.
- Preservar a chamada
  `synthesize_speech(text, output_format="wav" if wants_wav else "mp3")`
  dentro do mesmo `asyncio.gather` com a imagem.
- Salvar debug com extensão correspondente ao formato real.
- Em Task 11, validar e ouvir Fish MP3 no frontend e Fish WAV no perfil do
  dispositivo, além da entrada Gemini WAV.
- Em Task 12, manter o firmware neutro ao provedor; registrar Fish somente
  como implementação empírica do backend.

## 7. Falhas e segurança

Ausência de chave, timeout, falha de transporte, HTTP não-2xx, resposta vazia
ou WAV inválido geram exceção `FishAudioError` sanitizada. O handler existente
de mídia converte a falha em 502 e preserva CR-01: nenhuma transição pedagógica,
contador de uso/sucesso ou append do diário é persistido. O
`technical_failure_count` da tentativa e o failsafe técnico que ele pode
acionar continuam persistidos exatamente como na V1.

Não há retry automático para 401, 402, 422, 429 ou 5xx. O corpo remoto não é
propagado. Segredos não aparecem em `repr`, logs, debug ou respostas.

## 8. Testes obrigatórios

Adicionar testes sem editar os 152 existentes:

- payload, headers, modelo e voice id;
- texto plain sem tags;
- MP3 44,1 kHz/128 kbps;
- WAV RIFF/WAVE mono/16-bit/16 kHz;
- formato inválido;
- chave ausente;
- timeout e falha de transporte;
- 401, 402, 422, 429 e 5xx;
- resposta vazia e WAV inválido;
- erro nunca contém chave, texto ou corpo remoto;
- v1 mantém exatamente as chaves atuais e entrega MP3;
- perfil v1.1 entrega WAV por Base64/URL;
- falha Fish resulta em 502 sem transição pedagógica, alteração dos contadores
  de uso/sucesso ou append no diário; o incremento de falha técnica por
  tentativa previsto na CR-01 permanece;
- replay idempotente não chama TTS novamente;
- nenhuma chamada runtime à Polly;
- áudio e imagem permanecem no mesmo `asyncio.gather`.

Rodar `python -m pytest tests/ -q` ao fim de cada task.

## 9. Gates e condições de parada

Antes da Task 3 e de qualquer código da Parte 3:

1. o diff documental desta emenda e do plano consolidado recebe revisão
   independente. Findings bloqueantes são corrigidos documentalmente antes de
   continuar.

Depois da Task 3 e antes da Task 4:

1. o proprietário configura `FISH_API_KEY` apenas no `.env` local;
2. executa o preflight MP3+WAV;
3. ouve ambos os arquivos;
4. aprova explicitamente voz e qualidade;
5. registra o resultado no contrato canônico por handoff separado no
   `xiaozhi-esp32`.

Qualquer reprovação interrompe a execução e exige nova emenda. A oferta
`s2.1-pro-free` está sujeita a mudança e não possui SLA; modelo e endpoint
configuráveis permitem migração sem alterar o contrato.

## 10. Fora do escopo

- WebSocket e streaming de TTS;
- streaming do Gemini;
- desacoplamento de áudio e imagem;
- cache de áudio;
- tags emocionais automáticas;
- remoção de Polly/boto3 legado;
- alteração de prompts ou regras pedagógicas.
