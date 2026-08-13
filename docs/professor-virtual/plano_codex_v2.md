# Plano Codex v2 — Correção e integração da câmera UVC

## Artefato e manutenção

- Este arquivo é o plano operacional específico para corrigir e integrar a
  câmera UVC do Professor Virtual.
- Manter este documento atualizado com resultados, hashes dos binários,
  decisões e desvios encontrados durante a execução.
- Este plano não substitui `fase-2b.md` nem `plano-firmware.md`; esses documentos
  continuam sendo as fontes de acompanhamento da fase e do firmware completo.

## Diagnóstico e solução escolhida

A câmera NE-HD362 está saudável e enumera no ESP32-P4. A causa mais provável do
canal sem frames é o `usb_host_uvc 2.4.2`: essa versão tem um bug oficial que
força URBs isócronas a somente um pacote, produzindo exatamente o sintoma
observado — stream aberto, zero frames e nenhum erro conclusivo. A correção
entrou no 2.5.x:

- PR oficial: <https://github.com/espressif/esp-usb/pull/450>
- Changelog 2.5.1:
  <https://components.espressif.com/components/espressif/usb_host_uvc/versions/2.5.1/changelog?language=en>

Decisões fixadas:

- Arquitetura final exclusivamente UVC.
- Câmera CSI fora da variante de produção do Professor Virtual.
- Alinhar seletivamente com o upstream atual do XiaoZhi usando
  `esp_video ^2.3.0`, que depende de `usb_host_uvc 2.5.*`:
  <https://raw.githubusercontent.com/78/xiaozhi-esp32/main/main/idf_component.yml>.
- Usar `usb_host_uvc ~2.5.1`, evitando a regressão de memória da 2.5.0.
- Preview UVC em baixa resolução e captura em alta resolução.
- Preservar o JPEG MJPEG original, sem reencodificação.
- Se o travamento após reboot persistir, usar chave externa de VBUS comandada
  pelo firmware.
- Nenhuma alteração no backend ou no contrato HTTP.

## Implementação

### 1. Atualizar a pilha

- Atualizar `main/idf_component.yml` para `esp_video ^2.3.0`.
- Declarar diretamente `usb_host_uvc ~2.5.1` para tornar a versão requerida
  explícita.
- Regenerar `dependencies.lock` somente com ESP-IDF 6.0.2.
- Confirmar no lock e no log de boot que nenhuma versão 2.4.x permanece.
- Não editar ou versionar `managed_components/`, `sdkconfig*` ou saídas de
  build.
- Adaptar as extensões próprias de `EspVideo` às APIs do 2.3, preservando
  placas CSI existentes.
- Manter `CONFIG_UVC_CHECK_PAYLOAD_HEADER_EOH=n` exclusivamente nas variantes
  da NE-HD362.

### 2. Validar a correção no spike

- Recompilar o spike com a pilha nova e sem log por pacote.
- Registrar commit, SHA-256 do binário, versões dos componentes, MPS,
  quantidade e tamanho das URBs.
- Confirmar que cada URB isócrona passa a conter pelo menos quatro pacotes, em
  vez de um.
- Executar dez cold boots com a escada:
  - 800×600;
  - 1920×1080;
  - 2592×1944;
  - 3264×2448;
  - 4000×3000 opcional.
- Exigir JPEG com SOI/EOI, CRC, dimensões e decodificação válidos.

**Resultado (rodada 15, 2026-08-13, Mac — roteiro
`plano-claude-rodada15.md`):** pilha atualizada e validada em bancada com
duas partidas frias limpas (nascimento conjunto placa+câmera, máx. 2
tentativas do método). Binário: laço de escadas do commit `1c02248` com
`esp_video 2.3.0` + `usb_host_uvc 2.5.1` (lock conferido, nenhuma 2.4.x),
merged-binary sha256
`1e2a988201b600b974ff0ac5609fce8b423b7993508907a2b53dd3f0ca6057ba`.
A correção do bug de URB está **ativa e confirmada no log** — `Allocating
4 USB transfers for ISOC. Each: 12288 bytes, 4 ISOC packets, 3072 MPS` em
todos os 49 starts de stream (o critério "pelo menos quatro pacotes por
URB" deste plano foi atendido). **Mesmo assim o canal ISOC permaneceu
mudo**: 9 escadas completas nas duas tentativas, 0 frames, 0 `frame
error`, 0 `usb err`, DQBUF `errno=1` em todos os degraus; enumeração e
Probe/Commit perfeitos; teardown limpo (o assert `usbh_dev_close` da
pilha antiga sumiu). Sem JPEGs para validar (extrator: nenhum bloco).
Evidência: `evidencias/f2b/t5-run15-stack251-silencio.log`. Desvio
registrado: PSRAM livre cai por degrau (buffers REQBUFS aparentemente não
devolvidos entre degraus no glue 2.3.0) — não explica o silêncio. Os dez
cold boots deste plano não foram executados (sem sentido sem frames).
**Conclusão: o bug da 2.4.2 não era a causa, ou não a única — segue o
ramo abaixo ("se ainda não houver frames").** O manifesto experimental
foi revertido ao fim da rodada (consolidação condicionada ao BSP da
`esp-p4-function-ev-board` voltar a compilar + regressão P4/S3).

Se ainda não houver frames:

- Criar teste direto com `usb_host_uvc 2.5.1`, removendo o V4L2/`esp_video` da
  equação.
- Começar com 8 URBs e `urb_size=0`, usando o padrão oficial de 4×MPS.
- Testar 12 e 16 URBs apenas se necessário.
- Escolher automaticamente o menor valor que passe 10/10 cold boots.
- Coletar contadores agregados de callbacks, bytes e status por segundo.
- Fazer A/B com hub alimentado, segunda placa e segunda câmera antes de
  condenar a classe UVC.

### 3. Implementar a câmera UVC de produção

- Adicionar `UvcCamera` em `main/boards/common/`, derivada de `Camera`.
- Criar variante única
  `esp32-p4-wifi6-touch-lcd-7b-professor-virtual-uvc`.
- Nessa variante:
  - não construir ou inicializar CSI;
  - inicializar USB Host/UVC;
  - preservar display, touch, áudio, rede e Professor Virtual;
  - exportar exatamente um `DECLARE_BOARD(...)`.
- Usar diretamente a API pública de `usb_host_uvc`, separando buffers de frame
  e URBs.
- O callback apenas deposita eventos ou referências em filas limitadas.
- Toda troca de modo, abertura, fechamento e devolução de frame ocorre na task
  serial da câmera.

Configuração inicial:

- Preview: MJPEG 800×600, 3 buffers de 1 MiB.
- Captura: MJPEG 3264×2448, 2 buffers de `width × height / 2`.
- Transporte: 8 URBs independentes, tamanho automático de 4×MPS.
- FPS padrão da câmera; não usar `VIDIOC_S_PARM`.

Fluxo de captura:

1. Parar e fechar o preview.
2. Abrir o modo de alta resolução.
3. Descartar até 10 frames, limitado a 3 segundos, para AF/exposição.
4. Capturar JPEG válido e menor ou igual a 10 MB.
5. Copiar somente os bytes entre SOI e EOI.
6. Devolver todos os frames e fechar o stream.
7. Reabrir o preview.
8. Se houver falha, executar uma única tentativa de recuperação; nunca entrar
   em loop infinito.

### 4. Evoluir as interfaces da câmera

Adicionar de forma compatível em `camera.h`:

- `CameraJpegFrame`, com dados, tamanho, dimensões e posse transferida ao
  chamador.
- `CaptureJpeg()` opcional, com resultados `Unsupported`, `Ok`, `Unavailable`
  e `Failed`.
- `IsConnected()` opcional.
- Implementações antigas continuam usando `CaptureRaw()` e codificação q85.

No `PvCamera`:

- Preferir `CaptureJpeg()` quando disponível.
- Manter fallback CSI apenas nas variantes legadas.
- Separar resolução do preview e resolução da foto.
- Adicionar eventos de conexão e desconexão.
- Manter preview e captura mutuamente exclusivos e somente uma captura em voo.
- Desabilitar o botão de foto quando a UVC estiver indisponível.

Para a revisão:

- Adicionar `jpeg_to_image_scaled`, sem quebrar `jpeg_to_image`.
- Decodificar o mesmo JPEG capturado para RGB565 limitado a 800×600.
- Preservar proporção e dimensões múltiplas de oito.
- Não alocar RGB565 na resolução integral de 8–12 MP.

### 5. Recuperação elétrica por VBUS

Se a câmera ainda travar ao permanecer energizada durante reboot:

- Construir interposer USB externo com `TPS2051CDBVR`, que oferece limite de
  500 mA, soft-start, descarga e bloqueio reverso:
  <https://www.ti.com/product/TPS2051C>.
- Interromper apenas VBUS; manter D+/D− curtos e como par diferencial.
- Usar o conector P3 do esquema oficial da Waveshare:
  <https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-7B/ESP32-P4-WIFI6-Touch-LCD-7B.pdf>.
  - GPIO28/P3-6 para `EN`, com pulldown externo de 100 kΩ;
  - GPIO29/P3-5 para fault/overcurrent, com pull-up para 3,3 V;
  - GND/P3-11.
- Não usar GPIO34 ou GPIO36, pois são strapping pins.
- Tratar essa configuração como identidade da nova variante UVC; não modificar
  a variante 7B publicada.

Recuperação:

1. Manter VBUS desligado no reset.
2. Esperar 1 segundo para descarga.
3. Ligar VBUS.
4. Esperar enumeração por até 10 segundos.
5. Se falhar, fechar a pilha e fazer um único ciclo elétrico.
6. Persistindo a falha, deixar a câmera indisponível e informar o usuário.

## Testes e aceitação

### Software

- Manter os 21 testes host atuais verdes.
- Adicionar testes para:
  - versões das dependências;
  - seleção da variante UVC;
  - ausência de inicialização CSI;
  - posse e liberação de JPEG;
  - JPEG truncado, corrompido, com padding ou acima de 10 MB;
  - scaler da revisão;
  - conexão, desconexão, timeout e recuperação.
- Rodar `clang-format --dry-run -Werror` apenas nos arquivos tocados.
- Fazer builds canônicos:
  - spike UVC;
  - variante PV UVC;
  - variante PV CSI;
  - variante 7B original;
  - placas de câmera representativas P4 e S3.
- Executar a matriz de CI completa, pois `esp_video` afeta P4 e S3.
- Validar fisicamente a variante CSI existente por 10 minutos.

### Bancada UVC

- 10/10 cold boots com todos os degraus obrigatórios.
- 30 capturas consecutivas em 3264×2448.
- Usar 3264×2448 como padrão se passar.
- Permitir 2592×1944 somente se a extração continuar integral.
- Tratar 4000×3000 como opcional.
- Validar A4 inteira, lápis, caneta, centro e cantos em luz boa e reduzida.
- Comparar a mesma página com a captura desktop e o backend real.

### Produto integrado

- Preview médio de 5 fps durante pelo menos 10 minutos.
- Executar 100 ciclos preview → captura → revisão → preview.
- p95 do toque até revisão em até 5 segundos.
- Nenhum JPEG inválido ou vazamento progressivo de PSRAM.
- Maior bloco livre retorna a ±5% do baseline após liberar cada foto.
- Executar 20 desconexões/reconexões sem crash.
- Executar 20 reboots com a câmera conectada; com a chave de VBUS, exigir
  20/20 recuperações.
- Testar ao menos 10 páginas no backend real, com texto e manuscrito
  integralmente extraídos.
- Encerrar somente após revisão independente sem P0/P1.

## Consolidação e limites

- Atualizar este arquivo durante cada etapa com resultados reais.
- Atualizar também `fase-2b.md` e `plano-firmware.md` ao consolidar a fase.
- Registrar no decision-log:
  - arquitetura UVC única;
  - versões congeladas;
  - resolução final;
  - chave externa de VBUS.
- Preservar os logs antigos e esclarecer que o PASS anterior foi perturbado
  pelo flood serial.
- Flash continua sendo ação exclusiva do proprietário.
- Backend, contrato HTTP, lógica pedagógica e NVS permanecem inalterados.
- Se a NE-HD362 falhar mesmo com driver 2.5.1, caminho direto e VBUS
  controlado, testar outra câmera UVC na mesma 7B.
- Troca de plataforma somente após falha comprovada da classe UVC no
  ESP32-P4.
