# Relatório factual da investigação UVC — ESP32-P4 7B + NE-HD362

**Data da auditoria:** 14/08/2026  
**Escopo:** câmera UVC NE-HD362 na Waveshare ESP32-P4-WIFI6-Touch-LCD-7B, usando os spikes do Professor Virtual e o exemplo `usb_host_uvc` do ESP-IDF 6.0.2.  
**Snapshot do código:** HEAD `360c4527d8a0e84c6e1c98d7e89cfb111d0f91b3`, com mudanças de encerramento ainda não commitadas.

## Resposta curta

Não foi demonstrada uma causa raiz para a falha UVC.

O que os dados demonstram é mais limitado e, para a decisão de produto, suficiente: **a combinação NE-HD362 + placa 7B + pilha USB/UVC testada não entregou captura repetível**. Nos 19 logs brutos preservados há uma única linha de degrau com `result=PASS`, correspondente a um JPEG válido de 800×600 e 32.405 bytes. Não há outro frame válido arquivado. Esse resultado não voltou a ser obtido nas tentativas subsequentes, mas o ELF exato da rodada 4 (`fabe23054…`) não aparece novamente nos logs; portanto, não se pode afirmar que o mesmo binário bit a bit foi reexecutado e falhou. Os requisitos da fase — 1920×1080 e pelo menos um degrau de 2592×1944 ou maior, repetidos — nunca foram atingidos.

O exemplo oficial também abriu a câmera, mas não entregou frames:

- rodada 24, seleção automática: 133 starts, 132 timeouts e zero `New frame!`;
- rodada 25, `alt=4`, `MULT=0`, payload e pipe em 640 bytes: 198 starts, 197 timeouts e zero `New frame!`.

**Minha opinião, separada dos fatos:** é correto parar de investir nesta câmera com esta pilha para o produto atual. Isso não autoriza concluir que “UVC não funciona no ESP32-P4”, que toda câmera UVC falhará, ou que a causa está definitivamente no chip, no driver, na placa ou na câmera. O resultado rejeita esta rota concreta por falta de confiabilidade; não resolve a causa geral.

## O que foi construído

### 1. Spike V4L2 sobre `esp_video`

Foi criado um aplicativo de bancada isolado do XiaoZhi normal:

- `pv_uvc_spike.{h,cc}`;
- gate `CONFIG_PV_UVC_SPIKE`;
- variante de build `...-professor-virtual-uvc-spike`;
- enumeração dos formatos e resoluções anunciados pela câmera;
- escada 800×600 → 1920×1080 → 2592×1944 → 3264×2448 → 4000×3000;
- validação de SOI/EOI, dump serial, CRC e extração de múltiplos JPEGs;
- telemetria de timeouts, erros e resultados por degrau.

O extrator `scripts/pv/extract_jpeg_dump.py` ganhou suporte a `--all`, acompanhado por testes host. Antes da bancada, o histórico registra três builds verdes — spike, Professor Virtual normal e placa 7B original — e 21 testes host passando. Esses resultados validam compilação e ferramentas; não validam o transporte físico.

### 2. Spike direto sobre `usb_host_uvc`

Depois que o caminho V4L2 não se mostrou repetível, foi criado `pv_uvc_direct.{h,cc}`, sem `esp_video`:

- API pública do `usb_host_uvc` usada diretamente;
- 4 buffers de frame em PSRAM;
- número e tamanho de URBs configuráveis;
- telemetria agregada por segundo;
- retorno explícito da posse do frame fora do callback;
- mesma escada de resoluções e mesmos critérios de JPEG.

Esse spike permitiu variar uma condição por vez: versão do driver, 8 versus 4 URBs, URB de um pacote, sequência de VBUS e nível de log.

### 3. Instrumentação temporária do driver

Foram aplicados patches reversíveis de bancada para contar:

- URBs completadas;
- pacotes com dados;
- bytes recebidos;
- headers rejeitados;
- flags UVC ERR/EOF;
- falhas de ressubmissão.

Também foi forçado um alt setting `MULT=0` de 944 bytes na rodada 23B. Os patches de telemetria e alt 944 foram preservados em `docs/professor-virtual/evidencias/f2b/`.

### 4. Exemplo oficial fora do repositório XiaoZhi

Foi copiado para `/private/tmp` o exemplo `examples/peripherals/usb/host/uvc` do ESP-IDF 6.0.2. No baseline da rodada 24:

- `main/main.c` tinha o mesmo SHA-256 do arquivo oficial: `7b68afa7f239112280390ce6d30f4679b5958769a84692477ca189922b04fe78`;
- `usb_host_uvc` era 2.5.1;
- `espressif/usb` era 1.4.1;
- foram alteradas apenas configurações necessárias à revisão do P4 e a checagem EOH foi desligada para esta câmera.

A rodada 25 manteve o aplicativo oficial, mas adicionou um patch no driver para selecionar `alt=4` e limitar corretamente a negociação a 640 bytes. Portanto, a rodada 24 é o baseline praticamente intacto; a rodada 25 é um teste controlado sobre esse baseline, não um driver sem patch.

## O que foi testado e observado

| Rodada ou grupo | Variável principal | Observação preservada | O que o teste permite concluir |
|---|---|---|---|
| 1 | baseline V4L2, 2 URBs de 3072 B | câmera enumera; zero frame; 19 asserts `usbh_dev_close` no log | baseline e teardown falharam |
| 2–3 | DEBUG, prioridade/buffers, EOH on/off, dreno | 13.140 linhas de header inválido com EOH ligado; diário registra ausência de crash após desligar EOH, ainda sem frame | EOH era uma incompatibilidade real, mas não a falha restante |
| 4 | replug físico fresco + DEBUG por pacote | um JPEG 800×600/32.405 B; 15.011 `frame error`; degraus seguintes falham no `open` | uma captura foi possível; não voltou a ocorrer, mas o ELF exato não foi reexecutado nos logs |
| 5–6 | device único; ciclo do root port/VBUS | reconfiguração funcionou; cinco degraus sem frame em cada log | lifecycle e comando de root port não produziram captura |
| 7–14 | reset/replug, laço, fps 10, janela de 30 s | run11 teve muitos `frame error`; runs 13/14 sem PASS; fps 10 revertido | nenhuma configuração produziu frame válido |
| 15 | `esp_video` 2.3.0 + `usb_host_uvc` 2.5.1, URBs de 4 pacotes | 49 alocações com 4 pacotes; 47 degraus FAIL registrados; zero PASS | o URB de um pacote da 2.4.2 não era explicação suficiente |
| 17–22 | spike direto; versões, URBs, VBUS e DEBUG | runs 17/22 exibem tráfego como `frame error`; runs 18–21 têm zero callback visível no app; nenhum frame válido | DEBUG muda o comportamento observado, mas não demonstra uma corrida específica |
| 23A–B | telemetria no driver; high-bandwidth versus alt 944/MULT=0 | completions e dados nos dois casos; muitos headers inválidos; zero frame | o “canal mudo” não estava necessariamente sem tráfego; mecanismo da perda segue aberto |
| 24 | exemplo oficial, seleção automática | 133 starts, 132 timeouts, zero `New frame!` | o nosso spike não é requisito para reproduzir a falha |
| 25 | exemplo oficial + alt 4, payload=pipe=640, MULT=0 | 198 starts, 197 timeouts, zero `New frame!` | MULT/high-bandwidth não explica sozinho a falha |

O diário da F2B chama a sequência de “25 rodadas”. O arquivo de evidências contém 19 logs; algumas rodadas não têm log individual, e a rodada 16 foi principalmente a implementação do spike direto. Por isso o relatório não usa “25 logs” nem trata todas as rodadas como observações equivalentes.

## O que deu errado

### Problemas confirmados e corrigidos no experimento

1. **Checagem EOH incompatível com a câmera.** Com a checagem ligada, o driver registrou milhares de `invalid UVC payload header`; desligá-la removeu esse descarte específico. A ausência de frames continuou em testes posteriores, inclusive no exemplo oficial.

2. **Teardown com assert.** A rodada 1 caiu repetidamente em `usbh_dev_close` enquanto havia transferência de controle em voo. O ciclo foi alterado para drenar e manter um device aberto durante a escada. O boot loop deixou de ser o bloqueio, mas frames não apareceram.

3. **`open→close→open` do caminho V4L2.** Depois do único frame da rodada 4, quatro degraus falharam imediatamente no `open`. O spike passou a reconfigurar um device único; os cinco degraus então eram aplicados, mas continuaram sem frame.

4. **Task de eventos USB podia terminar no unplug.** O código inicial seguia o padrão de exemplo que sai ao receber `ALL_FREE`. Ele foi alterado para manter o daemon vivo. Runs 20/21 registram o evento de liberação com a mensagem de que o daemon continuou. Isso corrige o ciclo de eventos; não corrige a formação de frames.

5. **Dependência CMake condicionada tarde demais.** O requisito de componente estava dentro de `if(CONFIG_...)`, mas requisitos são resolvidos antes do Kconfig. O gate foi trocado para target P4/S3. Isso corrigiu a fiação do build; não é uma explicação para os timeouts depois que o binário compilou e rodou.

### Falha que permaneceu depois dessas correções

A câmera enumerava, anunciava formatos, aceitava seleção de formato e abria o stream. O que faltava era um frame MJPEG completo entregue ao aplicativo. Conforme o teste, a superfície observável variou entre:

- timeouts sem callback de frame no aplicativo;
- muitas linhas `frame error`;
- muitas completions e bytes no driver, mas headers classificados como inválidos;
- zero `New frame!` no exemplo oficial.

Essas manifestações são fatos dos logs. Elas não identificam, sozinhas, onde os bytes deixam de formar um JPEG válido.

## Conclusões anteriores que precisaram ser retiradas

### “A causa é uma corrida de timing”

Não demonstrado. Ligar DEBUG por pacote coincidiu com atividade visível nos runs 17 e 22. Isso prova que o logging alterou o comportamento observado. Não prova qual estado, janela, fila ou registrador causou a diferença. A rodada 23 ainda mostrou completions na pilha 2.5.1 sem flood, invalidando a leitura anterior de “nenhuma completion”.

### “A causa é o high-bandwidth/MULT não programado”

Há um fato de código: no stack local, o HCD passa ao HAL o MPS base do endpoint e a função de inicialização de `HCCHAR` não escreve o campo `EC`. Isso é uma lacuna observável no código, não uma causa demonstrada para estes logs.

O teste corretamente casado da rodada 25 removeu high-bandwidth: `alt=4`, MPS 640, `MULT=0`, payload 640. Ainda assim houve 198 starts e zero frame. Logo, high-bandwidth/MULT não é explicação suficiente para a falha observada.

### “A câmera e a placa estão perfeitas”

Não demonstrado. O diário registra que a câmera funcionou no Photo Booth de um Mac e os logs do P4 mostram enumeração e atividade USB. Isso é evidência contra uma câmera completamente morta. Não exclui uma incompatibilidade específica, defeito intermitente, sensibilidade elétrica, diferença de protocolo ou problema restrito à interação entre as duas unidades.

### “O software USB da Espressif tem um defeito conhecido completamente mapeado por nós”

Não demonstrado. O exemplo oficial reproduz a falha nesta combinação, o que retira o código do Professor Virtual como requisito da falha. Mas sem segunda câmera, segunda placa ou captura de barramento, o experimento não separa definitivamente driver, HCD, dispositivo, elétrica e compatibilidade.

### “Headers inválidos provam remontagem errada”

Não demonstrado. Os contadores da rodada 23 provam que o driver classificou muitos headers como inválidos e não entregou frames. O teste alt 944 ainda tinha payload commitado maior que o pipe. A rodada 25 corrigiu esse casamento e continuou falhando, mas não trouxe captura de barramento para localizar a corrupção.

## Erros de método identificados

1. **Controle com binário diferente.** O primeiro “controle do build que passou” não reproduziu o binário real da rodada 4: o PASS ocorreu com flood DEBUG ligado. A comparação inicial não controlou essa variável.

2. **Silêncio confundido com ausência de tráfego.** Runs 18–21 mostravam zeros no nível do aplicativo. A instrumentação da rodada 23 revelou completions e dados no driver. Assim, “zero callback de frame” não pode ser reescrito como “zero transação no barramento”.

3. **Primeiro MULT=0 mal casado.** Na rodada 23B, o pipe foi limitado a 944 bytes, mas a câmera continuou commitando payload maior. Esse ensaio é válido como observação, mas não como teste limpo de solução. A rodada 25 foi o teste limpo e também falhou.

4. **Afirmações causais foram feitas cedo demais.** O commit `360c452` e a entrada correspondente do decision-log chamaram o timing de causa raiz. A entrada posterior `F2B-Encerramento-UVCRejeitada` retirou essa conclusão.

## O que não foi testado ou não tem evidência local de execução

Não há no arquivo local evidência de bancada para:

- uma segunda câmera UVC na mesma placa;
- uma das câmeras listadas como testadas pelo exemplo oficial;
- uma câmera UVC com transporte BULK;
- uma segunda placa ESP32-P4 7B;
- hub alimentado, porta OTG-C ou medição osciloscópica de VBUS durante o streaming;
- captura de barramento USB entre câmera e host;
- patch do HCD que programe explicitamente `HCCHAR.EC/MULT`;
- matriz de dez partidas frias por célula;
- captura válida em 1920×1080, 2592×1944, 3264×2448 ou 4000×3000.

Essas lacunas impedem uma conclusão geral sobre todas as câmeras UVC ou todos os ESP32-P4. Elas não alteram o resultado de produto desta câmera: o requisito de repetibilidade ficou muito longe de ser atendido.

## Estado do repositório no momento da auditoria

O HEAD ainda é `360c452`, cujo título afirma incorretamente que a causa raiz foi encontrada. A correção epistemológica está no worktree, ainda sem commit:

- `fase-2b.md`, `plano-firmware.md` e `decision-log.jsonl` retiram a causa raiz e encerram a rota;
- os quatro arquivos `pv_uvc_spike.*` e `pv_uvc_direct.*` estão com exclusão staged;
- ramos e variantes UVC estão sendo removidos de CMake, Kconfig, `config.json`, `main.cc` e testes, mas essas mudanças ainda não formam um commit de encerramento.

Portanto, antes de push, é necessário revisar e commitar o conjunto de encerramento. Este relatório não altera firmware e não executa commit ou push.

## Recomendação final

Minha recomendação é **encerrar a NE-HD362/UVC como rota ativa do Professor Virtual na pilha atual**, preservar os logs e prosseguir com CSI enquanto chega o novo hardware.

Eu não chamaria isso de “desistência definitiva de UVC”. Reabriria a rota somente diante de uma mudança material e verificável, por exemplo:

- outra câmera com transporte comprovadamente diferente;
- uma correção nova do stack/HCD acompanhada de teste reproduzível;
- segunda câmera/segunda placa que permita isolar a interação;
- captura de barramento que mostre exatamente onde o fluxo diverge.

Sem uma dessas mudanças, repetir combinações de URB, prioridade e log tende a produzir mais observações sem responder a causa. A decisão de parar agora é sustentada pela diferença objetiva entre o critério de aceitação e o resultado: exigia alta resolução repetível; entregou um único frame 800×600 que não voltou a ser obtido nas tentativas posteriores.

## Fontes locais

- `docs/professor-virtual/fases/fase-2b.md`
- `docs/professor-virtual/evidencias/f2b/*.log`
- `docs/professor-virtual/evidencias/f2b/t5-rodada4-foto-800x600.jpg`
- `.claude/autonomy/decision-log.jsonl`
- histórico e diffs Git entre `7acbf49` e `360c452`
- instalação local do ESP-IDF 6.0.2 e componentes gerenciados usados nos builds
- `source-notes.md`, `audit-uvc.sql` e `git-audit-f2b.txt` neste diretório

As pesquisas externas em `docs/professor-virtual/resultados_pesquisa/` foram deliberadamente excluídas como prova de bancada ou de causa raiz.
