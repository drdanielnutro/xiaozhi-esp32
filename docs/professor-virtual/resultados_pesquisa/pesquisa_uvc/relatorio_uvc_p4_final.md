# UVC Host ISOC high-bandwidth no ESP32-P4 — Relatório final

**Data:** 13 de agosto de 2026
**Escopo:** 6 de 6 frentes executadas. As lacunas remanescentes estão na seção 9.

**Convenções de evidência:**

| Marca | Significado |
|---|---|
| `[CÓDIGO]` | Verificado em clone real, com arquivo:linha e commit. Literal. |
| `[DOC]` | Documentação oficial, citação literal + link |
| `[SPEC]` | Especificação USB 2.0 / UVC, com seção |
| `[BANCADA]` | Sua observação, conforme relatada |
| `[AUTOR-ISSUE]` | Afirmação do autor de uma issue — não é prova |
| `[INFERÊNCIA]` | Dedução a partir de fatos verificados, com nível de confiança |
| `[HIPÓTESE]` | Não validado |
| `[NÃO ENCONTRADO]` | Procurado e não achado — a ausência é o dado |

**Fontes primárias:** `espressif/esp-usb` @ `1fb14d3` (master, 2026-08-07) · `espressif/esp-idf` @ `v6.0.2` · `torvalds/linux` @ `3aa1dcaa4f6f` (v7.2.0-rc7) · dumps de descritor commitados em `esp-usb/host/class/uvc/usb_host_uvc/host_test/main/parsing/descriptors/`

---

## 1. Sumário executivo

1. **Causa raiz:** a pilha USB Host do ESP-IDF nunca escreve `HCCHAR.EC` (multi-count) e descarta os bits MULT no HCD. O host emite **uma** transação por microframe num endpoint que exige três. `[CÓDIGO]`
2. **O mecanismo do silêncio está resolvido, e é uma segunda falha independente:** um único erro de canal deixa o pipe em `HALTED` para sempre, e o driver UVC responde a isso pausando o stream **sem emitir um único log**. Cadeia de 7 elos, toda verificada. `[CÓDIGO]`
3. **Por que 1 pacote/URB dá erros e 4 pacotes dá silêncio:** com 1 QTD o canal é re-armado a cada microframe, o que impede o contador de erros consecutivos de chegar a 3; com 4 QTDs o canal fica habilitado 4 microframes seguidos e dispara `XCS_XACT_ERR` → morte silenciosa. `[INFERÊNCIA — confiança alta]`
4. **Comprovado:** HCCHAR.EC nunca escrito; MULT descartado; Linux programa MULTICNT inclusive em DDMA; o A/B 2.4.2→2.5.1 é `num_isoc_packets` 1→4; até a 2.4.1 o driver escolhia MULT=0 de propósito.
5. **Incerto:** se a NE-HD362 tem alt setting MULT=0 utilizável; se o `ESP_ERR_INVALID_STATE` da #538 tem outra origem; o valor de `HCCHAR.EC` lido em runtime.
6. **Solução de menor risco:** forçar MULT=0 (`MAX_MPS_IN 1024` ou revert do critério de MULT) **+** corrigir a recuperação de pipe halted. As duas juntas — a primeira sozinha foi o que falhou na issue #538.
7. **Banda:** MULT=0 = 8,19 MB/s. Seu preview usa 0,70 MB/s (8,6%); uma foto de 3 MB leva 366 ms. Folga de ~12× no pior caso. **Seu produto não precisa de high-bandwidth.**
8. **Próximo teste:** T0 (dump de descritores, 10 min) → T1 (instrumentar retorno de `usb_host_transfer_submit`, 20 min). Os dois juntos fecham o diagnóstico.

---

## 2. A descoberta central: a cadeia de silêncio

Este é o achado que faltava no relatório parcial. Sua observação de **"zero frames, zero bytes, zero overflow, zero underflow, zero erros de transferência reportados, silêncio após Probe/Commit"** não é um mistério: é o comportamento projetado do código, e cada elo está verificado.

**Elo 1** — Erro de canal mata o pipe permanentemente. `[CÓDIGO hcd_dwc.c:953-963]`

```c
case USB_DWC_HAL_CHAN_EVENT_ERROR: {
    usb_dwc_hal_chan_error_t chan_error = usb_dwc_hal_chan_get_error(chan_obj);
    pipe->last_event = pipe_decode_error_event(chan_error);
    event = pipe->last_event;
    pipe->state = HCD_PIPE_STATE_HALTED;
    _buffer_done(pipe, stop_idx, pipe->last_event, false);
    _buffer_parse(pipe);
    break;    // NÃO chama _buffer_exec nem _buffer_fill
}
```

**Elo 2** — O erro é registrado no URB, mas **não nos pacotes ISOC**. `[CÓDIGO hcd_dwc.c:2678-2702]` `_buffer_parse_error()` escreve `transfer->actual_num_bytes = 0` e `transfer->status`, e **nada mais**. `isoc_packet_desc[i].status` fica com o valor anterior — tipicamente `0` = `USB_TRANSFER_STATUS_COMPLETED`.

**Elo 3** — A camada superior faz FLUSH, nunca CLEAR. `[CÓDIGO usb_host.c:1128-1135]`

```c
case USBH_EP_EVENT_ERROR_XFER:
case USBH_EP_EVENT_ERROR_URB_NOT_AVAIL:
case USBH_EP_EVENT_ERROR_OVERFLOW:
case USBH_EP_EVENT_ERROR_STALL:
    // The endpoint is now stalled. Flush all pending URBs
    ESP_ERROR_CHECK(usbh_ep_command(ep_wrap->constant.ep_hdl, USBH_EP_CMD_FLUSH));
    __attribute__((fallthrough));
```

Sem `HCD_PIPE_CMD_CLEAR`, o pipe **jamais** volta a `ACTIVE`. `[CÓDIGO]` Os únicos `HCD_PIPE_CMD_CLEAR` para endpoints não-default estão em `usbh.c:585` e `:643`, alcançáveis **apenas pelo handler de suspend/resume do root port** (`usbh.c:869/887`).

**Elo 4** — O FLUSH marca os pacotes como CANCELED. `[CÓDIGO hcd_dwc.c:1992-1997]`

**Elo 5** — O driver UVC pausa o stream em silêncio absoluto. `[CÓDIGO uvc_isoc.c:67-70]`

```c
case USB_TRANSFER_STATUS_NO_DEVICE:
case USB_TRANSFER_STATUS_CANCELED:
    ESP_ERROR_CHECK(uvc_host_stream_pause(uvc_stream)); // This should never fail
    return; // No need to process the rest
```

**Elo 6** — `uvc_host_stream_pause()` **não emite nenhum log**. `[CÓDIGO uvc_host.c:1060-1078]` Apenas `dynamic.streaming = false` e devolve o frame corrente.

**Elo 7** — A partir daí todo callback retorna na linha 53, e a ressubmissão nunca acontece. `[CÓDIGO uvc_isoc.c:53-55, 208-212]`

```c
if (!UVC_ATOMIC_LOAD(uvc_stream->dynamic.streaming)) {
    return; // If the streaming was turned off, we don't have to do anything
}
...
if (UVC_ATOMIC_LOAD(uvc_stream->dynamic.streaming)) {
    usb_host_transfer_submit(transfer); // Restart the transfer   <-- retorno IGNORADO
}
```

**Resultado:** um único erro de canal produz zero bytes, zero frames, **zero mensagens em nível W ou E**, e morte permanente do stream. `ESP_LOGD` é o único log no caminho, e está desligado por padrão.

Isto casa exatamente com sua bancada. **O "silêncio" da 2.5.1 não significa que nada aconteceu — significa que aconteceu um erro e o código o engoliu.**

### 2.1. Por que 1 pacote errava ruidosamente e 4 pacotes morre calado

`[CÓDIGO usb_dwc_hal.h:89]` `USB_DWC_HAL_CHAN_ERROR_XCS_XACT = 0, /**< Excessive (three consecutive) transaction errors ... */`
`[CÓDIGO usb_dwc_hal.c:67]` `- USB_DWC_LL_INTR_CHAN_XCS_XACT_ERR is always unmasked`

`[INFERÊNCIA — confiança alta]` Com `HCCHAR.EC = 0` e MPS=1024, um QTD pedindo 3072 B nunca é satisfeito → cada descritor termina com erro de transação.

- **1 QTD por URB:** o canal é desabilitado e re-habilitado a cada microframe (`_buffer_exec` roda por URB). O contador de erros consecutivos do core é resetado antes de chegar a 3. Resultado: conclusão por descritor com status ruim → **~8000 callbacks/s com erro** = suas "dezenas de milhares de operações". E um frame pequeno o bastante para caber nas transações capturadas escapa íntegro — seu MJPEG de 32.405 bytes.
- **4 QTDs por URB:** o canal fica continuamente habilitado por 4 microframes. Ao 3º erro consecutivo dispara `XCS_XACT_ERR` → `CHAN_EVENT_ERROR` → Elos 1–7 → **silêncio permanente**.

A variável estrutural que muda entre os dois regimes é exatamente **quantos microframes consecutivos o canal permanece habilitado**. Nada mais.

### 2.2. Diferença arquitetural com o Linux

`[CÓDIGO Linux hcd_ddma.c:849-850]` `/* Enable channel only once for ISOC */` — o Linux habilita o canal ISOC **uma vez por sessão** e o deixa rodando circularmente sobre 256 descritores.
`[CÓDIGO hcd_dwc.c:2511]` O ESP re-habilita o canal **a cada buffer de URB**.

`[CÓDIGO Linux hcd_ddma.c:814-815]` `/* For Isochronous endpoints the channel is not halted on XferComplete interrupt so remains assigned to the endpoint(QH) until session is done. */`
`[CÓDIGO usb_dwc_hal.c:564-570]` O HAL do ESP trata XFERCOMPL-sem-halt como caso raro de "short interrupt IN packet" e **halta o canal à força** — mas esse é o caminho **normal** de ISOC. O comentário está errado.

`[CÓDIGO hcd_dwc.c:2375 + usb_dwc_hal.h:90]` Além disso, `memset(buffer->xfer_desc_list, 0, 64 * sizeof(qtd))` deixa 60 dos 64 QTDs zerados, e um QTD inativo alcançado pelo core gera `USB_DWC_HAL_CHAN_ERROR_BNA` ("Buffer Not Available ... An inactive transfer descriptor was fetched by the channel"). O design depende de re-armar o canal dentro de uma janela de 125 µs. `[INFERÊNCIA]` É um campo minado que amplia a superfície de erro do regime de 4 pacotes.

---

## 3. Tabela de bugs

| # | Sintoma | Camada | Versão afetada | Correção | Evidência de sucesso | Link / referência |
|---|---|---|---|---|---|---|
| **B1** | MULT descartado; `HCCHAR.EC` nunca programado → host emite 1 de 3 transações/µframe | HAL/HCD | v6.0.2 **e master**, todos os targets | **Nenhuma** | `[NÃO ENCONTRADO]` — zero patches públicos | `usb_dwc_struct.h:537`; `hcd_dwc.c:1907` |
| **B2** | Erro de canal → pipe HALTED sem recuperação → `ESP_ERR_INVALID_STATE` permanente | HCD + usb_host | v6.0.2 e master | **Nenhuma** | — | `hcd_dwc.c:958`, `usb_host.c:1133` |
| **B3** | UVC pausa o stream em silêncio total ao receber CANCELED; retorno do resubmit descartado | Class driver UVC | ≤2.5.1 e master | **Nenhuma** | — | `uvc_isoc.c:67-70, 208-212`; `uvc_host.c:1060` |
| **B4** | `_buffer_parse_error` não preenche `isoc_packet_desc[].status` → erro invisível ao class driver | HCD | v6.0.2 e master | **Nenhuma** | — | `hcd_dwc.c:2678-2702` |
| **B5** | Seleção de alt setting HB expõe B1 em câmeras que antes rodavam em MULT=0 | Class driver UVC | ≥2.4.2 | Reverter `26af107` ou usar ≤2.4.1 | `[NÃO TESTADO]` | [PR #424](https://github.com/espressif/esp-usb/pull/424) |
| **B6** | Alt setting escolhido **antes** do Probe, com teto fixo `MAX_MPS_IN=4096`; sem API pública | Class driver UVC | 2.4.2 … master | **Nenhuma** | — | `uvc_host.c:595`; `uvc_idf_version_priv.h:14` |
| **B7** | `urb_size` clamp invertido → 1 pacote por URB | Class driver UVC | ≤2.4.2 | PR #450 (`ae0641c`) → 2.5.0 | Merged, ticket IEC-505 | [PR #450](https://github.com/espressif/esp-usb/pull/450) |
| **B8** | URBs absurdamente grandes → OOM | Class driver UVC | 2.5.0 | `5ffef02` → 2.5.1 | Changelog 2.5.1 | esp-usb `5ffef02` |
| **B9** | Bit ERR / header inválido em pacote vazio descarta frame | Class driver UVC | ≤2.5.1 | `c04d1c7` — **Unreleased**, só master | `[NÃO VERIFICADO]` | esp-usb `c04d1c7` |
| **B10** | `usb_dwc_ll_qtd_set_in()` escreve QTD ISO IN pela view `in_non_iso`: `eol` no bit 26, reservado no layout ISO. O Linux **nunca** seta EOL em ISO | HAL | v6.0.2 e master | **Nenhuma** | Linux `hcd_ddma.c:534-541` vs `:775-777` | `usb_dwc_ll.h:134-142, 990-1000` |
| **B11** | `timeout_ms` documentado mas **nunca lido** — zero refs em `host/usb/src/` | usb_host | todas | **Nenhuma** | `[DOC]` "Transfer timeouts are not supported yet" | `usb_types_stack.h:171` |
| **B12** | Doc oficial afirma suporte a HB ISOC sem que o código o implemente | Documentação | v5.2 … master | **Nenhuma** | Commit `82020e6ff41f` é **só de docs** | `usb_host.rst:33` |

---

## 4. Evidência externa: issues #538, #279 e #468

### #538 — [InnoMaker U20-16MP-AF, aberta 11/08/2026, IEC-580](https://github.com/espressif/esp-usb/issues/538)

Estado: **Open**, zero comentários, sem assignee, sem milestone. Autor: MistahZing. Waveshare ESP32-P4-WIFI6-**POE-ETH**, IDF v5.5, usb_host_uvc 2.5.1, câmera `364d:6200`.

Escada de alt settings da câmera dele `[CITAÇÃO VIA WEBFETCH]`:

| alt | wMaxPacketSize | MULT | decodificação |
|---|---|---|---|
| 1 | `0x0080` | 0 | 128 × 1 |
| 2 | `0x0100` | 0 | 256 × 1 |
| 3 | `0x0320` | 0 | **800 × 1 = 6,40 MB/s** |
| 4 | `0x0B20` | 1 | 800 × 2 |
| 5 | `0x1320` | 2 | 800 × 3 = 2400 |

Aritmética conferida: `0x1320 >> 11 & 3 = 2`; `0x1320 & 0x7FF = 800`. Correto.

Ele mediu **~499 B/µframe** de carga real a 1280×720 (≈32 Mbps) — o alt 3 sobraria. O driver escolhe o alt 5.

Logs `[CITAÇÃO VIA WEBFETCH]`: `missed EoF`, `frame error`, `invalid MJPEG SOI`, `Stream: Frame not received on time`, `Stream stop`. Depois de `MAX_MPS_IN 4096 → 1024`: *"completely eliminates 'missed EoF', 'frame error' and 'invalid MJPEG SOI' errors"* — mas então:

```
E USB HOST: Enqueue URB error: ESP_ERR_INVALID_STATE
E uvc: uvc_host_stream_unpause(1095): Could not submit transfer 0
E uvc: uvc_host_stream_start(976): Could not unpause the stream
```

`[INFERÊNCIA — confiança alta, e consistente com os logs do próprio autor]` A explicação está na cadeia da seção 2. A sequência é: primeira tentativa de stream → erro de canal → pipe HALTED → o exemplo loga `Frame not received on time` e faz `Stream stop` → o app tenta `Stream start` de novo → `usb_host_transfer_submit()` bate em `_check_port_pipe_state()` com porta ENABLED + pipe HALTED → `ESP_ERR_INVALID_STATE`. **O pipe nunca foi limpo, porque ninguém no caminho ISOC chama `usb_host_endpoint_clear()`** — `uvc_clear_endpoint_feature()` (`uvc_host.c:938-949`) só é usado no ramo **BULK** de `uvc_host_stream_stop()` (`uvc_host.c:1052-1056`).

**Importante e честно:** o autor atribui a causa ao **RX FIFO**, não a HCCHAR.EC. Ele nunca menciona HCCHAR, EC ou multi-count. Não lhe atribua a tese deste relatório. `[INFERÊNCIA]` A tese dele é provavelmente incorreta: se fosse FIFO, o alt 4 (2×800 = 1600 B, muito abaixo de qualquer limite plausível) funcionaria. Teste que separa as duas: forçar `MAX_MPS_IN = 1600`, habilitando o alt 4 (MULT=1). Se falhar igual, é EC/MULT e não FIFO.

### #279 — [a origem, IEC-394](https://github.com/espressif/esp-usb/issues/279)

Aberta 05/10/2025 por Dandjinh, fechada com `Resolution: Done`, milestone **usb_host_uvc 2.4.2**, assignee tore-espressif. Câmera MS2130 no P4: MJPEG a 30 fps com **só 1/3 decodificável**, e a frase *"Looks like only USB-FS bandwidth works."*

`[INFERÊNCIA]` "1/3 decodificável" é a assinatura exata de MULT=2 com 1 de 3 transações coletadas — **o mesmo fenômeno da sua bancada**. Mas foi lido como "falta de banda", e a correção aplicada (PR #424) foi *passar a escolher o alt de MULT maior*, o que não resolve a raiz e expõe mais gente ao B1.

### #468 — [MS2107, IEC-518 — fechada como Won't Do](https://github.com/espressif/esp-usb/issues/468)

Aberta 13/04/2026 por gabrago. **Waveshare ESP32-P4**, IDF 5.5.3, componente **2.4.2**. "frame error" persistente logo após abrir o stream. Fechada como **`Resolution: Won't Do`**, sem explicação técnica pública e sem PR de correção.

`[INFERÊNCIA]` É o terceiro caso do mesmo padrão, no mesmo hardware, na versão que introduziu o PR #424. O fechamento sem justificativa é um dado relevante para calibrar expectativa de suporte upstream.

`[NÃO ENCONTRADO]` Nenhuma issue em esp-usb ou esp-idf mencionando `HCCHAR`, `EC`, "multi count" ou "multicnt" além da #538. Nenhuma declaração pública de engenheiro da Espressif sobre HB ISOC estar na roadmap ou ter branch interno.

---

## 5. Avaliação das hipóteses

### H1 — `HCCHAR.EC` não programado é a causa raiz

**Favorável:** `[CÓDIGO]` campo existe (`usb_dwc_struct.h:537`, bits 21:20), zero escritas em toda a árvore, nenhum setter em nenhum target · MULT descartado em `hcd_dwc.c:1907` · no Linux `chan->multi_count = qh->maxp_mult` (`hcd.c:2653-2659`) e `HCCHAR.MULTICNT` é escrito **inclusive em Descriptor DMA** (`hcd.c:1449-1451`) · `MAX_ISOC_XFER_SIZE_HS = 3072` no Linux (`hcd_ddma.c:507`) prova que DDMA foi projetado para HB e ainda assim programa MC · `[BANCADA]` erros majoritários com 1 frame íntegro ocasional · `[INFERÊNCIA]` "1/3 decodificável" da #279 é a mesma assinatura.

**Contrária:** `[DOC]` a Espressif afirma *"Supports High-Bandwidth Isochronous endpoints"* — contrapeso: a linha entrou por commit **exclusivamente de documentação** (`82020e6ff41f`, 2024-08-15).

**Teste discriminante:** T2 (ler HCCHAR em runtime).
**Confiança: ALTA** quanto ao fato de código; **ALTA** quanto à causalidade (subiu de média-alta com a evidência convergente da #279 e do mecanismo XCS_XACT).

### H2 — O silêncio da 2.5.1 é a cadeia de morte silenciosa disparada por erro de canal

**Favorável:** `[CÓDIGO]` os 7 elos estão integralmente verificados · `[CÓDIGO]` `XCS_XACT_ERR` = "três erros consecutivos", sempre desmascarado · `[INFERÊNCIA]` a única variável estrutural entre 1 e 4 pacotes é o número de microframes consecutivos com o canal habilitado · `[BANCADA]` "zero erros reportados" é exatamente o que o código produz (nenhum log em W/E no caminho).

**Contrária:** nenhuma validação em `hcd_dwc.c`/`usbh.c` rejeita a geometria de 4×3072, então a falha é de runtime, não de submissão — coerente, mas não observado diretamente.

**Teste discriminante:** T1 (logar retorno de `usb_host_transfer_submit` em `uvc_isoc.c:211`). Se aparecer `ESP_ERR_INVALID_STATE` (0x103) N vezes e depois nada, H2 está confirmada.
**Confiança: ALTA.** Subiu de média — o mecanismo agora está estabelecido em código, não só correlacionado.

### H3 — O produto pode operar em MULT=0 sem tocar no HCD

**Favorável:** `[CÓDIGO]` até a 2.4.1 o driver escolhia MULT=0 por critério explícito · aritmética: 8,192 MB/s vs 0,70 MB/s do preview (8,6%) e 366 ms para uma foto de 3 MB · `[CÓDIGO]` **as 7 câmeras ISOC com descritor comprovado nos vetores de teste da própria Espressif têm degrau MULT=0**, entre 800 e 1024 B · `[AUTOR-ISSUE]` na #538, forçar 1024 eliminou todos os erros de frame.

**Contrária:** `[AUTOR-ISSUE]` na #538 o streaming ainda não funcionou — mas `[INFERÊNCIA]` por B2/B3 (pipe halted de tentativa anterior), causa distinta e corrigível · `[NÃO ENCONTRADO]` não sei se a NE-HD362 tem degrau MULT=0 · `[SPEC]` escolher alt com banda menor que o `dwMaxPayloadTransferSize` commitado é fora de conformidade UVC; a saída correta é escrever um `dwMaxPayloadTransferSize` reduzido no COMMIT (é o que o Linux faz em `UVC_QUIRK_FIX_BANDWIDTH`), e `uvc_control.c` **não faz isso**.

**Teste discriminante:** T0 + T3.
**Confiança: ALTA** de que é o caminho certo; **MÉDIA-ALTA** de que funciona combinando com a correção de B2/B3.

### H4 — Payload UVC não conforme

**Favorável:** `[CÓDIGO]` commit `c04d1c7` não liberado trata exatamente isso.
**Contrária:** não explica o silêncio; `[BANCADA]` um frame chegou perfeito com CRC validado.
**Confiança: BAIXA** como causa primária; **ALTA** como fator que mascara diagnóstico.

### H5 — Limitação de silício do P4

**Contrária:** `[NÃO ENCONTRADO]` nenhum erratum de USB OTG HS no P4 (lista completa: RMT-176, I2C-308, MSPI-749/750/751, ROM-764/770/816, Analog-765, DMA-767, APM-560, ECDSA_DS-836/837) · `[CÓDIGO]` o campo `ec` está mapeado na struct do P4.
**Teste discriminante:** T5 (ler GHWCFG2/3/4).
**Confiança: BAIXA.**

### H6 — Alimentação / VBUS

**Favorável:** `[FATO/DATASHEET]` a placa tem apenas resistores Rd de 5,1 k nas linhas CC — **não negocia nada**, simplesmente puxa. De porta USB de PC você tem 500 mA nominais, e LCD de 7" + backlight + P4 + C6 já consomem a maior parte. Câmera UVC HS com autofoco puxa 300–500 mA com picos no arranque do motor AF.

**Contrária:** `[BANCADA]` a enumeração HS completa, Probe/Commit conclui, e um frame válido já foi entregue. Brownout produz reset ou re-enumeração, não silêncio limpo pós-Commit. `[FATO]` o limitador `DIO7003HEST5` é de ~2 A e **não é o gargalo** — a fonte a montante é. `[INFERÊNCIA]` a queda interna da placa a 500 mA é <100 mV (37 mV no switch + ~25 mV no P-FET de OR-ing); o que derruba é o cabo USB-C e a porta hospedeira.

**Confiança: BAIXA** como causa do silêncio; **MÉDIA** como fator de estabilidade que vale eliminar.

---

## 6. Hardware: Waveshare e hub

### 6.1. Placa ESP32-P4-WIFI6-Touch-LCD-7B

`[FATO/ESQUEMÁTICO]` [PDF oficial](https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-7B/ESP32-P4-WIFI6-Touch-LCD-7B.pdf)

| Item | Achado |
|---|---|
| Power switch da USB-A | **`U2 = DIO7003HEST5`** ([datasheet](https://www.dioo.com/uploads/product/20210527/39809ef9c4aeae798493447426d5dd7c.pdf)). Sufixo `H`=EN ativo-alto, `E`=**2 A**, `ST5`=SOT23-5. R_DS(on) 75 mΩ |
| Resistor RSET | **Não existe.** O limite é fixo pelo part number. O switch **não é o gargalo** |
| Power cycle por software | `[INFERÊNCIA — confiança média-alta]` **Não.** Nenhum net `USB_EN`/`VBUS_EN` no esquemático; a alegação "EN←GPIO7" é refutada por GPIO7/8 serem SDA/SCL do touch. **Verificação de 2 min:** medir pino 4 do U2 com o P4 em reset |
| Origem do 5 V da USB-A | `[INFERÊNCIA — confiança média]` OR-ing por P-FETs `AO3401` + Schottky `B5819WS` das nets `USB0_5V`/`USB1_5V`/`Boost_5V` no trilho `Core_5V` → `U2` → `VBUS_OUT`. **Sem boost dedicado** — vem direto do Type-C |
| Chip UART | **`U6 = CH343P`** (não CP2102, não o C6) |
| Negociação de corrente | `[FATO]` só Rd de 5,1 k, **sem controlador CC/PD**. Não negocia |
| Roteamento USB | `[FATO]` **1 PHY HS** (pinos dedicados 49/50 do P4) → **USB-A `J1`**. **1 PHY FS** (GPIO24/25, `USB1P1_N/P`) → **Type-C `H2`**. **Sem mux** |
| Bulk cap no VBUS_OUT | `[NÃO CONFIRMADO]` — só `C1`/`C2` 100 µF identificados, atribuídos ao `Core_5V`. `[SPEC §7.2.4.1]` exige ≥120 µF por porta downstream. Mitigação: **soldar 100–220 µF low-ESR + 0,1 µF no VBUS_OUT junto ao J1** |
| Outras entradas | Type-C `H1`(UART), Type-C `H2`(FS), bateria MX1.25 `J4` (carregador ETA6098 + boost SCT12A0), headers PH2.0/12P expõem `Core_5V`. **Sem DC jack** |
| Demo UVC oficial | `[NÃO ENCONTRADO]` — o [repo oficial](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B) tem 14 exemplos, nenhum UVC. O único USB (`12_usb_extend_screen`) usa TinyUSB em modo **device**. A câmera suportada é OV5647 via MIPI-CSI |
| Relatos de brownout com câmera | `[NÃO ENCONTRADO]` em esp32.com, GitHub, Waveshare, Reddit. A ausência sugere que pouca gente usa UVC nesta placa |

**Recomendação de alimentação:** Type-C com fonte de 5 V / ≥2–3 A e cabo curto e grosso (não porta de PC), **ou** injetar 5 V regulado nos pinos `Core_5V` do header PH2.0/12P — isso salta o conector e o cabo, que é onde está a resistência série real.

### 6.2. Hub USB — o que resolve e o que não resolve

`[SPEC USB 2.0 §11.7 e §11.14]` Correção de numeração: TT é **§11.14**, Hub Repeater é **§11.7** (a numeração 11.17 é do rascunho 0.79).

> §11.1.1: *"When a high-speed device is attached on downstream facing port, the routing logic will connect the port to the hub repeater... When a full/low-speed device is attached ... must connect the port to the transaction translator."*

`[SPEC]` Com host HS + hub HS + device HS, o **TT não é instanciado** — o hub é apenas repetidor, bit a bit, no mesmo microframe.

**Corolário `[INFERÊNCIA de alta confiança]`: se o host emite 1 transação, o hub repete 1 transação. Nenhum hub converte 1 em 3.** A hipótese "o hub conserta o MULT" está encerrada.

| Aspecto | Veredito |
|---|---|
| Benefício elétrico | ✅ **Real e é o único.** Hub *self-powered* fornece 500 mA/porta (`[SPEC §7.2.1.1]`), tirando a câmera do orçamento do `Core_5V` e desacoplando o transiente do autofoco |
| Suporte a hubs no ESP-IDF | `[CÓDIGO esp-usb/host/usb/Kconfig:137,143]` `CONFIG_USB_HOST_HUBS_SUPPORTED` (**default n**) e `CONFIG_USB_HOST_HUB_MULTI_LEVEL`. Introduzido no **ESP-IDF v5.4**. `[CÓDIGO]` O exemplo oficial `basic_uvc_stream` **já vem com `HUBS_SUPPORTED=y`** |
| Transaction Translator | `[DOC + CÓDIGO hub.c:407-414]` **Não implementado** — o driver **rejeita ativamente** device FS/LS sob hub HS: `"transaction translator (TT) is not supported"` → `ext_hub_port_disable()`. E `ext_hub.c:1255`: `"Transaction Translator has not been implemented yet"` |
| Agendamento ISOC | `[SPEC §5.6.4]` banda periódica limitada a **80% de cada microframe**, e é do **barramento**, não da porta. Hub não cria banda; com mais devices periódicos, eles competem |
| MULT / high-bandwidth | ❌ **Nenhum efeito** |
| Overcurrent | `[DOC]` o driver de hub do IDF **não implementa** tratamento de erro, incluindo overcurrent |

**O que o hub NÃO resolve, explicitamente:** não faz o HCD programar `HCCHAR.MULTICNT`; não altera `MAX_MPS_IN`; não aumenta banda ISOC; não quebra 3072 B em 3×1024; não recupera pipe halted.

### 6.3. Plano C — downgrade para Full-Speed

| Regime | Payload/transação | Banda de 1 endpoint ISOC |
|---|---|---|
| FS ISOC | 1023 B / frame de 1 ms | **~1,0 MB/s** |
| HS ISOC MULT=0 | 1024 B / µframe de 125 µs | **8,19 MB/s** |
| HS ISOC MULT=2 | 3072 B / µframe | 24,58 MB/s |

Preview 800×600@5fps (~150–200 KB/s) usa 16–20% do orçamento FS — cabe. Foto de 1,5–3 MB leva **1,9–3,8 s** com eficiência realista. Aceitável sob comando, com indicador de progresso.

**Armadilha:** um **hub HS não faz downgrade** — só um hub **USB 1.1 / FS-only** força FS. Nesse caso `parent_speed != USB_SPEED_HIGH`, o teste de `hub.c:407` passa e nenhum TT é necessário. Mas depende de a câmera oferecer alt settings FS utilizáveis — muitas UVC HS com AF são HS-only.

`[CÓDIGO]` **Não existe API pública para forçar FS na porta raiz.** A velocidade é detectada (`hcd_dwc.c:862/1666` → `usb_dwc_hal_port_get_conn_speed()`), não imposta. `usb_host_config_t.peripheral_map` (`usb_host.h:143`) permite usar o controlador FS do P4, mas **nesta placa o PHY FS está fisicamente no Type-C `H2`, não na USB-A** — exigiria adaptador OTG e VBUS externo.

**Veredicto:** viável em banda, condicional em descritores, e inferior a simplesmente usar MULT=0 em HS (8,19 MB/s, ~12× o necessário).

---

## 7. Soluções

| # | Solução | Já funcionou? | Evidência | Esforço | Risco | Adequação ao produto |
|---|---|---|---|---|---|---|
| **S1** | `urb_size = 3072` na 2.5.1 (1 pacote/URB) | Parcial `[BANCADA]` | `[CÓDIGO]` geometria idêntica à 2.4.2 | Trivial | Nulo | **Diagnóstico**, não solução |
| **S2** | `MAX_MPS_IN 1024` **+** correção de recuperação de pipe (S4) | `[NÃO TESTADO]` na combinação | `[AUTOR-ISSUE]` #538: sozinho elimina todos os erros de frame, mas falha por B2/B3 | Baixo | Baixo | **Melhor candidata** |
| **S3** | Reverter o critério de MULT (`26af107`) na 2.5.1, ou usar 2.4.1 | `[NÃO TESTADO]` | `[CÓDIGO]` diff `26af107` | Baixo | Baixo | Equivalente a S2, mais "natural" |
| **S4** | Verificar retorno de `usb_host_transfer_submit` em `uvc_isoc.c:211` e chamar `usb_host_endpoint_clear()` no erro | `[NÃO TESTADO]` | `[CÓDIGO]` cadeia B2/B3 provada | Baixo | Baixo | **Necessária junto com S2/S3** |
| **S5** | `MAX_MPS_IN 1024` sozinho | **Não** | `[AUTOR-ISSUE]` #538 → `ESP_ERR_INVALID_STATE` | Baixo | Médio | **Não classificar como funcional** |
| **S6** | Patch `HCCHAR.EC` + PID inicial no HAL/HCD | `[NÃO ENCONTRADO]` patch público | `[CÓDIGO]` mapa completo do Linux disponível (seção 8) | Médio-alto | Médio | Correta a longo prazo; boa contribuição upstream |
| **S7** | `usb_stream` (ESP-IoT-Solution) | **Não aplicável** | `[DOC]` descontinuado; só S2/S3; Full-Speed; MPS ≤512 | — | — | **Descartado** |
| **S8** | Hub USB alimentado | Não para o ISOC | `[SPEC §11.7/11.14]` | Baixo | Baixo | Resolve **só** o VBUS. Útil como higiene, não como correção |
| **S9** | Trocar por câmera UVC BULK | `[SEM COMPROVAÇÃO]` de descritor em nenhum módulo ≥8 MP | inferência de banda | Médio | Baixo | Plano B — exige comprar amostra e verificar |
| **S10** | Trocar por câmera ISOC com degrau MULT=0 | `[CÓDIGO]` degraus MULT=0 comprovados em 7 câmeras | vetores de teste da Espressif | Médio | Baixo | Plano B alternativo |
| **S11** | Migrar para SBC Linux | Funciona `[FATO]` — a câmera roda em macOS/Windows/Android | — | Alto | Baixo | Último recurso |
| **S12** | `CONFIG_UVC_CHECK_PAYLOAD_HEADER_ERR=n` (só master) | `[NÃO TESTADO]` | `[CÓDIGO]` `c04d1c7` | Trivial | **Alto** | **Inaceitável em produção** — entrega JPEG corrompido silenciosamente, degradando o reconhecimento da caligrafia. Só diagnóstico |

---

## 8. Câmeras

### 8.1. Achado que muda a decisão de compra

`[CÓDIGO]` **Still Image Capture do UVC não é suportado pelo componente.** Os enums `UVC_VS_STILL_PROBE_CONTROL` (0x03), `UVC_VS_STILL_COMMIT_CONTROL` (0x04) e `UVC_VS_STILL_IMAGE_TRIGGER_CONTROL` (0x05) existem em `private_include/usb_types_uvc.h:47-49` mas **nenhum arquivo `.c` os referencia**. `bStillCaptureMethod` e `uvc_still_image_frame_desc_t` aparecem só em `printf`. Zero ocorrências de "still" nos headers públicos.

E, nas câmeras com descritor comprovado, o Still Image **nunca** entrega resolução acima do stream (Canyon: idêntico; Anker C200: still 1080p vs stream 1440p, ou seja **menor**; as duas ELP: `bStillCaptureMethod = 0x00`, não implementam).

**Conclusão: a foto sai obrigatoriamente do stream normal.** O caminho é abrir em 800×600 para preview e usar `uvc_host_stream_format_select()` (disponível desde 2.2.0) para trocar para 2592×1944 ou 3264×2448 a 1–3 fps, capturar, e voltar.

### 8.2. Descritores comprovados

`[CÓDIGO]` Fonte de primeira mão: dumps do USB Device Tree Viewer commitados pela própria Espressif em `esp-usb/host/class/uvc/usb_host_uvc/host_test/main/parsing/descriptors/`. Decodificados:

| Dispositivo | VID:PID | Transporte | Melhor degrau MULT=0 |
|---|---|---|---|
| ELP/Ailipu (H.265) | `32E4:9415` | ISOC | **`0x0400` = 1024 B = 8,19 MB/s** |
| Anker PowerConf C200 | `291A:3369` | ISOC | `0x03FF` = 1023 B = 8,18 MB/s |
| Logitech C270 / StreamCam | `046D:0825/0893` | ISOC | `0x03B0` = 944 B = 7,55 MB/s |
| ELP/Ailipu (H.264) | `32E4:9422` | ISOC | `0x0320` = 800 B = 6,40 MB/s |
| Sonix SN9C (Trust/Canyon) | `0C45:6340` | ISOC | `0x0320` = 800 B = 6,40 MB/s |
| Rockchip UVC gadget | `2207:0018` | ISOC | `0x03FF` = 1023 B |
| Genérica "HD video" | `349C:3307` | **BULK** | alt único, `0x0040` — mas **Full-Speed**, só 720p |

**As 7 câmeras ISOC examinadas têm degrau MULT=0, entre 800 e 1024 bytes.** Isso sustenta H3 estruturalmente.

### 8.3. Candidatos de alta resolução — todos sem comprovação de descritor

Fui honesto e rigoroso: **não consegui um único descritor publicado** para nenhum módulo ≥8 MP. Tentei `lsusb -v` em fóruns, gists, lore.kernel.org (bloqueado por robots.txt), linux-hardware.org (bloqueado), GitHub code search (403). O que segue é **inferência de banda a partir de spec comercial**, não prova.

| Modelo | Sensor | Res. máx MJPEG | Transporte (inferido) | Evidência | Autofoco | Preço |
|---|---|---|---|---|---|---|
| **Arducam B0447** | IMX179 8MP | **3264×2448@30fps** | Provável **BULK** (8MP×30fps > 24,58 MB/s do teto ISOC) | `[SEM COMPROVAÇÃO]` — [arducam.com](https://www.arducam.com/8mp-imx179-autofocus-usb-camera-module-with-metal-case-for-windows-linux-android-and-mac-os-standard-module.html) | **Sim, 10 cm–∞** | $63,99 |
| **Arducam B0290** | IMX298 16MP | 4656×3496@10fps | Provável **BULK** | `[SEM COMPROVAÇÃO]` — [arducam.com](https://www.arducam.com/arducam-16mp-autofocus-camera-for-laptop-1-2-8-cmos-imx298-mini-uvc-usb2-0-4k-video-webcam-without-microphone-with-3-3ft-1m-cable-b0290.html) | **Sim, 10 cm–∞** | $49,99 |
| **InnoMaker U20-16MP-AF** | IMX298 16MP | 4656×3496@15fps | **ISOC comprovado** — é a câmera da #538, alts 800×1/2/3 | `[CITAÇÃO VIA WEBFETCH]` [#538](https://github.com/espressif/esp-usb/issues/538) | Sim, PDAF | $39 |
| **ELP-USB8MP02G** | IMX179 8MP | 3264×2448@15fps | Provável ISOC com degrau MULT=0 | `[SEM COMPROVAÇÃO]` p/ este PID; família ISOC comprovada | Varia por SKU | $50–75 |
| **HBVCAM-8M1915 V22** | IMX179 8MP | 3264×2448@15fps | Provável ISOC | `[SEM COMPROVAÇÃO]` | Sim, mas **30 cm–∞** — ruim para caderno | $25–40 |
| **IPEVO V4K** | 8MP | 3264×2448 | Desconhecido | `[SEM COMPROVAÇÃO]` — [specs](https://us.ipevo.com/pages/v4k-specs) | Sim, área 342×255 mm (A4) | $79–99 |
| **e-con See3CAM_CU135** | AR1335 13MP | 4192×3120 | Provável BULK (base Cypress FX3), mas **USB 3.x** | `[SEM COMPROVAÇÃO]` | Sim | $199+ |

**Nota valiosa:** a InnoMaker U20-16MP-AF, que eu classificaria como "provável BULK" por inferência de banda, é **comprovadamente ISOC** — sabemos disso porque o autor da #538 publicou a escada dela. **Isso mostra que a inferência de banda é pouco confiável e reforça a exigência de descritor.**

### 8.4. Método de verificação — correção importante

Sua regra "0x01=ISOC, 0x02=BULK" precisa de ajuste. `[SPEC USB 2.0 §9.6.6, Tabela 9-13]` só os **bits 1:0** dão o tipo:

```
tipo = bmAttributes & 0x03   →  0=Control, 1=Isochronous, 2=Bulk, 3=Interrupt
```

Na prática endpoints isócronos de vídeo aparecem como **`0x05`** (isoc+async) ou **`0x0D`** (isoc+sync), nunca `0x01` puro. **Filtrar por `== 0x01` descarta todas as câmeras ISOC por engano.**

Impressão digital mais rápida, direto da spec UVC: *"Bulk endpoints shall support only alternate setting zero"* e *"All devices that transfer isochronous video data must incorporate a zero-bandwidth alternate setting"*. Logo:

- **Um único `bAlternateSetting 0` com endpoint** → BULK.
- **`bAlternateSetting 0` com `bNumEndpoints 0` seguido de alts 1..N** → ISOC.

Decodificação do MULT:

```
MULT       = (wMaxPacketSize >> 11) & 0x03
transações = MULT + 1
MPS        = wMaxPacketSize & 0x07FF
banda      = MPS * (MULT+1) * 8000 B/s
```

| wMaxPacketSize | MULT | MPS | Banda | Veredito |
|---|---|---|---|---|
| `0x0200` + bmAttr 0x02 | — | 512 | ~30 MB/s efetivos | Ideal (BULK) |
| `0x0400` | 0 | 1024 | 8,19 MB/s | **Melhor MULT=0** |
| `0x03FF` | 0 | 1023 | 8,18 MB/s | OK |
| `0x0320` | 0 | 800 | 6,40 MB/s | OK |
| `0x0C00` | 1 | 1024 | 16,38 MB/s | Rejeitar |
| `0x1400` | 2 | 1024 | 24,58 MB/s | Rejeitar |

Comandos:

```bash
lsusb -t                                    # confirmar 480M (High-Speed)
lsusb -v -d VID:PID 2>/dev/null | grep -E \
 "bInterfaceNumber|bAlternateSetting|bInterfaceClass|bInterfaceSubClass|bNumEndpoints|bmAttributes|Transfer Type|wMaxPacketSize"
v4l2-ctl --list-formats-ext -d /dev/video0  # resoluções e intervalos REAIS
v4l2-ctl -d /dev/video0 --set-fmt-video=width=3264,height=2448,pixelformat=MJPG \
  --stream-mmap --stream-count=1 --stream-to=foto.jpg
ls -l foto.jpg    # divida por 8192000 → tempo de transferência no pipe MULT=0
```

**Teste decisivo antes de integrar** — simular a restrição do P4 no Linux:

```bash
sudo modprobe -r uvcvideo && sudo modprobe uvcvideo quirks=0x80   # UVC_QUIRK_FIX_BANDWIDTH
v4l2-ctl -d /dev/video0 --set-fmt-video=width=2592,height=1944,pixelformat=MJPG \
  --set-parm=5 --stream-mmap --stream-count=5 --stream-to=teste.mjpg
grep -B4 'Atr=05' /sys/kernel/debug/usb/devices | grep 'I:\*'      # alt ativo AGORA
```

Se com `FIX_BANDWIDTH` a câmera continuar entregando alta resolução num alt de MPS ≤1024, ela muito provavelmente funciona no P4 com `MAX_MPS_IN = 1024`.

---

## 9. Plano de testes

Ordenado por (informação obtida) ÷ (risco × esforço). Nenhum toca NVS ou tabela de partições.

### T0 — Dump de descritores da NE-HD362
- **Alteração:** `CONFIG_UVC_PRINTF_CONFIGURATION_DESCRIPTOR=y` ou `uvc_host_desc_print()`. Em paralelo, `lsusb -v -d VID:PID` num Linux.
- **PASS** (existe alt com MULT=0 e MPS ≥512): H3 viável → T3.
- **FAIL** (só alts MULT>0): H3 morre → S6 ou S9/S10.
- **Risco:** nulo. **Elimina:** a incerteza central sobre MULT=0.

### T1 — Instrumentar a cadeia de silêncio
- **Alteração:** em `uvc_isoc.c:211`, capturar e logar o retorno de `usb_host_transfer_submit()`. Adicionar `ESP_LOGW` em `uvc_host_stream_pause()`. Elevar o log do driver UVC para `DEBUG`.
- **PASS** (aparece `ESP_ERR_INVALID_STATE` = 0x103 N vezes e depois nada, N = `number_of_urbs`): **H2 confirmada, pipe está HALTED.**
- **FAIL** (URBs simplesmente nunca voltam): o mecanismo é outro — investigar `num_urb_pending`.
- **Risco:** nulo (só logs). **Elimina:** H2. **Este é o teste de maior retorno.**

### T2 — Ler HCCHAR do canal ISOC em runtime
- **Alteração:** durante streaming, ler o registrador do canal (base do host channel 0 = offset `0x500`, `0x20` por canal, `usb_dwc_struct.h:1232`) e logar `val`, `mps`, `eptype`, bits 21:20 (`ec`).
- **PASS** (`ec == 0` com endpoint MULT=2): **H1 sai de inferência de código para observação de hardware.** É a prova que falta.
- **Risco:** baixo (leitura; faça de tarefa, não de ISR).

### T3 — Forçar MULT=0 **junto com** a correção de recuperação
- **Alteração:** (a) `MAX_MPS_IN 1024` em `private_include/uvc_idf_version_priv.h:14`, **ou** reverter o critério de MULT em `uvc_descriptor_parsing.c:98-100`; **e** (b) em `uvc_isoc.c:211`, se o submit falhar, chamar `usb_host_endpoint_clear()` e retentar.
- **PASS** (frames válidos a 800×600): **você tem produto.** B1 continua bug upstream, mas deixa de bloquear.
- **FAIL:** distinga — se voltar `INVALID_STATE`, a recuperação está incompleta; se der silêncio limpo, o problema não é multi-count.
- **Risco:** baixo. **Elimina:** H1 vs "problema genérico de ISOC no P4". **Nota:** (a) sozinho é exatamente o que falhou na #538. Não repita esse erro.

### T4 — `urb_size = 3072` (fechar o A/B)
- **Alteração:** `stream_config.advanced.urb_size = 3072;`
- **PASS** (voltam os callbacks e os erros): confirma que a variável era `num_isoc_packets`.
- **FAIL:** o A/B não era a versão do componente — audite `espressif/usb`, Kconfig e sdkconfig entre os builds.
- **Risco:** nulo.

### T5 — Ler GHWCFG2/3/4 (descartar H5)
```c
bool perio_ep   = !!(hwcfg2 & BIT(18));                              /* GHWCFG2 @ 0x048 */
u32  max_xfer   = (1u << (( hwcfg3        & 0xf) + 11)) - 1;         /* precisa >= 3072 */
u32  max_pktcnt = (1u << (((hwcfg3 >> 4)  & 0x7) +  4)) - 1;         /* precisa >= 3    */
bool desc_dma   = !!(hwcfg4 & BIT(30));
```
- **PASS:** H5 descartada. **FAIL:** limitação de silício → decide a favor de S9/S10/S11.
- **Risco:** nulo.

### T6 — Distinguir EC/MULT de RX FIFO (a tese do autor da #538)
- **Alteração:** `MAX_MPS_IN = 1600` (permite alt com MULT=1, banda pequena mas ainda HB).
- **PASS** (funciona): a tese do FIFO ganha força.
- **FAIL** (falha igual ao MULT=2): **é EC/MULT, não FIFO.**
- **Risco:** nulo. Barato e decisivo entre as duas leituras da #538.

### T7 — Patch de `HCCHAR.EC` (só depois de T2)
- **Alteração:** adicionar `mult` a `usb_dwc_hal_ep_char_t`; propagar `USB_EP_DESC_GET_MULT()+1` desde `hcd_dwc.c:1907`; criar `usb_dwc_ll_hcchar_set_mc()`; escrever `ec = mult` na ativação do canal; programar o PID inicial (DATA0 para MC=1, **DATA1 para MC=2, DATA2 para MC=3**, encoding `DATA0=0, DATA2=1, DATA1=2, MDATA=3`); manter `HCCHAR.MPS = 1024`; rejeitar `mult == 4`.
- **Referência exata do Linux:** `hcd.c:2653-2659` (atribuição), `hcd.c:1449-1451` (escrita em DDMA), `hcd.c:1057-1077` (PID), `hcd.c:216-220` (RxFIFO ≥ 516 + n_channels words).
- **PASS:** causa provada + contribuição upstream de valor real.
- **Risco:** **médio.** Toca o HAL compartilhado. Branch isolado; valide regressão com pendrive (bulk) e teclado (interrupt).

### T8 — Higiene elétrica (paralelo, independente)
- Alimentar por fonte de 5 V/≥2 A com cabo curto, ou injetar em `Core_5V`; opcionalmente soldar 100–220 µF low-ESR no `VBUS_OUT` junto ao `J1`; opcionalmente hub self-powered com `CONFIG_USB_HOST_HUBS_SUPPORTED=y`.
- **Risco:** baixo. Não corrige o ISOC, mas elimina H6 do espaço de busca.

**Ordem recomendada:** T0 → T1 → T4 → T3 → T2 → T6 → T5 → T7. Se T3 der PASS, pare: você tem produto, e T2/T7 viram trabalho de contribuição upstream.

---

## 10. Ausências

### 10.1. Correções procuradas e não encontradas

- `[NÃO ENCONTRADO]` Nenhum patch, fork, branch, gist ou commit que programe `HCCHAR.EC`/`MC` no ESP-IDF. Varredura em esp-idf, esp-usb, componente `usb`, notas de release 6.0.x/6.1/master.
- `[NÃO ENCONTRADO]` Nenhuma demonstração pública de webcam UVC high-bandwidth entregando frames num ESP32-P4 — nem vídeo, nem blog, nem thread, nem repositório. **A ausência é significativa.**
- `[NÃO ENCONTRADO]` Nenhuma issue mencionando `HCCHAR`, `EC`, "multi count" ou "multicnt" além da #538. Nenhuma declaração de engenheiro da Espressif sobre roadmap ou branch interno.
- `[NÃO ENCONTRADO]` Nenhuma correção no master de esp-usb tocando ISOC, MULT, EC ou recuperação de pipe halted desde 2026-05-01.

### 10.2. Câmeras sem comprovação de descritor

Todos os módulos ≥8 MP da seção 8.3, exceto a InnoMaker U20-16MP-AF (comprovada ISOC pela #538). Bloqueios: lore.kernel.org e linux-hardware.org por robots.txt, GitHub code search por 403.

### 10.3. Patches sem evidência de funcionamento

- `MAX_MPS_IN = 1024` sozinho: **comprovadamente insuficiente** (#538).
- `CONFIG_UVC_CHECK_PAYLOAD_HEADER_ERR=n`: existe em master, não testado, e desliga validação sem corrigir transporte.

### 10.4. Limitações ainda sem documentação

- `[NÃO ENCONTRADO]` A descrição literal do campo `HCCHAR.EC` no TRM do ESP32-P4. O PDF em `www.espressif.com` está bloqueado no ambiente; a versão HTML retorna o sumário do Capítulo 47 sem o corpo dos registradores. A própria doc Espressif remete ao *DWC_OTG Databook* da Synopsys (não público), e o header do P4 comenta *"Width depends on OTG_TRANS_COUNT_WIDTH (see databook)"*.
- `[NÃO CONFIRMADO]` EN e FLG do `DIO7003` (U2): extrações do esquemático conflitantes. Resolve-se com multímetro em 2 minutos.
- `[NÃO CONFIRMADO]` Capacitor de bulk no `VBUS_OUT`. Inspeção visual da placa junto ao `J1`.
- `[NÃO ENCONTRADO]` Thread [esp32.com t=41337](https://esp32.com/viewtopic.php?t=41337) ("Will the ESP32-P4 library support High speed USB for UVC camera?") — o fórum serve bot challenge por JavaScript. **Você abre isso no navegador em 1 minuto.** É a fonte mais provável de declaração oficial.
- `[NÃO DETERMINADO]` Se `HFNUM.FRNUM` no P4 conta microframes. O modelo do driver ESP e o do Linux assumem que sim.
- `[NÃO ELABORADO]` Método de captura comparativa (usbmon/Wireshark vs USBPcap vs logs instrumentados) das perguntas K.36–K.37. É trabalho de bancada bem definido e de alto valor: comparar Probe, Commit, SET_INTERFACE, alt escolhido e primeira submissão ISOC entre Linux e P4.

### 10.5. Conformidade com as restrições do briefing

Não recomendei `usb_device_uvc` ✓ · Não recomendei apagar flash, nem atribuí nada a NVS ou partições ✓ · Nenhuma mudança no backend HTTP ✓ · `HCCHAR.EC` declarado como hipótese de confiança alta, não como causa provada — falta a leitura do registrador (T2) ✓ · `MAX_MPS_IN=1024` **não** classificado como funcional ✓ · Eliminação de missed EoF **não** confundida com recepção de frames ✓ · Nenhuma câmera BULK sugerida sem prova — e disse explicitamente que não consegui provar nenhuma ✓ · Não concluí que a câmera está defeituosa ✓ · Onde não há solução comprovada, disse e indiquei o experimento de menor risco ✓

---

## 11. Fontes

**Código (clones locais, primária):**
- `espressif/esp-usb` @ `1fb14d3` — `host/class/uvc/usb_host_uvc/{uvc_host.c, uvc_isoc.c, uvc_descriptor_parsing.c, Kconfig, CHANGELOG.md, private_include/uvc_idf_version_priv.h, host_test/main/parsing/descriptors/}`, `host/usb/src/{hcd_dwc.c, usbh.c, usb_host.c, hub.c, ext_hub.c}`, `host/usb/Kconfig`, `docs/en/usb_host.rst`
- `espressif/esp-idf` @ `v6.0.2` — `components/soc/esp32p4/include/soc/usb_dwc_struct.h`, `components/esp_hal_usb/{usb_dwc_hal.c, esp32p4/include/hal/usb_dwc_ll.h, include/hal/usb_dwc_hal.h}`, `docs/en/api-reference/peripherals/usb_host*`
- `torvalds/linux` @ `3aa1dcaa4f6f` (v7.2.0-rc7) — `drivers/usb/dwc2/{hcd.c, hcd_ddma.c, hcd_intr.c, hcd_queue.c, hw.h, params.c, core.h}`, `drivers/usb/core/{config.c, urb.c}`, `include/uapi/linux/usb/ch9.h`

**Commits:** `26af107` (PR #424) · `8196b6e`/`ae0641c` (PR #450, IEC-505) · `5ffef02` · `c04d1c7` (Unreleased) · `ac3c5e6` (2.5.1) · `e35a018` (2.5.0) · `82020e6ff41f` (doc-only, esp-idf)

**Issues e PRs:**
[#538 (IEC-580)](https://github.com/espressif/esp-usb/issues/538) · [#279 (IEC-394)](https://github.com/espressif/esp-usb/issues/279) · [PR #424](https://github.com/espressif/esp-usb/pull/424) · [PR #450](https://github.com/espressif/esp-usb/pull/450) · [#468 (IEC-518, Won't Do)](https://github.com/espressif/esp-usb/issues/468) · [#52 (IEC-34/143)](https://github.com/espressif/esp-usb/issues/52) · [esp-idf#18235 (DMA vs EMAC no P4 v1.3)](https://github.com/espressif/esp-idf/issues/18235) · [esp-idf#17550](https://github.com/espressif/esp-idf/issues/17550)

**Documentação:**
[USB Host v6.0.2 / P4](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32p4/api-reference/peripherals/usb_host.html) · [Maintainers Notes DWC_OTG](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/usb_host/usb_host_notes_dwc_otg.html) · [ESP-USB latest](https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_host.html) · [usb_stream — descontinuado](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_host/usb_stream.html) · [ESP32-P4 Errata](https://docs.espressif.com/projects/esp-chip-errata/en/latest/esp32p4/index.html) · [ESP-FAQ USB](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/usb.html)

**Especificações:** [USB 2.0 (USB-IF)](https://www.usb.org/document-library/usb-20-specification) — §5.6.3, §5.6.4, §5.8.3, §5.8.4, §7.2.1.1, §7.2.4.1, §9.6.6, §11.1.1, §11.7, §11.14

**Hardware:** [Wiki Waveshare 7B](https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-7B) · [Esquemático PDF](https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-7B/ESP32-P4-WIFI6-Touch-LCD-7B.pdf) · [Repo de exemplos](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B) · [Datasheet DIO7003](https://www.dioo.com/uploads/product/20210527/39809ef9c4aeae798493447426d5dd7c.pdf) · [CH343](https://www.wch-ic.com/downloads/CH343DS1_PDF.html) · [Datasheet ESP32-P4](https://files.waveshare.com/wiki/common/Esp32-p4_datasheet_en.pdf)
