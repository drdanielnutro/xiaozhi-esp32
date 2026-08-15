# Tuning de câmera do Professor Virtual

`ov5647_pv.json` é o arquivo de tuning do pipeline IPA usado pela variante
`esp32-p4-wifi6-touch-lcd-7b-professor-virtual` (flags
`CONFIG_CAMERA_OV5647_CUSTOMIZED_IPA_JSON_CONFIGURATION_FILE*` no
`config.json` da placa). Base: cópia exata do `ov5647_default.json` do
componente `espressif__esp_cam_sensor`, com UMA mudança:

- **Seção `awb` adicionada** (decisão `PV-CamMaxPotential-Round1`,
  2026-08-15): liga o balanço de branco automático (modelo 0, gray world) que
  o tuning default da Espressif para o OV5647 não instancia — sem ele a cor
  sai só da matriz CCM fixa (tom azulado documentado na F2). Valores iniciais:
  passos de ganho 0.2, `min_counted` 1000, faixa de verde alargada para
  120–235 (página de caderno é clara); razões R/G e B/G nos defaults do
  gerador (`esp_ipa_config.py`). São ponto de partida sem calibração de
  bancada — ajustar com A/B físico se a cor oscilar ou não convergir.

Pegadinhas conhecidas do gerador (documentadas na sessão de 2026-08-15):
`min_blue_gain_step` é ignorado (o gerador repete o valor do vermelho); os
`enable_log` das structs não são configuráveis por JSON; as tabelas por ganho
(`adn.bf`, `aen.sharpen`, `aen.contrast`) só usam a primeira entrada no
OV5647, porque o driver não expõe leitura de ganho.

Seções `agc`/`atc` NÃO funcionam com o OV5647 (o driver não expõe
`V4L2_CID_GAIN` nem `V4L2_CID_CAMERA_AE_LEVEL`); a exposição fica com o AEC
interno do sensor. A seção `af` roda estatística mas não move nada (foco
fixo, sem motor).
