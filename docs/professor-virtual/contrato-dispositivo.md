# Contrato de Dispositivo — Perfil v1.1 (aditivo sobre a API v1)

**Canônico neste arquivo.** Cópia-ponteiro em `licao_casa/CONTRATO-DISPOSITIVO.md`.
Complementa a seção 7 de `DOCUMENTACAO-APP.md`; em conflito, este documento vence
para os campos/endpoints v1.1.

## Princípios

1. **Aditivo:** nada da API v1 muda. Campos novos são opcionais; endpoints novos
   têm rotas novas. O frontend web continua funcionando sem alteração.
2. **Cliente burro:** o dispositivo continua sem decidir nada pedagógico
   (princípio da seção 2 da especificação).
3. **Retry de turno:** retry de `POST /api/turn` continua PROIBIDO sem
   `request_id`. Com cache íntegro e cliente serial conforme este contrato,
   uma retransmissão do mesmo turno pode processar quando a falha anterior
   ocorreu antes do marcador `processing`, devolver replay 200 quando existe
   resposta `done` válida e replayável, ou devolver 409 quando o resultado é
   indeterminado, supersedido ou não replayável. A garantia é não haver dupla
   aplicação silenciosa dentro dessas premissas. Status HTTP isolado, inclusive
   502, não autoriza retry automático. Após 409, o dispositivo descarta todos
   os ids pendentes anteriores, re-hidrata `GET /api/state` +
   `GET /api/lesson` e só então inicia um turno lógico novo com UUID novo.

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

## `POST /api/turn` — campos opcionais novos (multipart form)

| Campo | Tipo | Valores | Efeito |
|---|---|---|---|
| `request_id` | str | 1–128 caracteres, não branco; único durante toda a instalação (UUID v4 recomendado) | chave de idempotência (ver abaixo) |
| `media` | str | `url` | resposta traz `audio_url`/`image_url`; `audio_base64`/`image_base64` vêm vazios (`""`) |
| `audio_format` | str | `mp3` (default) \| `wav` | `mp3` ⇒ mono 44,1 kHz, 128 kbps; `wav` ⇒ PCM s16le mono 16 kHz com header RIFF/WAVE |
| `image_max_px` | int | 64–4096 | imagem reduzida para caber em `image_max_px` no lado maior (nunca amplia) e reencodada **JPEG** q85 |

O cliente gera um `request_id` novo para cada turno lógico novo e jamais
reutiliza um id para outro conteúdo, outra lição ou outro momento da
instalação. O mesmo id só pode reaparecer com os mesmos campos e bytes de
mídia do turno original quando a resposta se perdeu. O backend rejeita
`request_id` composto apenas por espaços ou com mais de 128 caracteres com
400. Por normalização da stack de formulários, valor vazio
(`request_id=`) equivale a campo omitido: não ativa idempotência nem o
perfil v1.1.

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
`png`|`jpg` (`jpg` quando `image_max_px` foi usado). Requisições **sem**
nenhum campo v1.1 recebem **exatamente** o payload v1 atual (mesmo conjunto de
chaves; nenhuma chave nova). Os campos `audio_url`, `image_url`,
`audio_format`, `image_format` e `request_id` aparecem na resposta **somente**
quando a requisição envia ao menos um campo do perfil v1.1 (`request_id`,
`media`, `audio_format`, `image_max_px`).

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

## `GET /api/media/{filename}` (novo)

- Serve os arquivos do último turno gerado com `media=url`.
- `filename` validado por regex estrita (`turn_<hex32>_(audio|image).<ext>`);
  qualquer outro nome ⇒ 404. Content-Type por extensão
  (mp3⇒audio/mpeg, wav⇒audio/wav, png⇒image/png, jpg⇒image/jpeg).
- A mídia nova é escrita SEM remover a anterior; a remoção da mídia não
  referenciada acontece apenas DEPOIS da consolidação da nova resposta
  idempotente (ou da supersessão), dentro da mesma fronteira protegida do
  turno, em limpeza best-effort (órfãos tolerados; mídia ainda referenciada
  por resposta replayável nunca é apagada).
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

## Formatos de áudio (referência para o firmware)

- WAV: PCM s16le, mono, 16000 Hz, com header RIFF/WAVE.
- MP3: mono, 44100 Hz, 128 kbps.

O WAV entregue pelo backend é sempre um **arquivo finito**: o RIFF ChunkSize
corresponde ao tamanho físico do arquivo menos 8 bytes e o `data` Subchunk2Size
corresponde ao PCM efetivamente entregue. Placeholders de streaming (valores
próximos de `0xFFFFFFFF`) são normalizados no backend e **não chegam ao
dispositivo**. O firmware pode confiar nos tamanhos declarados no cabeçalho.

O firmware deve interpretar a estrutura RIFF/WAVE; não deve presumir que o
payload PCM sempre começa em um offset fixo de 44 bytes.

## Áudio de subida (dispositivo → backend)

Sem mudança de contrato: `/api/turn` repassa bytes+MIME ao Gemini. O
dispositivo envia **WAV 16 kHz mono** (`audio/wav`) — pendente de validação
empírica (abaixo). WebM/Opus NÃO é exigido.

## Validações empíricas pendentes (Task 11 do plano)

- [x] Fish Audio, usando o modelo configurado e a voz Itachi
      (`c5a6cb585b094dedb241365e7e271973`), produz MP3 mono 44,1 kHz/128 kbps
      e WAV PCM s16le mono/16 kHz com qualidade aprovada pelo proprietário.
      Registrar data, modelo efetivo, voz, formatos, tempos observados no MP3
      e no WAV, tamanhos declarados no cabeçalho WAV e resultado da audição
      humana.
      **Concluído em 2026-07-31.**
      - Data: 2026-07-31
      - Endpoint: `https://api.fish.audio/v1/tts`
      - Modelo: `s2.1-pro-free`
      - Voice id: `c5a6cb585b094dedb241365e7e271973` (Itachi)
      - Formatos: MP3 44,1 kHz/128 kbps; WAV PCM s16le mono/16 kHz
      - Parâmetros: `latency=normal`, `speed=1.0`, `volume=0`,
        `normalize=true`, `normalize_loudness=true`, `timeout=120s`
      - Tempos: MP3 10,08 s; WAV 8,62 s
      - Antes da normalização: RIFF `ChunkSize` 4294967076; `data`
        `Subchunk2Size` 4294967040
      - Depois da normalização: arquivo 1.273.518 bytes; RIFF `ChunkSize`
        1273510; `data` `Subchunk2Size` 1273474; PCM real 1273474 bytes;
        duração 39,80 s
      - Aprovação humana: aprovada em 2026-07-31; voz Itachi correta;
        qualidade e ritmo satisfatórios em MP3 e WAV
      - Resultado técnico: placeholders removidos; RIFF = tamanho físico − 8;
        `data` = PCM real
- [ ] Gemini aceita `audio/wav` no turno multimodal com qualidade de avaliação
      equivalente ao WebM/Opus.

Resultados devem ser registrados NESTE documento ao concluir a Task 11.
