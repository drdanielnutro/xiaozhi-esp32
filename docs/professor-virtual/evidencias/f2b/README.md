# Evidências da F2B — comparação A/B CSI × UVC (2026-08-10)

Fotos que fundamentam a decisão de rota da F2B (`F2B-RouteUvc` no
decision-log). Capturadas na bancada de 2026-08-10 (estação Windows/WSL,
placa 7B via COM3, firmware PV do release 46db44b) e no piloto de PC do
`licao_casa`. Detalhes completos em `../../fases/fase-2b.md`, "Notas da
fase".

| Arquivo | Origem | Resolução | Tamanho | Leitura |
|---|---|---|---|---|
| `csi-foto1-pouca-luz.jpg` | OV5647 CSI na 7B (1280×960 RAW10 binning → JPEG q85), luz ambiente fraca | 1280×960 | 193.759 B | Impresso grande legível; legenda pequena degradada por ruído/ganho; página ocupa só parte do quadro |
| `csi-foto2-mais-luz.jpg` | Mesma câmera e página, com boa iluminação | 1280×960 | 162.851 B | Página quase cheia; legenda pequena nítida a 300%; tom azulado (AWB) e leve clarão especular. Melhor caso realista da CSI |
| `uvc-baseline-piloto-page1.jpg` | Câmera UVC NE-HD362 (IMX362) no piloto de PC do `licao_casa`, modo REDUZIDO negociado pelo Chrome; extração comprovada no MESMO backend | 1358×1920 | 404.350 B | ~6,2 px/mm na página; crédito minúsculo legível; traço de lápis apagado visível |

Conclusão A/B: mesmo no melhor caso (foto 2), a CSI entrega ~1.000 px no
lado longo da página (~3,5–4 px/mm) com foco fixo e mais ruído — cerca de
metade dos pixels lineares por letra da baseline UVC, que por sua vez roda
em modo reduzido (o nativo 3264×2448 dobraria de novo). Evidência do
proprietário: no piloto, resoluções da classe da CSI já causavam extração
incompleta até em texto impresso (falha silenciosa). A rota UVC deixou de
ser "principal a confirmar" e passou a **necessária**; o CSI fica, no
máximo, como câmera de preview/enquadramento.
