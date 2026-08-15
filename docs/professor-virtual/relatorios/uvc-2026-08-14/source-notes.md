# Notas de fonte — investigação UVC

Snapshot auditado em 2026-08-14, com HEAD `360c4527d8a0e84c6e1c98d7e89cfb111d0f91b3` e worktree sujo preservado.

## Regra de evidência

- “Confirmado” no relatório significa que há log bruto, imagem, diff, código ou hash local correspondente.
- Informações presentes somente no diário `fase-2b.md` são identificadas como “registro do diário”; não recebem o mesmo peso de um log bruto.
- Pesquisas de ChatGPT, Gemini e Claude em `resultados_pesquisa/` não foram usadas como prova dos resultados de bancada nem da causa raiz.
- Ausência de evento no nível INFO não foi tratada como ausência de tráfego depois que a rodada 23 registrou completions no driver.
- O relatório não transforma comportamento correlacionado em causa.

## Recontagens executadas

- Arquivos `.log` preservados no diretório F2B: 19.
- Arquivos com uma linha `PV-UVC-RUNG ... result=PASS`: 1.
- Total de linhas `PV-UVC-RUNG ... result=PASS`: 1.
- Linhas `New frame!` nos 19 logs: 0.
- JPEG UVC de bancada `t5-*.jpg`: 1, válido como JPEG baseline 800×600.
- Rodada 24: 133 starts, 132 timeouts, 0 `New frame!`.
- Rodada 25: 198 starts, 197 timeouts, 0 `New frame!`.

## Integridade dos logs e do único JPEG UVC

```text
fea1b5821a92ec303dc694504c756f1b839c1aee65cdb6f6785f224e4220fe93  t5-rodada1-isoc-sem-frames.log
13ac23760829aa7895c299edc6f0ed1286d38d15b08fb3babfabe523740e8e84  t5-rodada2-eoh-rejeita-pacotes.log
57b1ec7fca5156b856d9ef3710e7241cf1d8316eabc2b36c8515cc2c2a692bdb  t5-rodada4-800x600-pass.log
778ce8e0738d63193aadeb825a7036be7b8d361990420008aa9b17c2891f7b46  t5-rodada5-ciclo-ok-camera-travada.log
e2bc8a3484f2ac917ad44e2df965c157e07c61286407ade828dd8951a4aa998a  t5-rodada6-vbus-nao-destrava.log
5e4cb242c0895a713c5b70a764db828f548e64ed463f695023200579caa8c839  t5-run11-streaming-com-err-madrugada.log
e2ce1c6bf460c73b215b602e138c8fd042990c2d4655a30a9e7c2419deb20424  t5-run13-mudo-r12.log
e99c50338eb2e849ca71f0cb7ee37fb29c2ccbca5ab6b224cd5d93092de0abd9  t5-run14-controle-r4-mudo.log
54c02325f47552aeeada8c5318d6e444d49398d909edb88b83e01377c7619029  t5-run15-stack251-silencio.log
a642292bb2e94755b3a25d099b624837b5bda4d18423ecc9fba49860be8366e6  t5-run17-controle-242-canal-vivo.log
89428eb010b29f730ba7fcb7899996e425da8ee7bd612269fd82306ed2c1b649  t5-run18-e1-urb1pacote-mudo.log
780619938be7d2551a38633f26bfd0071f84dc801eec675c639a6587c7a00ed3  t5-run19-e2-sem-vbus-mudo.log
3a48b3946f39eae4aa3cdb14b6af1590ff34306db856860f63bc0b83522f480e  t5-run20-e5lite-242-mudo.log
9b3f62f6e60552756e5587e6f1f9ef33a95b3b3fd0960e86fc8316d2b5df40ee  t5-run21-e6-4urbs-mudo.log
73914338d5965bb5900211bea6ba037111906c7c754b58d251c05bcf4056d8c4  t5-run22-e7-flood-acorda-canal.log
9e3a090701bb39a90b04447d41d53b471e6f995dea97714f02f28a11462cc6f2  t5-run23a-controle-hb-headers-invalidos.log
3daf5fa4a72d5d3c2dbbcf4903cba43957ec748db5d0edf50f94710ced6cb5c7  t5-run23b-alt944-mult0-headers-invalidos.log
54a777b017b6d7e832c7635ac2f306a44493f9f48a5607bdeb8eb70e0344300c  t5-run24-exemplo-oficial-idf602-mudo.log
19073fccb3c828636e367bce89a46274052c70c1aec2f080e77650e6236c6446  t5-run25-exemplo-oficial-alt640-mult0-mudo.log
235894a9e46acb96ce3e0916740bb68c585fc4153872e37b572a006554da332d  t5-rodada4-foto-800x600.jpg
```

## Limitações da rastreabilidade

- O diário chama a investigação de “25 rodadas”; o diretório preserva 19 logs. Rodadas 3, 7–10, 12 e 16 não têm um arquivo bruto individual com esse número. Algumas são mudanças de firmware ou estão consolidadas em outros logs.
- O teste da câmera no Photo Booth consta no diário, mas não há captura ou log separado arquivado.
- O patch exato usado para forçar alt 4/640 na rodada 25 não está preservado neste diretório. O log de runtime registra `CODEX-ALT640 selected alt=4`, `payload=640` e a alocação de 16 pacotes de 640 B.
- O PASS da rodada 4 usou o ELF `fabe23054…`. O chamado controle da run14 usou `0f8737c26…`, e a run17 usou `2bdfb0cac…`. Assim, há tentativas posteriores com configuração e finalidade de controle semelhantes, mas não há evidência de nova execução do mesmo ELF bit a bit que produziu o JPEG.
- Não há captura de barramento USB; portanto os contadores do driver não mostram o conteúdo integral das transações no fio.
- O encerramento e a remoção do código UVC ainda não eram um commit no snapshot; eram mudanças no worktree.

## Observação de código sobre high-bandwidth

Esta observação foi conferida diretamente no stack local e foi mantida separada da causalidade de bancada:

- `managed_components/espressif__usb/src/hcd_dwc.c` atribui `ep_char->mps = USB_EP_DESC_GET_MPS(pipe_config->ep_desc)`, isto é, o MPS base.
- No ESP-IDF 6.0.2, `components/esp_hal_usb/esp32p4/include/hal/usb_dwc_ll.h`, `usb_dwc_ll_hcchar_init()` escreve endereço, tipo, velocidade, direção, endpoint e MPS; não escreve `ec`.
- `components/soc/esp32p4/include/soc/usb_dwc_struct.h` declara `ec:2` em `usb_dwc_hcchar_reg_t`.
- A busca por escrita em `.ec` no HCD/HAL auditado não retornou ocorrência.

Isso demonstra a forma como o campo é tratado no código auditado. Não demonstra que essa omissão causou os timeouts da NE-HD362; a rodada 25 falhou mesmo sem transações adicionais por microframe.

## Por que o relatório usa tabela e apenas um gráfico

Os ensaios não têm a mesma duração, a mesma pilha nem o mesmo número de tentativas. Somar falhas ou comparar taxas entre todas as rodadas produziria uma falsa equivalência. A tabela preserva as condições de cada ensaio. O único gráfico compara apenas as duas execuções do mesmo aplicativo oficial, com a mesma métrica literal de log.
