# Plano — Rodada 15: escada UVC com o driver consertado (usb_host_uvc 2.5.1)

> Roteiro TÁTICO da rodada 15, subordinado ao plano-mestre
> `plano_codex_v2.md`. Revisado 2×2 pelo Codex; esta versão incorpora todas
> as correções (P1 do commit global, P1 do push, P2 do rollback, P2 do
> worktree, P2 do BSP).

## Contexto

A T5 da F2B travou num mistério: a câmera NE-HD362 enumera perfeitamente no
ESP32-P4, mas o canal de vídeo ISOC fica mudo na maioria das partidas. Dois
pareceres independentes de Codex convergiram: o plano v2 do proprietário
(`docs/professor-virtual/plano_codex_v2.md`) identificou **bug documentado no
`usb_host_uvc` 2.4.2** — cálculo de URB errado que força transferências
isócronas de 1 pacote — corrigido na **2.5.1** (verificado por mim no
changelog oficial); o Codex do diagnóstico (thread 019ff72c) apostou, sem
conhecer o bug, exatamente nessa camada ("pipe ISOC preso; 4 URBs de 1
microframe") e entregou o **método de bancada** (partida fria limpa, máx. 2
tentativas, gates de encerramento).

Este plano executa a **cabeça do plano v2 com a disciplina do tático**:
testar a escada com a pilha corrigida, classificar factualmente, encerrar a
sessão com commit de evidência e worktree limpo. Decisão já tomada pelo
proprietário: NÃO flashar o controle `b8ed27e` (ordem do tático superada
pela descoberta do bug).

## Estado já pronto (nada a construir)

- **Binário staged**: `releases/uvc-spike/merged-binary.bin`
  (sha256 `1e2a988201b600b9…`), compilado com `esp_video 2.3.0` +
  `usb_host_uvc 2.5.1` (confirmado no `dependencies.lock`) sobre o firmware
  do laço (commit `1c02248`: escada por ciclo, janela de 30 s/degrau, fps
  default). Compilou **sem** adaptação do nosso código.
- **Mudança experimental de worktree** (não será consolidada nesta rodada):
  `main/idf_component.yml` (esp_video ^2.3.0 + usb_host_uvc ~2.5.1 + BSP
  `esp32_p4_function_ev_board` comentado por conflito de versão).
- Porta serial livre; captura sem-reset (`pv_capture_noreset.py`, tolera a
  porta sumir e voltar) e monitor prontos no scratchpad.

## Execução — passo a passo

### Etapa 1 — Flash (gestos do proprietário)

1. **Desplugar a câmera** da USB-A (deixar fora).
2. Digitar: `! zsh releases/flash-spike.sh`
3. Esperar `Hash of data verified` (~2 min; a placa fica em bootloader).

### Etapa 2 — Partida fria 1 (proprietário; protocolo do tático)

1. **Desconectar o USB-C** ("USB TO UART") da placa → tudo morre junto.
2. Esperar **10 segundos**.
3. **Conectar a câmera** na USB-A com a placa ainda morta.
4. **Reconectar o USB-C** → boot automático, nascimento conjunto.
5. **Não apertar RST; não replugar nada** durante o teste.

(Antes da etapa 2 eu abro a captura run15 — ela espera a porta reaparecer
sozinha — e armo o monitor de degraus/dumps/crashes.)

### Etapa 3 — Observação (minha, automática)

Sinais a medir no log, por tentativa:
- Linha `Allocating N USB transfers ... M ISOC packets` — **expectativa da
  correção: M > 1** (era sempre 1 com o driver bugado).
- Contagens: atividade do canal (`frame error`/underflow são visíveis em W),
  `PV-UVC-RUNG` por degrau, `PV-JPEG-BEGIN`/dumps.
- Critérios de parada da tentativa: resultado do degrau 800×600 e, se PASS,
  fim dos dumps da escada; **ou** 4 min sem progresso; **ou** crash/boot-loop.

### Etapa 4 — Classificação factual (sem hipótese nova)

- **PASS em 1920×1080 E ≥1 degrau ≥2592×1944** → objetivo mínimo da fase
  atingido em uma partida (repetibilidade — 10 cold boots — fica para a
  próxima sessão, conforme o próprio plano v2; a fase NÃO é declarada
  "validada" hoje).
- Frames/erros fluindo sem PASS → regime ISOC ativo; registrar contadores.
- Silêncio total de novo → o bug 2.4.2 não era a causa (ou não a única);
  registrar e encerrar.
- **Tentativa 2** idêntica (balé completo do USB-C), a critério do
  proprietário. **Nunca uma terceira hoje.**

### Etapa 5 — Evidência (minha)

- `extract_jpeg_dump.py --all` no log; validar CRCs; **enviar as fotos ao
  proprietário** na conversa.
- Arquivar o log como
  `docs/professor-virtual/evidencias/f2b/t5-run15-stack251-<resultado>.log`.

### Etapa 6 — Encerramento com gates (minha, com 1 aval do proprietário)

1. Atualizar: `plano_codex_v2.md` (seção de resultados, como o plano exige),
   `fase-2b.md` (tabela factual das tentativas + retomada), decision-log
   (entrada da rodada 15: upgrade experimental da pilha, resultado, SHA-256).
2. **Preservar e limpar o experimento**: salvar o patch em
   `<scratchpad>/idf_component.rodada15.patch`
   (`git diff main/idf_component.yml`) e **restaurar cirurgicamente o
   manifesto** — o worktree termina LIMPO e o experimento fica preservado
   para reaplicação na próxima sessão. A alteração experimental **não pode
   ser consolidada até que a placa `esp-p4-function-ev-board` volte a
   compilar e a regressão P4/S3 passe**; o caminho para isso (sincronização
   seletiva com o upstream, ajuste de dependência ou outra compatibilização
   comprovada) fica em aberto, decidido na próxima sessão sob o plano v2.
3. Gates: `python3 -m unittest discover -s scripts/tests` (verdes),
   `git diff --check`, `git status --short` limpo após o commit.
4. **Commit único de EVIDÊNCIA E DOCUMENTOS apenas**: logs, `fase-2b.md`,
   decision-log, `plano_codex_v2.md` e `plano-claude-rodada15.md` (com aval
   do proprietário para os planos). `main/idf_component.yml` já estará
   restaurado — fora do commit por construção.
5. **Push é ação exclusiva do proprietário** (decision-policy): eu apenas
   preparo o comando (`git push origin main`) e apresento o
   `git log origin/main..HEAD` para conferência; **o proprietário executa**.
   Se os gates não fecharem antes de o proprietário parar, o push adia.
6. **Se houver frames**: o gate de repetibilidade do plano v2 (10 cold
   boots) fica agendado como primeiro item da próxima sessão de bancada —
   um PASS hoje não valida a fase.

## Se falhar (2 tentativas sem vida no canal)

Nada mais hoje. Próxima sessão (contexto fresco, no Mac): spike **direto de
`usb_host_uvc`** sem esp_video — 8→16 URBs desacoplados dos frame buffers,
contadores agregados por segundo no lugar de LOGD — passo já desenhado tanto
no plano v2 quanto nas ordens do tático. Windows/peças (hub alimentado com
`CONFIG_USB_HOST_HUBS_SUPPORTED=y`, adaptador A→C) permanecem documentados
como plano C na retomada da fase-2b.

## Rollback (cirúrgico, sem `git checkout` às cegas)

1. Backup primeiro:
   `cp main/idf_component.yml <scratchpad>/idf_component.yml.rodada15`.
2. Reversão cirúrgica: restaurar manualmente o bloco editado (esp_video
   ^2.0.1 de volta; descomentar o BSP) — o diff é pequeno e conhecido.
3. Rebuild regenera o lock (não versionado) com a pilha 2.0.1/2.4.2.
Zips antigos preservados no scratchpad.

## Riscos conhecidos (aceitos)

- A câmera tem arranque não-determinístico já observado ("moeda") — por isso
  o protocolo de nascimento conjunto (elimina os traps conhecidos: trava por
  reboot energizado e slot-fantasma) e as 2 tentativas.
- O bump de `esp_video` afeta também S3: a **regressão completa** (builds PV,
  7b original e representativos S3 + testes host) é obrigatória antes de
  qualquer consolidação do manifesto ou fechamento de fase.
- Com o manifesto experimental aplicado, a placa `esp-p4-function-ev-board`
  não compila (BSP removido) — condição de consolidação descrita na Etapa
  6.2; solução em aberto para a próxima sessão.

## Verificação de sucesso do plano

1. Log da run15 mostra URBs com >1 pacote ISOC (a correção ativa).
2. `PV-UVC-RUNG ... result=PASS` nos degraus-alvo com dumps de CRC válido e
   fotos abertas/enviadas.
3. Testes host verdes; manifesto restaurado (patch preservado no
   scratchpad); worktree limpo pós-commit; push preparado para o
   proprietário executar (ou explicitamente adiado).

## Apêndice operacional (autocontenção para sessão nova)

Tudo o que uma sessão fria precisa e que não está em outro documento:

- **Porta serial**: `/dev/cu.usbmodem5B3E0883401` @ 115200 (ponte CH343 da
  7B; o device só existe com o USB-C conectado ao Mac).
- **Binário staged**: `releases/uvc-spike/merged-binary.bin` — conferir
  `shasum -a 256` começando com `1e2a988201b600b9`. Se ausente, rebuildar:
  reaplicar o patch do manifesto (abaixo), apagar o zip da variante em
  `releases/`, e rodar `python3 scripts/release.py
  waveshare/esp32-p4-wifi6-touch-lcd --name
  esp32-p4-wifi6-touch-lcd-7b-professor-virtual-uvc-spike` com o IDF 6.0.2
  (`source ~/.espressif/tools/activate_idf_v6.0.2.sh` +
  `export PATH="$IDF_PATH/tools:$PATH"`).
- **Flash (gesto do proprietário)**: `! zsh releases/flash-spike.sh` — o
  script existe em `releases/` (não versionado); se ausente, é:

  ```sh
  #!/bin/zsh
  source /Users/institutorecriare/.espressif/tools/activate_idf_v6.0.2.sh >/dev/null 2>&1
  cd /Users/institutorecriare/VSCodeProjects/xiaozhi-esp32
  exec python3 -m esptool --chip esp32p4 -p /dev/cu.usbmodem5B3E0883401 \
    -b 460800 --after no-reset write-flash 0x0 releases/uvc-spike/merged-binary.bin
  ```

- **Captura sem-reset** (NUNCA abrir a porta com DTR/RTS default — isso
  reseta a placa e trava a câmera). Rodar em background com o python do
  venv do IDF (`~/.espressif/tools/python/v6.0.2/venv/bin/python`):

  ```python
  import serial, sys, time
  PORT = "/dev/cu.usbmodem5B3E0883401"
  LOG = sys.argv[1]
  s = serial.Serial()
  s.port = PORT; s.baudrate = 115200; s.timeout = 1
  s.dtr = False; s.rts = False          # aplicados ANTES do open: sem reset
  while True:
      try: s.open(); break
      except serial.SerialException: time.sleep(0.5)
  with open(LOG, "ab", buffering=0) as out:
      while True:
          try: data = s.read(65536)
          except serial.SerialException:   # placa desligou (balé do USB-C)
              try: s.close()
              except Exception: pass
              time.sleep(0.5)
              while True:
                  try: s.open(); break
                  except serial.SerialException: time.sleep(0.5)
              continue
          if data: out.write(data)
  ```

- **Monitor** (notificação por evento): `tail -n 0 -f <log> | grep -a -E
  --line-buffered "rst:0x|UVC device found|Allocating|PV-UVC-RUNG|PV-UVC-DUMP
  result|PV-UVC-JPEG|PV-UVC-SUMARIO fim|assert failed|frame error"`.
- **Extração de fotos**: `python3 scripts/pv/extract_jpeg_dump.py --all
  <log> -o <dir>` (valida len+CRC-32 por bloco; nomes `foto_NN_WxH.jpg`).
- **Patch do manifesto experimental** (se o worktree estiver limpo,
  reaplicar antes de rebuildar; se já estiver aplicado, não duplicar): em
  `main/idf_component.yml`, (a) trocar a versão de `espressif/esp_video` de
  `^2.0.1` para `^2.3.0`; (b) adicionar dependência `espressif/usb_host_uvc`
  com `version: ~2.5.1` e `rules: if target in [esp32p4, esp32s3]`;
  (c) comentar o bloco `espressif/esp32_p4_function_ev_board` (^5.2.3, o
  BSP trava esp_video ~2.0). Apagar `dependencies.lock` antes do build.
- **Regras de bancada aprendidas** (custaram caro): liberar a porta serial
  (matar capturas) antes de todo flash; todo reboot da placa com a câmera
  energizada pode emudecê-la (só energia física recupera); unplug da câmera
  sem stream aberto cria slot-fantasma no driver (só boot com porta vazia
  limpa); `VIDIOC_S_PARM` fora do default emudece a câmera; a tela da placa
  fica APAGADA no spike (por desenho).
