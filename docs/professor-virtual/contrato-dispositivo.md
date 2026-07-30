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
