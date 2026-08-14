# Plano de implementação — baseline UVC oficial no ESP32-P4

## Resumo

Executar o [exemplo UVC oficial do ESP-IDF 6.0.2](https://github.com/espressif/esp-idf/tree/v6.0.2/examples/peripherals/usb/host/uvc) fora do XiaoZhi, isolando completamente nosso código. Excepcionalmente, o Codex preparará, configurará, compilará e analisará o teste. A gravação na placa e os gestos físicos continuarão sob responsabilidade do proprietário.

## Implementação

1. Aguardar a tentativa atual terminar e preservar seu log, hash do binário e estado do Git. Não alterar, descartar ou misturar as mudanças não commitadas do Claude.

2. Criar o projeto em `/private/tmp/pv-uvc-official-idf602`, copiando o exemplo instalado em:
   `/Users/institutorecriare/.espressif/v6.0.2/esp-idf/examples/peripherals/usb/host/uvc`.

3. Garantir independência e reprodutibilidade:
   - manter `main/main.c` byte a byte igual ao oficial, SHA-256 `7b68afa7f239112280390ce6d30f4679b5958769a84692477ca189922b04fe78`;
   - usar ESP-IDF `v6.0.2`;
   - fixar `usb_host_uvc ==2.5.1` no manifesto;
   - confirmar as versões resolvidas em `dependencies.lock`.

4. Alterar somente configurações necessárias ao hardware:
   - `CONFIG_UVC_CHECK_PAYLOAD_HEADER_EOH=n`, porque a NE-HD362 não marca EOH;
   - `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`;
   - `CONFIG_ESP32P4_REV_MIN_100=y`, compatível com a placa rev. 1.3;
   - conservar PSRAM, `USB_HOST_HW_BUFFER_BIAS_IN` e demais defaults oficiais.

5. Compilar para `esp32p4`, conferir a imagem com `esptool image-info` e gerar:
   - binários e argumentos de flash;
   - SHA-256 do firmware;
   - manifesto com versões, configurações e hashes;
   - comando de gravação para o proprietário.

6. Não modificar backend, pinos, particionamento, NVS ou firmware normal do Professor Virtual.

## Testes na placa

1. O proprietário grava o firmware oficial com a câmera desconectada.

2. Capturar a serial sem reset automático. Após `Installing UVC driver`, conectar a câmera e observar por 60 segundos.

3. Repetir três ciclos:
   - desconectar somente a câmera;
   - aguardar 10 segundos;
   - reconectar;
   - exigir nova enumeração e retomada do streaming.

4. Executar três partidas frias adicionais com a câmera já conectada, cortando toda a alimentação por 10 segundos entre elas.

5. Não alterar resolução ou buffers durante o baseline. O modo será exatamente `640×480 MJPEG @ 15 fps`, anunciado pela NE-HD362.

## Critérios e decisão posterior

- **PASS de transporte:** pelo menos 10 mensagens `New frame!` em até 30 segundos em cada uma das três partidas frias, além de reenumeração correta nos três replugs.
- **Resultado instável:** algum frame, mas sem repetibilidade. Não considerar funcionamento validado.
- **FAIL:** câmera enumera e abre, mas nenhuma mensagem `New frame!`.
- **Falha de enumeração:** tratar separadamente como porta, alimentação, conexão ou host USB.

Decisão automática:

- Se o exemplo oficial passar, alinhar o spike XiaoZhi inicialmente aos parâmetros oficiais: modo fixo `640×480@15`, três buffers, três URBs de 10 KiB, task USB permanente e nenhuma escada dinâmica. Reintroduzir cada diferença isoladamente até localizar a regressão.
- Se o exemplo oficial falhar apenas com a NE-HD362, testar uma webcam da lista oficial no mesmo firmware. Se ela funcionar, classificar como incompatibilidade NE-HD362 ↔ driver/P4.
- Se uma webcam oficialmente testada também falhar, investigar porta USB-A, VBUS, PHY/controlador da placa e testar OTG-C ou segunda placa.
- Não integrar UVC ao produto enquanto não houver repetibilidade e pelo menos um JPEG integral validado posteriormente por SOI, EOI, CRC e decodificação.

## Evidências e encerramento

Salvar logs brutos, hashes e resultado em `docs/professor-virtual/evidencias/f2b/`, atualizar a cronologia da F2B sem reescrever evidências anteriores e registrar quais conclusões antigas ficaram invalidadas pelo bug do daemon USB. O proprietário restaurará o firmware anterior após o experimento. Nenhum `git push` ou `flash` será realizado pelo Codex.
