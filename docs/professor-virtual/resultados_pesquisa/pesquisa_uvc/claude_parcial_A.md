# UVC Host ISOC high-bandwidth no ESP32-P4 — Relatório parcial

**Data:** 13 de agosto de 2026
**Status:** PARCIAL. 3 de 6 frentes concluídas. As frentes pendentes estão listadas na seção 8.

**Convenções de evidência usadas neste documento:**

| Marca | Significado |
|---|---|
| `[CÓDIGO]` | Verificado por mim em clone real do repositório, com arquivo:linha e commit |
| `[DOC]` | Documentação oficial, com citação literal e link |
| `[BANCADA]` | Observação da sua bancada, conforme relatada |
| `[AUTOR-ISSUE]` | Afirmação do autor de uma issue — não é prova |
| `[INFERÊNCIA]` | Dedução minha a partir de fatos verificados |
| `[HIPÓTESE]` | Ainda não validado |
| `[NÃO ENCONTRADO]` | Procurado e não achado — a ausência é o dado |

Repositórios clonados e usados como fonte primária:

- `espressif/esp-usb` @ `1fb14d33a942db7d1f7cae48b0b586d0cd21a3b3` (master, 2026-08-07)
- `espressif/esp-idf` @ tag `v6.0.2`
- `torvalds/linux` @ `3aa1dcaa4f6f` (v7.2.0-rc7)

---

## 1. Sumário executivo

1. `[CÓDIGO]` A pilha USB Host do ESP-IDF **nunca escreve HCCHAR.EC** (multi-count). Campo existe (`usb_dwc_struct.h:537`, bits 21:20), zero escritas em toda a árvore, v6.0.2 e master, todos os targets. Não existe setter.
2. `[CÓDIGO]` Os bits MULT de `wMaxPacketSize` são **descartados no HCD** (`hcd_dwc.c:1907`, máscara `0x07FF`). Só o class driver UVC os enxerga.
3. `[CÓDIGO]` O argumento "Scatter/Gather DMA dispensa o MC" está **refutado**: o Linux programa `HCCHAR.MULTICNT` justamente no modo descritor (`dwc2/hcd.c:1449-1451`).
4. `[CÓDIGO]` **O A/B 2.4.2 → 2.5.1 está explicado.** PR #450 inverteu um comparador e o commit `5ffef02` trocou o default; a única variável que mudou foi `num_isoc_packets` de 1 para 4. O tamanho por pacote continuou 3072 nos dois casos.
5. `[CÓDIGO]` **Achado de maior valor prático:** até a 2.4.1 o driver escolhia deliberadamente o alt setting de **MULT=0**. O PR #424 removeu esse critério. Ou seja, existe um regime MULT=0 comprovadamente alcançável — e 1024 B/µframe = 8 MB/s cobre seu produto com folga de 50×.
6. **Ainda incerto:** o mecanismo pelo qual 4 pacotes por URB produz silêncio total, e se a NE-HD362 oferece alt setting MULT=0.
7. **Solução de menor risco:** forçar o regime MULT=0 sem tocar no HCD (teste T1/T2 da seção 7).
8. **Próximo teste recomendado:** T0 — dump completo dos descritores da câmera. Custo ~10 minutos, elimina ou confirma metade das hipóteses.

---

## 2. O que mudou na sua leitura do problema

### 2.1. O A/B 2.4.2 vs 2.5.1 está resolvido — e não é o que parecia

`[CÓDIGO]` Três commits, em sequência, explicam exatamente os números da sua bancada.

**Antes do PR #450** (isto é, na 2.4.2), em `uvc_host.c`:

```c
// check if the urb_size is smaller than the maximum payload transfer size
size_t urb_size = stream_config->advanced.urb_size > vs_result.dwMaxPayloadTransferSize ?
                  vs_result.dwMaxPayloadTransferSize : stream_config->advanced.urb_size;
```

O comparador está invertido — o título do PR na Espressif é literalmente *"usb_host_uvc urb_size clamp inverted (IEC-505)"*. Com `urb_size = 10 KiB` e `dwMaxPayloadTransferSize = 3072`: `10240 > 3072` → `urb_size = 3072`.

Depois, em `uvc_transfers_allocate()`:

```c
max_packet_size *= (USB_EP_DESC_GET_MULT(ep_desc) + 1);   // 1024 * 3 = 3072
num_isoc_packets = usb_round_up_to_mps(transfer_size, max_packet_size) / max_packet_size;
```

`3072 / 3072 = 1`. **→ 3072 bytes por URB, 1 pacote ISOC.** Exatamente o que seus logs mostraram.

**Depois do PR #450** (commit `ae0641c`, merge `8196b6e`, 2026-03-31, autor externo `danieltwagner`) o comparador virou `<`, e depois o commit `5ffef02` (Tomas Rezucha, 2026-04-30) removeu o clamp inteiro e introduziu o default de 4×MPS:

```c
if (transfer_size == 0) {
    transfer_size = 4 * max_packet_size;   // 4 * 3072 = 12288
}
```

`12288 / 3072 = 4`. **→ 12288 bytes por URB, 4 pacotes ISOC.** Exatamente o que a 2.5.1 mostrou.

`[CÓDIGO]` **Conclusão:** o A/B **está** isolado ao componente UVC, e a variável que mudou é **exclusivamente `num_isoc_packets` (1 → 4)**. O tamanho por pacote ISOC permaneceu 3072 nos dois builds. Sua ressalva da seção 6 do briefing (de que outras diferenças poderiam existir) permanece válida como método, mas o mecanismo do driver está documentado linha a linha.

**O que isso te dá de graça:** `urb_size` é um parâmetro público (`stream_config->advanced.urb_size`). Setar `urb_size = 3072` na 2.5.1 reproduz **exatamente** a geometria da 2.4.2, sem downgrade, sem patch. Isso responde sua pergunta 18 com sim, e converte o A/B numa variável controlável. É o teste T3.

### 2.2. O PR #424 não corrigiu high-bandwidth — ele *habilitou* high-bandwidth

`[CÓDIGO]` Este é o ponto que reorganiza o diagnóstico. O diff de `26af107` (2026-02-23, liberado na 2.4.2):

```c
- uint8_t last_mult = UINT8_MAX; // Looking for minimum: init to max
  ...
- // Here we look for an interface that offers the largest MPS with minimum
- // multiple transactions in a microframe
- const uint16_t current_mps = USB_EP_DESC_GET_MPS(ep_desc);
- const uint8_t current_mult = USB_EP_DESC_GET_MULT(ep_desc);
- if (current_mps >= last_mps && current_mult <= last_mult && current_mps <= dwMaxPayloadTransferSize) {
+ // Here we look for an interface that offers the largest MPS that is not larger than requested MPS
+ const uint8_t packets_per_frame = USB_EP_DESC_GET_MULT(ep_desc) + 1;
+ const uint16_t current_mps = USB_EP_DESC_GET_MPS(ep_desc) * packets_per_frame;
+ if (current_mps >= last_mps && current_mps <= dwMaxPayloadTransferSize) {
```

A lógica antiga tinha um critério explícito `current_mult <= last_mult`, com `last_mult` inicializado em `UINT8_MAX` e estreitando a cada match. `[INFERÊNCIA]` Numa câmera cujos alt settings sobem em banda efetiva (típico: 128/0, 256/0, 512/0, 1024/0, 1024/1, 1024/2), o alt de 1024 com MULT=0 fixa `last_mult = 0`, e os alts com MULT=1 e MULT=2 são reprovados pelo segundo termo. **O driver escolhia, por construção, o maior alt setting com MULT=0.**

O changelog da 2.4.2 descreve isso como *"Fixed incorrect selection of high-bandwidth ISOC endpoints"* — do ponto de vista da issue #279 (uma câmera que precisava de mais banda) era mesmo um bug. Mas para o seu caso o efeito é o inverso: **a 2.4.2 passou a selecionar um endpoint que o HCD não sabe operar.**

`[INFERÊNCIA — confiança média-alta]` Isto significa que a 2.4.1 e anteriores, com a sua câmera, provavelmente rodariam em MULT=0 / 1024 B/µframe. Não posso afirmar sem os descritores da NE-HD362 (teste T0) e sem executar (teste T1).

### 2.3. O alt setting é escolhido ANTES do Probe, com uma constante

`[CÓDIGO]` `uvc_host.c:595`:

```c
uvc_desc_get_streaming_intf_and_ep(cfg_desc, bInterfaceNumber, MAX_MPS_IN, &intf_desc, &ep_desc)
```

O terceiro parâmetro se chama `dwMaxPayloadTransferSize` na assinatura da função (`uvc_descriptor_parsing.c:69`), mas recebe a constante **`MAX_MPS_IN = 4096`** (`private_include/uvc_idf_version_priv.h:14`, para P4/S31). E `uvc_claim_interface()` roda **antes** de `uvc_host_stream_control_probe()` em `uvc_host_stream_open()` (`uvc_host.c:817` vs `:843`).

Consequências, todas verificadas:

- O `dwMaxPayloadTransferSize` que a câmera devolve no Probe/Commit **não influencia** a escolha do alt setting.
- O teto efetivo é sempre 4096 → o driver sempre pega o maior alt disponível ≤4096, ou seja o HB de 3072.
- **Não existe API pública para escolher o alt setting nem para limitar o MPS efetivo.** `MAX_MPS_IN` está em `private_include/`. Isso responde sua pergunta 15: não existe; qualquer controle exige patch local ou downgrade de versão.
- `[INFERÊNCIA]` O valor 3072 do Commit é, portanto, **produzido pela negociação com a câmera, mas não consumido pelo driver na seleção**. Se o teto passado fosse 1024, o driver escolheria um alt menor independentemente do que a câmera commitou — que é precisamente o que o autor da #538 fez.

### 2.4. `ESP_ERR_INVALID_STATE` na issue #538 não é rejeição de tamanho

`[CÓDIGO]` Em `hcd_dwc.c` existem **exatamente duas** origens desse código de erro no caminho de submissão:

- `:2815` — `HCD_CHECK(urb->hcd_ptr == NULL && urb->hcd_var == URB_HCD_STATE_IDLE, ESP_ERR_INVALID_STATE)` → URB já enfileirado ou ainda não coletado.
- `:2830` — `_check_port_pipe_state()` falha → tipicamente **pipe em HALTED com a porta ENABLED**, ou porta em RECOVERY/SUSPENDED.

Rejeição por geometria daria outros códigos:

- `:2819` → `ESP_ERR_INVALID_SIZE` se `num_isoc_packets × interval > 64`.
- `usbh.c:285/299/303` → `ESP_ERR_INVALID_ARG` se `num_isoc_packets == 0`, se `num_bytes != Σ isoc_packet_desc[].num_bytes`, ou se algum `isoc_packet_desc[i].num_bytes % mps != 0` (com `mps` = MPS **base**, 1024).

`[INFERÊNCIA]` O `ESP_ERR_INVALID_STATE` reportado na #538 é quase certamente **"pipe halted por erro e nunca reiniciado"** — um bug de recuperação, não uma incompatibilidade de tamanho. Isso é importante porque significa que o workaround de `MAX_MPS_IN=1024` pode ter falhado por um motivo **secundário e corrigível**, não por impossibilidade do regime MULT=0. Sua restrição está preservada: continuo **não** classificando `MAX_MPS_IN=1024` como solução funcional.

### 2.5. Existe um commit não liberado, posterior à 2.5.1, diretamente sobre o bit ERR

`[CÓDIGO]` Commit `c04d1c7` (zhouli, 2026-06-10), em `[Unreleased]` no CHANGELOG — **não está na 2.5.1**:

- Adiciona `CONFIG_UVC_CHECK_PAYLOAD_HEADER_ERR` (default `y`), que permite **desligar** o descarte de frame quando o bit ERR está setado.
- Em `uvc_isoc.c`, pacotes **vazios** com header inválido deixam de descartar o frame corrente:

```c
- uvc_stream->single_thread.skip_current_frame = true;
+ if (isoc_desc->actual_num_bytes != 0) {
+     // Only skip the frame if we received some data
+     uvc_stream->single_thread.skip_current_frame = true;
+ }
```

`[INFERÊNCIA]` Isso é altamente relevante ao seu sintoma da 2.4.2 (maioria dos payloads com ERR, um frame íntegro escapando). Ainda assim: **desligar a checagem de ERR não faz o host receber as transações perdidas** — ela apenas para de descartar frames parciais. Serve como diagnóstico e possivelmente para MJPEG tolerante a perdas, não como correção de transporte.

---

## 3. Tabela de bugs

| # | Sintoma | Camada | Versão afetada | Correção | Evidência de sucesso | Link |
|---|---|---|---|---|---|---|
| B1 | MULT descartado; HCCHAR.EC nunca programado → host só executa 1 de 3 transações por microframe | HAL/HCD (`espressif/usb`, `esp_hal_usb`) | v6.0.2 **e master**; todos os targets | **Nenhuma** | `[NÃO ENCONTRADO]` — zero patches públicos | `usb_dwc_struct.h:537`; `hcd_dwc.c:1907` |
| B2 | Seleção do alt setting HB expõe B1 em câmeras que antes rodavam em MULT=0 | Class driver UVC | ≥2.4.2 (regressão funcional para HB-capable cams) | Reverter `26af107` ou usar ≤2.4.1 | `[NÃO TESTADO]` na sua bancada | [PR #424](https://github.com/espressif/esp-usb/pull/424) |
| B3 | `urb_size` clamp invertido → URB de 1 pacote em vez de N | Class driver UVC | ≤2.4.2 | PR #450 (`ae0641c`) → 2.5.0 | Merged pela Espressif, ticket IEC-505 | [PR #450](https://github.com/espressif/esp-usb/pull/450) |
| B4 | URBs absurdamente grandes → OOM | Class driver UVC | 2.5.0 | `5ffef02` → 2.5.1 | Changelog 2.5.1 | esp-usb `5ffef02` |
| B5 | Alt setting escolhido antes do Probe, com teto fixo 4096; sem API pública para MULT/alt | Class driver UVC | 2.4.2 … master | **Nenhuma** | — | `uvc_host.c:595` |
| B6 | Bit ERR / header inválido em pacote vazio descarta frame inteiro | Class driver UVC | ≤2.5.1 | `c04d1c7` — **Unreleased**, só em master | `[NÃO VERIFICADO]` | esp-usb `c04d1c7` |
| B7 | Pipe HALTED não reiniciado → `ESP_ERR_INVALID_STATE` na resubmissão | HCD + class driver | ≥2.5.1 | `[NÃO ENCONTRADO]` | `[AUTOR-ISSUE]` #538 | `hcd_dwc.c:2830` |
| B8 | `usb_dwc_ll_qtd_set_in()` escreve QTD ISO IN pela view `in_non_iso`: `eol` no bit 26, que é reservado no layout ISO | HAL | v6.0.2 e master | `[NÃO ENCONTRADO]` | — | `usb_dwc_ll.h:120-167, 990-1000` |
| B9 | Doc oficial afirma suporte a HB ISOC sem que o código o implemente | Documentação | v5.2 … master | `[NÃO ENCONTRADO]` | Commit `82020e6ff41f` é **só de docs** | `usb_host.rst` |

Sobre B8, para calibrar: `3072 = 0xC00` cabe nos 12 bits do campo ISO, e `set_in()` zera `buffer_status_val` antes de escrever, então a leitura de `rem_len` por 17 bits provavelmente devolve valor correto. A anomalia real é o bit `eol` (26) caindo em `reserved_26_27` do layout ISO — o descritor Host ISO IN do DWC_OTG não tem bit "last"; o fim da lista vem de `HCTSIZ.NTD`. `[HIPÓTESE]` Efeito desconhecido. Note que `eol` é setado em exatamente um QTD tanto no regime de 1 pacote quanto no de 4, então **B8 não discrimina o A/B** — é um achado separado.

---

## 4. Avaliação das hipóteses

### H1 — HCCHAR.EC/MC não programado é a causa raiz da perda de 2/3 das transações

**Favorável:**
- `[CÓDIGO]` Campo existe no P4 na posição canônica MC/EC (bits 21:20) e nunca recebe escrita, em nenhum target, em nenhuma versão.
- `[CÓDIGO]` MULT descartado em `hcd_dwc.c:1907`.
- `[CÓDIGO]` No Linux, `chan->multi_count = qh->maxp_mult` (`hcd.c:2653-2659`) e `HCCHAR.MULTICNT` é escrito **inclusive em Descriptor DMA** (`hcd.c:1449-1451`). O modo descritor não dispensa o MC.
- `[CÓDIGO]` O Linux define `MAX_ISOC_XFER_SIZE_HS = 3072` (`hcd_ddma.c:507`) — o modo descritor foi projetado para HB, e ainda assim programa MC.
- `[BANCADA]` Frames majoritariamente com ERR e um MJPEG íntegro ocasional é consistente com "o host recebe 1 de 3 transações": a maioria dos payloads chega truncada, e um frame pequeno o bastante para caber nas transações capturadas passa íntegro.
- `[AUTOR-ISSUE]` #538 relata o mesmo padrão (missed EoF, frame error) com outra câmera HB.

**Contrária:**
- `[DOC]` A Espressif afirma explicitamente *"Supports High-Bandwidth Isochronous endpoints"* para o P4. Contrapeso: `[CÓDIGO]` essa linha entrou pelo commit `82020e6ff41f` (2024-08-15), **exclusivamente de documentação**, sem nenhuma alteração de HAL ou HCD.
- Nenhuma evidência direta de leitura do registrador em runtime na sua bancada.

**Teste discriminante:** T4 (dump de HCCHAR durante streaming). Ler `hcchar_reg.val` do canal ISOC e verificar bits 21:20. Se forem 0 com um endpoint de MULT=2, H1 sai de hipótese e vira fato observado.

**Confiança: ALTA** quanto ao fato de código. **MÉDIA-ALTA** quanto à causalidade — falta apenas a leitura do registrador e/ou o teste de escrita.

### H2 — O silêncio total da 2.5.1 é causado por `num_isoc_packets = 4`

**Favorável:**
- `[CÓDIGO]` É comprovadamente a única variável que mudou entre os dois builds.
- `[BANCADA]` Correlação perfeita: 1 pacote → milhares de callbacks; 4 pacotes → zero.

**Contrária:**
- `[CÓDIGO]` Nenhuma validação em `hcd_dwc.c` ou `usbh.c` rejeita a geometria de 4×3072: `4 × 1 = 4 ≤ 64`; `12288 = 4 × 3072`; `3072 % 1024 == 0`. Passa em todas as checagens.
- Zero erros reportados também é estranho — uma rejeição daria código de erro na submissão, não silêncio.

**Teste discriminante:** T3 (`urb_size = 3072` na 2.5.1). Se os callbacks voltarem, H2 confirmada e o A/B fica sob seu controle. Se continuar silêncio, o A/B **não** era a versão do componente e há uma diferença de build ainda não identificada — o que reabre a ressalva da sua seção 6.

**Confiança: MÉDIA.** Correlação forte, mecanismo não estabelecido. Este é o maior buraco do relatório.

### H3 — O produto pode operar em MULT=0 (1024 B/µframe) sem tocar no HCD

**Favorável:**
- `[CÓDIGO]` Até a 2.4.1 o driver **escolhia** MULT=0 por critério explícito (`current_mult <= last_mult`).
- Aritmética: 1024 B/µframe × 8000 µframes/s = **8 MB/s**. Preview 800×600 MJPEG @5 fps ≈ 32 KB × 5 = **160 KB/s** — 50× de folga. Foto única 3264×2448 MJPEG ≈ 1,5–3 MB → **0,2–0,4 s** de transferência. Confortável.
- `[CÓDIGO]` Regime MULT=0 não usa HCCHAR.EC — é o caminho já exercitado por qualquer câmera não-HB.

**Contrária:**
- `[AUTOR-ISSUE]` Na #538, forçar 1024 eliminou os erros mas o streaming não funcionou (`ESP_ERR_INVALID_STATE`). Contrapeso: `[INFERÊNCIA]` esse erro é pipe-halted, provavelmente causa distinta (B7).
- `[NÃO ENCONTRADO]` Não sei se a NE-HD362 oferece alt setting com MULT=0 e MPS alto. Se o menor alt for 512/MULT=0, a banda cai para 4 MB/s — ainda suficiente, mas convém saber.
- `[HIPÓTESE]` A câmera pode recusar streaming num alt setting menor que o `dwMaxPayloadTransferSize` que ela commitou. Não conformidade é comum.

**Teste discriminante:** T0 (descritores) + T1 (downgrade para 2.4.1).

**Confiança: MÉDIA-ALTA** de que é o caminho certo; **MÉDIA** de que funciona sem patch adicional.

### H4 — Payload UVC não conforme explica os erros da 2.4.2

**Favorável:**
- `[CÓDIGO]` Existe commit não liberado (`c04d1c7`) tratando exatamente header inválido em pacote vazio e tornando a checagem de ERR opcional.
- `[CÓDIGO]` A 2.4.1 já tinha adicionado checagem de SOI e de payload header — checagens novas geram descartes novos.

**Contrária:**
- Não explica o silêncio da 2.5.1 (sua própria observação da seção D.14 está correta).
- `[BANCADA]` Um frame chegou íntegro com CRC validado. Uma câmera cronicamente não conforme raramente produz um frame perfeito.

**Confiança: BAIXA-MÉDIA** como causa primária. **ALTA** como fator agravante que mascara diagnóstico.

### H5 — Limitação de silício do P4 impede HB ISOC

**Favorável:** nenhuma evidência positiva.

**Contrária:**
- `[NÃO ENCONTRADO]` Nenhum erratum de USB OTG HS no ESP32-P4 (lista completa: RMT-176, I2C-308, MSPI-749/750/751, ROM-764/770/816, Analog-765, DMA-767, APM-560, ECDSA_DS-836/837).
- `[CÓDIGO]` O campo `ec` está exposto na struct do P4 — a Espressif mapeou o registrador.
- `[DOC]` A doc declara suporte.

**Teste discriminante:** ler `GHWCFG2[18]` (PerioEpSupported), `GHWCFG3[3:0]` (XferSizeWidth → `max_transfer_size` precisa ser ≥3072) e `GHWCFG3[6:4]` (PktSizeWidth → `max_packet_count` ≥3) em runtime. Este é o teste T5.

**Confiança: BAIXA.** Improvável, mas barato de descartar.

### H6 — Alimentação/VBUS insuficiente

`[NÃO PESQUISADO]` — frente interrompida. Contra-indicação lógica: `[BANCADA]` a enumeração HS completa, o Probe/Commit conclui, e um frame válido já foi entregue. Brownout tipicamente produz reset ou re-enumeração, não silêncio limpo pós-Commit com zero erros.

**Confiança: BAIXA** como causa do silêncio. Merece verificação como fator de estabilidade.

---

## 5. Onde a hipótese "S/G DMA dispensa o MC" morre

Vale isolar isto, porque é o único argumento técnico plausível a favor do código atual da Espressif — e ele não sobrevive ao confronto com o upstream.

`[DOC]` As *Maintainers Notes* da Espressif dizem, literalmente:

> "HS USB allows an isochronous endpoint to have three isochronous transactions in a single microframe."

> "Each filled QTD must represent a single transaction instead of the entire transfer."

`[CÓDIGO]` Mas `_buffer_fill_isoc()` (`hcd_dwc.c:2377-2383`) coloca `isoc_packet_desc[i].num_bytes` inteiro — 3072, ou seja **três transações** — num único QTD. A implementação contradiz a própria nota de manutenção.

`[CÓDIGO]` E o Linux, em Descriptor DMA, faz as duas coisas: um descritor por microframe **e** `HCCHAR.MULTICNT = maxp_mult`:

```c
/* dwc2_hc_start_transfer_ddma(), drivers/usb/dwc2/hcd.c:1449-1451 */
hcchar &= ~HCCHAR_MULTICNT_MASK;
hcchar |= chan->multi_count << HCCHAR_MULTICNT_SHIFT & HCCHAR_MULTICNT_MASK;
```

**O modo descritor não substitui o multi-count.** Ele descreve *onde* colocar os bytes; o MC descreve *quantas transações emitir* naquele microframe.

`[CÓDIGO]` Há um segundo detalhe que qualquer patch precisará respeitar, e que é fácil de esquecer: o **PID inicial**. Em ISOC IN HS, `dwc2_set_pid_isoc()` (`hcd.c:1057-1077`) programa DATA0 para MC=1, DATA1 para MC=2, DATA2 para MC=3 — com encoding não-linear no registrador (`DATA0=0, DATA2=1, DATA1=2, MDATA=3`, `hw.h:798-801`). Programar MC=3 com PID DATA0 desincroniza o device. E `HCCHAR.MPS` recebe **1024**, jamais 3072 — o campo tem 11 bits.

---

## 6. Soluções

| # | Solução | Já funcionou? | Evidência | Esforço | Risco | Adequação ao produto |
|---|---|---|---|---|---|---|
| S1 | `urb_size = 3072` na 2.5.1 (regime de 1 pacote) | Parcial `[BANCADA]` — reproduz o estado "muitos erros, 1 frame íntegro" | `[CÓDIGO]` geometria idêntica à 2.4.2 | Trivial (1 linha) | Nulo | Diagnóstico, não solução |
| S2 | Downgrade para usb_host_uvc **2.4.1** → seleção automática de MULT=0 | `[NÃO TESTADO]` | `[CÓDIGO]` diff `26af107` | Baixo | Baixo — perde correções de 2.4.2–2.5.1 | **Candidata a solução real.** 8 MB/s cobre o produto |
| S3 | Patch de 1 linha: reverter o critério de MULT na 2.5.1, mantendo o resto | `[NÃO TESTADO]` | `[CÓDIGO]` diff `26af107` | Baixo | Baixo | **Melhor variante de S2** — mantém correções recentes |
| S4 | `MAX_MPS_IN = 1024` | **Não** | `[AUTOR-ISSUE]` #538: erros somem, streaming não funciona (`INVALID_STATE`) | Baixo | Médio | Não classificar como solução |
| S5 | Patch HCCHAR.EC/MC + PID inicial no HAL/HCD | `[NÃO ENCONTRADO]` — nenhum patch público | `[CÓDIGO]` mapa completo do Linux disponível | Médio-alto | Médio — mexe no HAL para todos os periféricos USB | Solução correta a longo prazo; boa candidata a contribuição upstream |
| S6 | `usb_stream` (ESP-IoT-Solution) | **Não aplicável** | `[DOC]` "*This component is no longer maintained*"; registry lista só ESP32-S2/S3; limite de 512 B; S2/S3 são Full-Speed | — | — | **Descartado.** Responde suas perguntas 22–25 |
| S7 | Trocar por câmera UVC BULK | `[NÃO PESQUISADO]` | frente interrompida | Médio | Baixo | Provável plano B sólido |
| S8 | Trocar por câmera ISOC MULT=0 | `[NÃO PESQUISADO]` | frente interrompida | Médio | Baixo | Alternativa se a NE-HD362 não tiver alt MULT=0 |
| S9 | Migrar para SBC Linux | Funciona `[FATO]` — a câmera roda em macOS/Windows/Android | — | Alto | Baixo | Último recurso, conforme seu briefing |
| S10 | `CONFIG_UVC_CHECK_PAYLOAD_HEADER_ERR=n` (só em master) | `[NÃO TESTADO]` | `[CÓDIGO]` `c04d1c7` | Trivial | **Alto** — entrega frames corrompidos silenciosamente | Inaceitável para OCR de manuscrito infantil. Só diagnóstico |

Sobre S10 e sua pergunta 13: `CONFIG_UVC_CHECK_PAYLOAD_HEADER_EOH=n` e o novo `..._ERR=n` desligam **validações**, não corrigem transporte. O risco concreto no seu produto é entregar ao backend um JPEG com blocos faltando que ainda abre — e degrada silenciosamente o reconhecimento da caligrafia. Use como instrumento de diagnóstico, nunca em produção.

---

## 7. Plano de testes

Ordenado por (informação obtida) ÷ (risco × esforço). Nenhum destes testes escreve na flash além do firmware normal, e nenhum toca NVS ou tabela de partições.

### T0 — Dump completo dos descritores da câmera
- **Alteração:** `CONFIG_UVC_PRINTF_CONFIGURATION_DESCRIPTOR=y`, ou chamar `uvc_host_desc_print()`. Em paralelo: `lsusb -v -d VID:PID` num Linux com a mesma câmera.
- **Esperado:** lista de todos os alt settings da interface VideoStreaming com `bmAttributes`, `wMaxPacketSize` e a linha `wMaxPacketSize (with additional transactions in microframe)` (`usb_helpers.c:239`).
- **PASS** (existe alt com MULT=0 e MPS ≥512): H3 fica viável; siga para T1.
- **FAIL** (só existem alts com MULT>0): H3 morre; vá direto para S5 ou S7/S8.
- **Elimina:** a incerteza central sobre a viabilidade do regime MULT=0.
- **Risco:** nulo.

### T1 — Downgrade para usb_host_uvc 2.4.1
- **Alteração:** fixar `espressif/usb_host_uvc: "2.4.1"` no `idf_component.yml`. Nada mais.
- **Esperado:** o driver seleciona o alt de MULT=0; log `Allocating N USB transfers ... M MPS` deve mostrar **MPS ≤ 1024**.
- **PASS** (frames válidos a 800×600): **H1 confirmada por exclusão** e você tem produto. B1 continua sendo bug upstream, mas deixa de ser bloqueante.
- **FAIL** (silêncio ou erros mesmo em MULT=0): H1 enfraquece muito; o problema não é multi-count e sim algo no caminho ISOC do HCD. Reavaliar tudo.
- **Elimina:** H1 vs "problema genérico de ISOC no P4". É o teste mais informativo do conjunto.
- **Risco:** baixo — perde as correções de 2.4.2–2.5.1, inclusive B3/B4. Se der OOM, é a regressão conhecida, não um bug novo.

### T2 — Patch de 1 linha na 2.5.1 (variante preferida de T1)
- **Alteração:** em `uvc_descriptor_parsing.c:98-100`, restaurar o critério de MULT mínimo:
  ```c
  const uint16_t current_mps = USB_EP_DESC_GET_MPS(ep_desc);
  const uint8_t  current_mult = USB_EP_DESC_GET_MULT(ep_desc);
  if (current_mps >= last_mps && current_mult <= last_mult && current_mps <= dwMaxPayloadTransferSize) { ... }
  ```
  (reintroduzindo `uint8_t last_mult = UINT8_MAX;` e `last_mult = current_mult;`)
- **Esperado:** idêntico a T1, mas mantendo todas as correções da 2.5.x.
- **PASS/FAIL:** mesma interpretação de T1.
- **Risco:** baixo. É um revert cirúrgico de um diff de 8 linhas.

### T3 — `urb_size = 3072` na 2.5.1 (fechar o A/B)
- **Alteração:** `stream_config.advanced.urb_size = 3072;`
- **Esperado:** logs mostram `3072 bytes, 1 ISOC packets` — geometria idêntica à 2.4.2.
- **PASS** (voltam os callbacks e os erros): H2 confirmada; o A/B era `num_isoc_packets`, sob seu controle.
- **FAIL** (silêncio persiste): o A/B **não** era a versão do componente. Reabre a ressalva da sua seção 6 — audite `espressif/usb`, Kconfig e sdkconfig entre os dois builds.
- **Elimina:** H2. Também valida se o "silêncio" é reprodutível ou intermitente.
- **Risco:** nulo.

### T4 — Ler HCCHAR do canal ISOC em runtime
- **Alteração:** durante streaming ativo, ler o registrador do canal e logar `val`, `mps`, `eptype`, `ec`. Base do host channel 0 = offset `0x500`, cada canal ocupa `0x20` (`usb_dwc_struct.h:1232`). Interessam os bits 21:20.
- **Esperado:** `ec == 0` e `mps == 1024` com endpoint MULT=2.
- **PASS** (`ec == 0`): H1 passa de inferência de código para **observação de hardware**. É a prova que falta.
- **FAIL** (`ec != 0`): algum caminho programa o campo e H1 cai — nesse caso a causa é outra.
- **Risco:** baixo (leitura). Cuidado apenas com o timing: leia de uma tarefa, não de ISR.

### T5 — Ler GHWCFG2/3/4 (descartar H5)
- **Alteração:** logar `GHWCFG2` (`0x048`), `GHWCFG3` (`0x04C`), `GHWCFG4` (`0x050`) e derivar:
  ```c
  bool perio_ep   = !!(hwcfg2 & BIT(18));
  u32  max_xfer   = (1u << (( hwcfg3        & 0xf) + 11)) - 1;  /* precisa >= 3072 */
  u32  max_pktcnt = (1u << (((hwcfg3 >> 4)  & 0x7) +  4)) - 1;  /* precisa >= 3    */
  bool desc_dma   = !!(hwcfg4 & BIT(30));
  ```
- **PASS** (`perio_ep=1`, `max_xfer ≥ 3072`, `max_pktcnt ≥ 3`): H5 descartada, o silício suporta HB.
- **FAIL:** limitação de hardware documentada — decide a favor de S7/S8/S9.
- **Risco:** nulo.

### T6 — Patch experimental de HCCHAR.EC (só depois de T4)
- **Alteração:** adicionar `mult` a `usb_dwc_hal_ep_char_t`, propagar `USB_EP_DESC_GET_MULT()+1` desde `hcd_dwc.c:1907`, criar `usb_dwc_ll_hcchar_set_mc()`, escrever `ec = mult` no momento da ativação do canal, e programar o PID inicial conforme `dwc2_set_pid_isoc`. Manter `HCCHAR.MPS = 1024`. Rejeitar `mult == 4`.
- **Esperado:** frames válidos a 3072 B/µframe.
- **PASS:** causa provada, e você tem uma contribuição upstream de valor real.
- **FAIL:** revisar QTD/PID/FIFO — em particular RxFIFO ≥ 516 + n_channels words, conforme `dwc2/hcd.c:216-220`.
- **Risco:** **médio.** Toca o HAL compartilhado por todo o subsistema USB. Faça em branch isolado e valide regressões com um pendrive (bulk) e um teclado (interrupt).

**Ordem recomendada:** T0 → T3 → T1/T2 → T4 → T5 → T6. Se T1 ou T2 der PASS, pare: você tem produto, e T4/T6 viram trabalho de contribuição upstream, não de desbloqueio.

---

## 8. Ausências — e o que ainda preciso pesquisar

### 8.1. Frentes interrompidas (não executadas)

| Frente | O que cobriria | Perguntas do seu briefing que ficam em aberto |
|---|---|---|
| **Issue #538 na íntegra** | Texto literal do autor, respostas de mantenedores Espressif, tickets internos IEC/IDF, medições publicadas | Seção 8 do briefing; confirmar a leitura de B7 |
| **Análise commit-a-commit exaustiva** | Varredura completa de issues/PRs em esp-usb e esp-idf por "high bandwidth", "MULT", "HCCHAR", "missed EoF" | C.9–C.11 (cobri o essencial por código, mas não a varredura de issues) |
| **Câmeras UVC BULK** | Modelos 8–16 MP com endpoint BULK **comprovado por descritor** (ELP, Arducam, InnoMaker, HBV, e-con, IMX179/258/298/335), preços, disponibilidade, método de verificação pré-compra | **H.26–H.29 inteiras** |
| **Câmeras ISOC MULT=0** | Modelos de alta resolução com ≤1024 B/µframe e descritor publicado | **I.30–I.32 inteiras** |
| **Waveshare VBUS / hub** | Esquemático, chip de power switch, limite de corrente, power cycle por software, alimentação via USB-C UART; efeito real de hub HS alimentado | **J.33–J.35 inteiras** |

### 8.2. Perguntas do briefing sem resposta suficiente

- **A.4** — "Existe demonstração real de webcam usando 3 transações/µframe entregando frames no ESP32-P4?" → `[NÃO ENCONTRADO]`, e a ausência é significativa: nenhum vídeo, blog, thread ou repositório público. Isso é consistente com B1, mas não o prova.
- **A.1/A.3** — Nenhum patch, fork, branch ou commit que programe HCCHAR.EC/MC no ESP-IDF. `[NÃO ENCONTRADO]` em esp-idf, esp-usb, componente `usb`, notas de release 6.0.x/6.1/master.
- **K.36–K.37** — Método de captura comparativa (usbmon/Wireshark vs USBPcap vs logs instrumentados) não foi elaborado. É trabalho de bancada bem definido e de alto valor: comparar Probe, Commit, SET_INTERFACE, alt setting escolhido e a primeira submissão ISOC entre Linux e P4 responderia várias perguntas de uma vez.
- **Fórum esp32.com** — A thread ["Will the ESP32-P4 library support High speed USB for UVC camera?"](https://esp32.com/viewtopic.php?t=41337) foi identificada mas **não pôde ser lida** (o fórum serve um bot challenge por JavaScript). É a fonte mais provável de uma declaração oficial de engenheiro Espressif. Você consegue abrir isso no navegador em 1 minuto — vale a pena.
- **TRM do ESP32-P4** — A descrição literal do campo HCCHAR.EC (nome, bits, semântica) **não pôde ser obtida**: o PDF em `www.espressif.com` está bloqueado neste ambiente e a versão HTML retorna o sumário do Capítulo 47 sem o corpo dos registradores. Indício indireto: a própria doc Espressif remete ao *DWC_OTG Databook* da Synopsys (não público) e o header do P4 comenta "*Width depends on OTG_TRANS_COUNT_WIDTH (see databook)*".

### 8.3. Mecanismos não estabelecidos

1. **Por que 4 pacotes por URB produz silêncio absoluto.** Nenhuma validação em `hcd_dwc.c` ou `usbh.c` rejeita `4 × 3072`. Zero erros também é anômalo. **Este é o buraco número um do relatório**, e T3 é o teste que o ataca.
2. **Efeito real de B8** (bit `eol` gravado em posição reservada do QTD ISO IN). Não discrimina o A/B, mas é uma discrepância concreta entre a struct declarada e o código que a preenche.
3. **Se a NE-HD362 aceita streaming num alt setting menor que o `dwMaxPayloadTransferSize` commitado.** T0 + T1 respondem empiricamente.
4. **A causa do `ESP_ERR_INVALID_STATE` na #538.** Minha leitura (pipe halted) é `[INFERÊNCIA]` a partir do código, não confirmada pelo autor.

### 8.4. Restrições do briefing — conformidade

- Não recomendei `usb_device_uvc`. ✓
- Não recomendei apagar flash, nem atribuí nada a NVS ou tabela de partições. ✓
- Nenhuma mudança no backend HTTP. ✓
- HCCHAR.EC declarado como **hipótese de alta confiança**, não como causa provada. ✓
- `MAX_MPS_IN=1024` **não** classificado como funcional. ✓
- Eliminação de missed EoF **não** confundida com recepção de frames. ✓
- Nenhuma câmera BULK sugerida (a frente não foi executada; nenhuma sugestão sem descritor). ✓
- Não concluí que a câmera está defeituosa. ✓
- Onde não há solução comprovada, disse isso e indiquei o experimento de menor risco (T0 → T3 → T1/T2). ✓

---

## 9. Fontes

**Código (clones locais, fonte primária):**
- `espressif/esp-usb` @ `1fb14d3` — `host/class/uvc/usb_host_uvc/{uvc_host.c, uvc_descriptor_parsing.c, uvc_isoc.c, Kconfig, CHANGELOG.md}`, `host/usb/src/{hcd_dwc.c, usbh.c, usb_host.c, usb_helpers.c}`, `host/usb/include/usb/usb_types_ch9.h`
- `espressif/esp-idf` @ `v6.0.2` — `components/soc/esp32p4/include/soc/usb_dwc_struct.h`, `components/esp_hal_usb/esp32p4/include/hal/usb_dwc_ll.h`, `components/esp_hal_usb/{usb_dwc_hal.c, include/hal/usb_dwc_hal.h}`, `docs/en/api-reference/peripherals/usb_host*`
- `torvalds/linux` @ `3aa1dcaa4f6f` (v7.2.0-rc7) — `drivers/usb/dwc2/{hcd.c, hcd_queue.c, hcd_ddma.c, hcd_intr.c, hw.h, params.c, core.h}`, `drivers/usb/core/{config.c, urb.c}`, `include/uapi/linux/usb/ch9.h`

**Commits citados:** `26af107` (PR #424) · `8196b6e`/`ae0641c` (PR #450, IEC-505) · `5ffef02` · `c04d1c7` (Unreleased) · `ac3c5e6` (release 2.5.1) · `e35a018` (release 2.5.0) · `82020e6ff41f` (doc-only, esp-idf)

**Web:**
- [esp-usb issue #279 (IEC-394)](https://github.com/espressif/esp-usb/issues/279) · [PR #424](https://github.com/espressif/esp-usb/pull/424) · [PR #450](https://github.com/espressif/esp-usb/pull/450) · [issue #538](https://github.com/espressif/esp-usb/issues/538) *(não lida na íntegra)*
- [usb_host_uvc no Component Registry](https://components.espressif.com/components/espressif/usb_host_uvc) · [espressif/usb](https://components.espressif.com/components/espressif/usb)
- [USB Host — ESP-IDF v6.0.2 / ESP32-P4](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32p4/api-reference/peripherals/usb_host.html) · [Maintainers Notes (DWC_OTG)](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32p4/api-reference/peripherals/usb_host/usb_host_notes_dwc_otg.html)
- [usb_stream — descontinuado](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_host/usb_stream.html) · [registry](https://components.espressif.com/components/espressif/usb_stream)
- [ESP32-P4 Errata](https://docs.espressif.com/projects/esp-chip-errata/en/latest/esp32p4/index.html) · [ESP-FAQ USB](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/usb.html)
- [esp-idf#18235 — DWC OTG DMA vs EMAC no P4 v1.3](https://github.com/espressif/esp-idf/issues/18235) *(ruído de fundo a descartar se você usa Ethernet)*
- [esp32.com t=41337](https://esp32.com/viewtopic.php?t=41337) *(não acessível automaticamente)*
