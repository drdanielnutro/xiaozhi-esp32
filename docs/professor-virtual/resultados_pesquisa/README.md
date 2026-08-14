# Resultados de pesquisa externa (não canônicos)

Este diretório arquiva **saídas brutas de ferramentas externas de pesquisa**
(ChatGPT, Gemini, Claude e afins) usadas como apoio durante a investigação da
F2B (UVC no ESP32-P4) e no estudo de opções de câmera CSI (OV5647 e
sucessoras).

Regras de leitura:

- **Não são fontes canônicas do projeto.** São material de apoio, no estado
  em que as ferramentas o produziram, sem revisão linha a linha.
- **Podem conter hipóteses, imprecisões ou erros factuais** — inclusive
  afirmações apresentadas com confiança pelas ferramentas.
- **Em qualquer conflito, as evidências de bancada prevalecem**
  (`../evidencias/f2b/` e as notas de `../fases/fase-2b.md`). Onde um
  relatório daqui divergir do que a bancada mediu, o relatório está
  supersedido.
- Não cite estes arquivos como conclusões verificadas; use-os apenas como
  ponto de partida para verificação própria.

Conteúdo:

- `pesquisa_uvc/` — relatórios sobre a falha do streaming UVC/ISOC no
  ESP32-P4 (a rota UVC foi **rejeitada** no encerramento da F2B em
  2026-08-13, sem causa raiz demonstrada; ver decision-log
  `F2B-Encerramento-UVCRejeitada`).
- `pesquisa_ov5647/` — pesquisa sobre o sensor OV5647 e alternativas CSI de
  maior resolução (insumo para a escolha da próxima câmera CSI).
