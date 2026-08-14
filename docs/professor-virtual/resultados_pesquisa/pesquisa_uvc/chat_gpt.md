# Sumário Executivo

A análise indica que o problema **não está na câmera** mas na pilha USB Host do ESP32-P4. A **causa provável** é que o controlador DWC2 do ESP32-P4 não está programando o campo *HCCHAR.EC* (número de transações por microframe) para endpoints isócronos high-bandwidth, efetivamente solicitando só 1 de 3 transações por microframe. Segundo a especificação USB 2.0, endpoints HS isócronos podem ter até 3 transações por microframe, e o driver Linux DWC2 explicitamente programa esse multiplicador. Esse campo não está sendo configurado no ESP32-P4 (hipótese forte). Como consequência, na versão 2.4.2 havia muitos erros (bits *ERR* nos payloads) mas algum único quadro ocasionalmente vinhou completo. Após as mudanças de URB em 2.5.1, o streaming parou completamente. 

**Comprovado:** O PR #424 (IDF v6.0.2 / usb_host_uvc v2.4.2) corrige a seleção de endpoints de alta largura, mas só resultou em quadros esporádicos (um JPEG válido encontrado em bancada). O *workaround* de limitar a carga máxima ao valor de 1024 bytes por microframe (issue #538) reduziu erros de EoF e CRC, mas **não** gerou quadros completos (falha ESP_ERR_INVALID_STATE ao submeter URB). **Incerto:** Não há prova de que *HCCHAR.EC* seja o único culpado, nem patch oficial para isso. Também não há evidência de câmera UVC 8–12MP convencional usando endpoint BULK (o único exemplo conhecido é a câmera térmica FLIR Boson). 

**Solução de menor risco:** Em vez de criar disco novo, recomenda-se usar URBs de um pacote (como em 2.4.2) ou um firmware/biblioteca que programe corretamente *MULT=2*. Em último caso, pode-se usar uma webcam UVC bulk comprovada, ou migrar para um SBC Linux (que lida corretamente com múltiplas transações USB2 HS). 

**Próximo teste recomendado:** Modificar temporariamente o driver USB do ESP32-P4 para definir *HCCHAR.EC = 2* (permitir 3 transações/microframe) e ver se quadros fluem. Se funcionar, confirma a hipótese do MULT ausente. Em paralelo, comparar a negociação USB (Probe/Commit, alt setting, payload) via usbmon no PC vs logs do ESP32-P4 para identificar diferenças de configuração. 

# Tabela de Bugs

Sintoma                      | Camada            | Versão Afetada       | Correção               | Evidência de sucesso           | Link  
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━|━━━━━━━━━━━━━━━━━━━━|━━━━━━━━━━━━━━━━━━━━━━|━━━━━━━━━━━━━━━━━━━━━━|━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━|━━━━━━━
Pacotes ISO perdidos / bit ERR em quase todos os payloads | USB Host (UVC)      | v2.4.2 (com PR#424)  | (#279) seleção correta de endpoint HB | Um frame JPEG válido (800×600) foi recebido ocasionalmente (teste interno) | –  
Nenhum quadro recebido (silêncio após SET_INTERFACE)     | USB Host (UVC)      | v2.5.1             | –                      | Nenhum dado de vídeo após negociação  | [18†L209-L217]  
Missed EoF e CRC inválido nos payloads (frames incompletos) | USB Host (UVC)      | v2.5.1             | –                      | Redução de erros com MAX_MPS_IN=1024 (issue #538) | [18†L209-L217]  
ESP_ERR_INVALID_STATE ao submeter URB             | USB Host (UVC)      | v2.5.1             | –                      | –                              | [18†L215-L217]  
Tarefa de eventos USB para após ALL_FREE          | USB Host Library   | Exemplo (código proprio) | Corrigido no exemplo oficial     | Reconexão detectada corretamente após atualização | [61†L281-L289]  

# Avaliação das Hipóteses

**Hipótese 1: MULT não programado (HCCHAR.EC ausente)**  
- *Evidências a favor:* O IP DWC2 suporta até 3 transações por microframe e o Linux explicitamente programa esse “multi-count”. A câmera anunciou MULT=2 (3072 B/µframe) e testes indicam ~500B/µframe real. Manter *EC=0* significaria pedir só 1024 B/µf. O patch de limitar para 1024 aliviu erros na issue #538.  
- *Evidências contra:* Não há medição direta do campo EC. O ESP-IDF não expõe API para isso, e não se sabe se o hardware infere o valor automaticamente. Mesmo com MULT correto, é possível que outros fatores (FIFO, timing) atrapalhem.  
- *Teste discriminante:* Instrumentar o HAL (usb_dwc_ll) para forçar **HCCHAR.EC=2** (3 transações). Se quadros completos surgirem, confirma causalidade. Se ainda falhar, descarta *somente* MULT faltante.  
- *Confiança:* Alta. Essa explicação alinha-se bem com o padrão USB e as observações de redução de erro ao limitar o payload.  

**Hipótese 2: Payload header inválido da câmera (missing EOF/FID)**  
- *Evidências a favor:* Erros contínuos de CRC e EoF ausente podem indicar formato UVC irregular. Muitos dispositivos UVC não conformes causam erros no driver (por exemplo, payload sem EoF, FID travado). Essa hipótese explicaria os erros vistos na v2.4.2.  
- *Evidências contra:* Mesmo corrigindo erros de payload (por exemplo, desabilitando checagem de EOH/EoF), o streaming *não* avançou em v2.5.1 – ficou em silêncio. Ou seja, payloads inválidos causam frames corrompidos, mas não explicam falta total de callbacks. Além disso, a câmera funcionou em outros hosts sem ajustes especiais.  
- *Teste discriminante:* Desabilitar validação rígida de cabeçalho UVC (CONFIG_UVC_CHECK_PAYLOAD_* = n) e rodar stream. PASS: se quadros aparecem (mesmo corrompidos), sugere payload; FAIL: se ainda não há atividade, indica outro problema.  
- *Confiança:* Média-baixa. Essa hipótese pode justificar quadros corrompidos, mas dificilmente explica o streaming “mudo” em 2.5.1.  

**Hipótese 3: Alternate setting/mps excessivo escolhido**  
- *Evidências a favor:* A issue #538 mostrou que, sem restrição, o driver escolhia alt setting com 3×800 B/µf (2400 B/µf) excedendo o FIFO real, causando *missed EOF*. Limitando a 1024 B/µf (alt menor) eliminaram erros. Isto sugere que o Commit Report com 3072 pode ser enganador e estava muito alto.  
- *Evidências contra:* Mesmo usando alt menor (via MAX_MPS_IN=1024), o streaming não iniciou – deu ESP_ERR_INVALID_STATE. Ou seja, apesar de aliviar erros, não produziu quadros. Portanto, alt “maior que o real” explica erros mas não o silêncio final.  
- *Teste discriminante:* Forçar manualmente a seleção de um alternate setting sem MULT (se disponível) ou criar um alt custom de MPS≤1024 em *Commit*. Se quadros surgirem, culpa é a negociação errada. Caso contrário, descarta apenas alt setting.  
- *Confiança:* Média. É certo que escolher alt acima do real sobrecarrega o host (e.g. FIFO overflow), mas é secundário ao problema principal de transmissão.  

**Hipótese 4: Diferença elétrica/agendamento via hub**  
- *Evidências a favor:* Alguns dispositivos USB HS se comportam diferente através de hub (piscar bits SOF, scheduling dos microframes). Um hub alimentado poderia atenuar limites de corrente ou timing.  
- *Evidências contra:* O problema persistiu em testes repetidos; nenhuma evidência sugere falha de alimentação ou handshake (câmera enumera corretamente). Hubs USB 2.0 não mudam o scheduling isócrono HS de forma que explique 0 bytes recebidos.  
- *Teste discriminante:* Conectar câmera a um hub USB 2.0 alimentado e repetir o streaming. Se quadros funcionarem, aponta para questão de driver/hardware vs enrivnomena. Se não, elimina benefícios de hub.  
- *Confiança:* Baixa. Não há indícios claros de falha elétrica ou de suporte a hubs no log (o problema é lógico, não de energia).  

**Hipótese 5: Usar endpoint BULK alternativo**  
- *Evidências a favor:* UVC define que um dispositivo *pode* usar Bulk em vez de ISO para melhor banda, e o driver do ESP32 suporta isso . Se existisse uma webcam USB convencional (8–12MP) que usa Bulk, ela contornaria completamente o problema. (Ex.: a câmera térmica FLIR Boson usa Bulk e funcionaria no host ESP).  
- *Evidências contra:* Não encontramos nenhuma câmera comercial 8–12MP com endpoint Bulk comprovado. A especificação UVC de câmeras consumer praticamente exige ISO high-bandwidth para vídeo. Em fóruns, ninguém relatou webcams USB em Bulk (exceto dispositivos de nicho caros).  
- *Teste discriminante:* Testar uma câmera que seja conhecida por usar Bulk (como Boson, se disponível) com o ESP32-P4. Se streaming funcionar via Bulk, indica o contorno; se falhar, descarta Bulk como solução prática.  
- *Confiança:* Média-baixa para câmeras comuns. Bulk resolveria o problema de HF, mas não há câmera UVC bulk popular de alta resolução identificada.  

# Soluções Propostas

Solução                            | Já funcionou?  | Evidência de funcionamento         | Esforço    | Risco      | Adequação ao produto  
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━|━━━━━━━━━━━━━━━━━|━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━|━━━━━━━━━━━━|━━━━━━━━━━━━━|━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Programar *HCCHAR.EC* (multi-count) no HCD ESP32-P4 | **Não** (hip.)  | Hipótese técnica forte (Linux usa MULT)  | Alto (desenvolvimento HAL/HCD)  | Médio (pode quebrar outros transfers)  | Alta (resolveria raiz high-bandwidth)  
Forçar 1 pacote/URB (modo 2.4.2)         | Parcial (observação) | Benchmark inicial do v2.4.2 obteve JPEG válido esporádico  | Baixo (configuração de *urb_size*) | Médio (várias interrupções e CPU alto) | Média (não ideal, mas possivelmente viável como contingência)  
Selecionar alt menor (payload ≤1024)     | Não (tentativa)  | Issue #538: eliminou erros de EoF mas streaming travou  | Médio (precisa modificar driver/device)  | Médio (menor qualidade/velocidade)  | Baixa (câmera precisa suportar e perderia resolução máxima)  
Usar câmera UVC Bulk comprovada        | Parcial (Boson)  | Boson 640 (térmica) via Bulk funciona; **nenhuma câmera 8–12MP** convencional testada  | Alto (encontrar/certificar hardware) | Baixo (pouca mudança SW, só hardware) | Baixa (câmeras bulk high-res indisponíveis ou caras)  
Migrar para SBC Linux (USB nativo)      | Sim             | Linux dwc2 trata MULT corretamente; câmeras testadas funcionam em PC/Android   | Alto (trocar plataforma, refazer software) | Alto (maior consumo, complexidade HW) | Último recurso (estável, mas maior custo e esforço)  

# Plano de Testes

1. **Ativar MULT no HCCHAR:** Modificar o driver USB do ESP32-P4 (usb_dwc_ll ou HCD) para que `ep_char.multi_count = 2` (permitir 3 transações/µf). Espera-se que os URBs isócronos passem *todas* as transações.  
   - *PASS:* Quadros MJPEG começam a chegar completos. Confirma que a falta de MULT era a causa.  
   - *FAIL:* Ainda sem quadros ou mesma falha. Descartar falt de MULT como causa única.  
   - *Elimina:* Se passar, descarta outras hipóteses. Se não passar, elimina MULT como único fator.  
   - *Risco:* Baixo (só software, não danifica HW).  

2. **URB de 1 pacote no v2.5.1:** Configurar `urb_size` para ≈3072 bytes (1×MPS) em usb_host_uvc 2.5.1, simulando o comportamento do v2.4.2.  
   - *PASS:* Observa-se callbacks frequentes e eventualmente quadros (como visto em 2.4.2). Mostra que “regime de 1 pkt/URB” funciona.  
   - *FAIL:* Ainda nada recebido. Implica que nem mesmo reduzindo URB ajuda, reforçando que problema é *outro*.  
   - *Elimina:* Se PASS, reforça Hipótese 1 (MULT). Se FAIL, sugere que algo além de URB (talvez HCD) trava tudo.  
   - *Risco:* Baixo (só ajusta configuração). Alto uso de CPU e IRQs.  

3. **Testar via hub USB HS alimentado:** Conectar a câmera através de um hub USB 2.0 alimentado, mantendo ESP32-P4 na porta host. Verificar se o *alternate setting* negociado muda (por possivelmente alterar carga) e se quadros chegam.  
   - *PASS:* Vídeo flui; implica que o hub (ou VBUS extra) mitiga o problema, orientando solução (ex.: usar hub permanente).  
   - *FAIL:* Ainda silencioso; sugere que o problema é lógico e não de linha elétrica ou negociação de enlace.  
   - *Elimina:* Se FAIL, descarta falhas de alimentação ou timing de host; foca no software.  
   - *Risco:* Baixíssimo (aparentemente não muda nada no USB2).  

4. **Capturar tráfego USB/Probing com PC:** Usar `usbmon`/Wireshark no Linux para capturar a negociação Probe/Commit e os pacotes isócronos da câmera num host conhecido. Comparar com logs debug do ESP32-P4.  
   - *PASS:* Confirma detalhes reais (e.g. ~500B/µf medidos), valida ou ajusta entendimento do que a câmera exige. Revela qualquer diferença de parâmetros (intervalo, alt setting, etc.).  
   - *FAIL:* (improvável; só coleta informação).  
   - *Elimina:* Clarifica se o host ESP-IDF está enviando configurações idênticas às do PC. Descarta problemas de câmera ou de especificação.  
   - *Risco:* Nenhum (monitor passivo).  

5. **Testar câmera USB Bulk (Boson 640):** Se disponível, conectar a FLIR Boson ou outra câmera que use endpoint Bulk. Verificar se o ESP32-P4 consegue receber stream via BULK usando usb_host_uvc (ou usb_device_xfer=bulk).  
   - *PASS:* Streaming funciona, mostrando que o host USB do ESP32 lida bem com bulk HS (demonstra contorno).  
   - *FAIL:* Também não funciona, indicaria problema geral no host/driver.  
   - *Elimina:* Se FAIL, descarta completamente a solução “usar bulk” neste hardware.  
   - *Risco:* Baixo (teste isolado). Não substitui a câmera do produto final.  

# Ausências e Pontos Não Resolvidos

- **Nenhum patch comprovado encontrado** que programe *HCCHAR.EC* no ESP32-P4. Não há commit ou fork público que implemente essa correção no HAL/HCD.  
- **Falta de câmeras Bulk comprovadas:** Não localizamos módulos UVC 8–12MP com endpoint bulk verificado em descritor. Módulos ELP, Arducam, InnoMaker, etc., não têm documentos públicos confirmando bulk. Estudos e logs de usuários indicam que webcams comuns usam ISO.  
- **Patches sem evidência:** O *workaround* de MAX_MPS_IN=1024 (issue #538) e outras modificações no esp-usb não têm demonstração de quadros válidos. Deve-se notar que eliminar “Missed EoF” não significa sucesso no streaming.  
- **Documentação insuficiente:** Não há seção no TRM do ESP32-P4 detalhando *HCCHAR.EC* ou limitações do controlador DWC2 quanto a transações periódicas high-bandwidth. Tampouco há guia no IDF sobre limitações de FIFOs em endpoints isócronos.  
- **Desconhecimento de solução oficial:** Até 13 ago.2026 não encontramos branch, issue ou versão do ESP-IDF corrigindo definitivamente o problema de HB ISOC no P4. Recomenda-se acompanhar futuras atualizações do esp-usb e do ESP-IDF master.