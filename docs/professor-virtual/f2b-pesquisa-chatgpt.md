# Limites do ISP e contorno

Segundo a documentação oficial, o pipeline de ISP do ESP32-P4 suporta resolução máxima **1920×1080**.  Em especial, o readme do `esp_cam_sensor` confirma: “**tamanho de entrada máximo do ISP é 1920×1080**; ao contornar (bypass) o ISP, a saída pode ser maior que 1080p”. Não há menção a qualquer suporte oficial a “tiling” ou duplo passe para maior resolução. O que existe de oficial é justamente a recomendação de **bypass do ISP** para capturar imagens acima de 1080p. Portanto, **com ISP do P4 não dá para passar de 1080p** (máx. 1080p); o *workaround* oficial seria usar `bypass_isp`, mas isso só habilita saída raw/bruta maior, sem processamento ISP. Conclusão: *o ISP nativo é limitado a 1080p* (Nível de confiança: **alto**).

# CSI sem ISP (bypass)

Em tese, o controlador MIPI-CSI do P4 suporta taxas altas (até 1.5 Gbps por pista) que permitiriam receber direto as 5 MP (2592×1944) do sensor. De fato, o componente `esp_cam_sensor` nota que, “com bypass, o tamanho de saída pode ser maior que 1920×1080” e o bit rate de 1.5 Gbps/lane “permite recepção de 2592×1944 do OV5645”. Ou seja, **o hardware pode receber raw acima de 1080p**. Na prática, porém, não há relatos de captura bem-sucedida acima de 1080p no P4 sem ISP: fóruns indicam que mesmo 1080p RAW10 está dando problemas. Por exemplo, usuários reportaram que ao configurar 1920×1080 RAW10@30fps via CSI o firmware “travava” ou entregava apenas *frames* parciais. Não encontramos exemplos de captura direta de 2592×1944 funcionando no P4. Portanto, embora o hardware suporte, **o firmware esp_video atual não consegue (ainda) capturar quadros acima de 1080p via CSI** sem ISP – e mesmo a 1080p têm ocorrido falhas. Veredicto: *incerto/impraticável atualmente* (Nível de confiança: **médio-baixo**).

# Sensores MIPI com ISP interno (YUV/JPEG acima de 1080p)

Não há sensores MIPI-CSI “plug-and-play” que saiam já em JPEG/YUV acima de 1080p no ecossistema típico usado com P4. O `esp_cam_sensor` lista sensores como OV5645 (5MP) que **podem** gerar 2592×1944 em RAW e também suportam saída YUV422/YUV420. No entanto, o pipeline do P4 espera RAW vindo da CSI (que é processado pelo ISP interno). Em tese, um sensor como OV5645 poderia ser configurado para saída YUV ou JPEG, mas o driver do P4 (via CSI) não está projetado para receber JPEG nativo nem YUV pré-demosaicado – ele assume Bayer raw e usa o ISP para converter. Não há referência nem exemplo concreto de usar sensor MIPI entregando JPEG/YUV direto. Resumindo: não conhecemos nenhum sensor CSI de alta resolução com ISP interno que dispense *completamente* o ISP do P4. O componente `esp_cam_sensor` mesmo destaca formatos RAW e conversões padrão, sem citar caminho YUV/JPEG direto. Conclusão: *nenhuma solução prática conhecida* – o caminho suporte hoje é RAW + ISP. (Confiança: **média**.)

# Câmeras USB UVC no P4 (12 MP IMX362 candidato)

O ESP32-P4 tem USB Host OTG High-Speed (480 Mbps) capaz de UVC. O componente `usb_host_uvc` foi atualizado para ESP32-P4 (suporta USB HS após correção de [issue #279][37]). Na prática, **camadas conhecidas**: 

- **Resoluções**: Em geral, o P4 tem operado com UVC até 1080p@30 (USB HS) sem taxa alta. Usuários relatam 1080p consistente, mas **não há evidência que 4K/UHD streaming funcione** no host. Em vídeo de demonstração USB (projetos/perfis Demosaic), só reportam até 1080p. 
- **MJPEG vs YUY2**: Câmeras USB geralmente fornecem MJPEG ou YUY2. USB2.0 HS limita ~40 MB/s, logo MJPEG é preferível para >VGA. Câmeras IMX362 disponíveis saem MJPEG 12 MP. Em teoria, o P4 deve capturar MJPEG nativo (é apenas dados USB) e pode passar direto no uplink (sem recodificar). Não encontramos benchmarks específicos, mas **em 1 fps** um frame MJPEG ~400 KB (por ex. 2592×1944) cabe sem problemas no pipe USB 480 Mb/s. Fluxo contínuo 30fps, porém, *ultrapassaria* a banda (parece improvável sem perda). 
- **Modelos que funcionam**: Não há público de casos específicos de IMX362 ou câmeras “documento” em hosts embarcados. A função UVC é relativamente nova na fila; câmera genérica UVC (C270 720p) funciona (exemplo [53]), mas nem ela segue specs padrão (C270 injeta H.264 em MJPEG). 
- **Frame único grande (2592×1944 ou 3264×2448)**: Em tese, pode-se enquadrar a câmera por software (solicitar um único *snap*). Como é USB HS, pegar um único MJPEG de 3264×2448 (~1–2 MB comprimido) numa vez também cabe, mas depende de *enumeração/descritores*. Se o driver aceitar a linha de resolução, sim. Mas não há teste publicado. Risco: a queda de quadro (timeout USB) e o descritor do dispositivo precisam suportar essa resolução (câmeras chinesas às vezes ignoram UVC padrão). 
- **Autofoco**: Câmeras como a IMX362 têm autofoco embutido. Normalmente elas focam automaticamente ao ligar (AF estático ou contínuo dentro da câmera). A especificação UVC tem controles (CT_FOCUS_AUTO), mas muitos módulos resolvem foco internamente sem necessidade de comando do host. Não há relatos de P4 implementando controle UVC de foco, então dependerá do funcionamento “out-of-box” da câmera. 
- **VBUS e corrente**: A placa Waveshare expõe porta USB OTG tipo-A HS. Como é USB2.0 HS, presume-se fornecimento típico de até 500 mA a 5 V. A câmera IMX362 consome 160–260 mA, ou seja **dentro do esperado**. (Não achamos documento oficial da Waveshare, mas é razoável supor ~500 mA disponível.) 

**Veredicto (Rota UVC)**: *Funciona* até 1080p comprovado (espera-se MJPEG), *não comprovado* acima disso. A câmera NE-HD362 deve conectar em USB HS, mas pode saturar a interface: 3264×2448 MJPEG será grande, e o limite prático de streaming do P4 é ~1080p (usuários apontam “máx 1080p@30”). Foco deverá estar OK sem ação extra. Riscos: compatibilidade UVC, consumo de corrente (deve ok) e buffers de PSRAM para quadros grandes. Teste inicial: focar no stream 1080p via USB para validar UVC; se OK, tentar captura pontual em 2–3 MP.

# Decodificação JPEG pelo P4

O codec JPEG por hardware do P4 decodifica até cerca de “4K” (resolução ≈ 3840×2160). Em particular, o datasheet lista *“still image decode up to 4K”*. Portanto, uma imagem JPEG de 5 MP (2592×1944) ou até ~8 MP (3264×2448) está dentro desse limite (4K UHD ≈8,3 MP). Conclui-se que o decoder integrado consegue abrir essas fotos para preview. (Para o pipeline de faxina de escrita manual, podemos usar 800×600 no preview se for crítico.) Veredicto: *funciona* – P4 decodifica JPEGs ~5–8 MP. Confiança: **alta**.

# Relatos da comunidade (>1080p no P4)

Até onde achamos, **pouco foi relatado** sobre >1080p funcionar. O mais próximo é o material oficial do Espressif: no *Esp-Techpedia* eles mostram o P4 recebendo **OV5645** em 2592×1944 (5MP) a 15 fps, em modo RGB565. Isso indica que, com sensore OV5645 e ISP/driver ajustados, 5MP foi obtido. Porém, trata-se de dados de laboratório oficial, não uma aplicação de terceiros. Por outro lado, fóruns de usuários mostram tentativas frustradas: e.g. configurações simples com OV5647@1080p RAW10 travam. Não há relatos públicos de 2592×1944 real rodando. 

- **Comprovado funcionar**: Teste Espressif (OV5645 5MP→RGB565 15fps), que sugere o caminho com ISP/PPA. 
- **Comprovado não funcionar**: Usuários colhendo *hang* em 1080p RAW10; não há evidências de >1080p em campo. 
- **Incertos/não testados**: 2592×1944 sem ISP (raw) ou 3264×2448 em qualquer modo, não documentados em lugares públicos. 

Assim, historicamente, não há casos de sucesso independentes acima de 1080p (apenas testes controlados de Espressif). Falhas em 1080p indicam que capturas maiores provavelmente ainda não estão viáveis em código de usuário atual. 

# Tabela de resumo

| Recurso / Caso                          | Funciona        | Não funciona      | Não testado / Incerto        |
|-----------------------------------------|-----------------|-------------------|------------------------------|
| **MIPI-CSI → ISP (pipeline padrão)**     | ≤1920×1080 p/ISP      | >1080p via ISP (limitado) | —                            |
| **MIPI-CSI bypass (raw direto)**        | (teórico sim**) | não capturado em usuário (hang) |  >=1080p RAW pelo usuário: incerto |
| **Sensor MIPI com JPEG/YUV integrado**  | N/A (não há sensor comum suportado)  | — | sem exemplos conhecidos |
| **USB UVC (Host)**                      |  up to 1080p MJPEG (reportado)  | >1080p contínuo: não testado  | 2592×1944 à la carte: incerto |
| **Autofoco UVC**                        | embutido na câmera *excluído* host | — | explicação de UVC CT não implementado |
| **Decoder JPEG P4**                     | até 4K (3840×2160)  | —  | — |
| **Comunitário >1080p**                  | OV5645 5MP @15fps   | 1080p raw travando | Outros capta acima não documentados |

# Recomendações práticas (uso: foto A4 parada)

Para maximizar resolução real em foto estática (página A4 manuscrita):

1. **MIPI-CSI atual**: Com OV5647, só dá até 1080p via ISP. Insuficiente (apenas ~1100px no lado longo).  Possível melhoria: trocar sensor por OV5645 ou similar (5MP) e usar resolução 2592×1944 via CSI (se o driver suportar) – mas *atenção*: testes internos mostram potencial, mas implementações comunitárias atuais ainda não conseguem facilmente (ver [66] vs. [69]). Se tentar, deve ser com driver de baixo nível (bypass + PPA talvez) e esperar bugs.
2. **Rota UVC (USB)**: A câmera *NE-HD362/IMX362* oferece até 12MP hardware, mas no P4 o mais seguro é usá-la em 1920×1080 MJPEG. A 1080p o OCR de manuscrito já foi testado e funciona bem; dá ~1920px no lado longo, o que é *próximo* do mínimo desejado (2448px). Para superar isso, pode-se tentar captura única em 2592×1944 MJPEG. É **viável em tese** (banda USB HS e PSRAM aguentam ~400KB frame), mas não garantido no firmware padrão. 
3. **Interface USB**: Configurar projeto com `CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE=y` (esp_video) ou usar `usb_host_uvc` do esp-iot-solution. Testar primeiro com resoluções menores (720p/1080p) e MJPEG para verificar pipeline USB. 
4. **Bibliotecas**: Use o componente `usb_host_uvc` atualizado (>=v2.4.2) e exemplos básicos. Verifique buffers de PSRAM alocados para receber o frame inteiro (MJPEG).
5. **Autofoco da NE-HD362**: O módulo IMX362 foca automaticamente por hardware. Não há necessidade imediata de implementar CT_FOCUS_AUTO, a menos que notar falta de foco.
6. **VBUS/Waveshare**: A placa fornece USB Host padrão 5 V. A corrente do IMX362 (~0.2 A) é bem dentro do usual de 0.5 A USB2. Não deve exigir alimentação extra.
7. **Prévia (preview)**: Fazer preview em resolução menor (800×600) para enquadrar, e após configurar, disparar captura em alta resolução.

**Câmera NE-HD362/IMX362 no P4**: Possível, mas com ressalvas. Espera-se funcionar até 1080p MJPEG com esforço moderado. Capturar resolução nativa (2448px) é arriscado: *tentar primeiro uma única foto 2592×1944 MJPEG* e checar se o host avalia corretamente o formato. Se falhar, solução alternativa: usar 1080p e, se possível, recortar ou compor a imagem via software/PPA para obter mais detalhe (não ideal). Riscos principais: compatibilidade UVC (descritor), latência da recepção, e atrasos no decodificador JPEG de tamanhos muito grandes. Portanto, **testar rapidamente** 1080p MJPEG para confirmar a rota UVC; se estabilizar, tentar 2–3 MP.

**Resumo**: O caminho de maior resolução real é provavelmente o *USB Host + IMX362*, pois o MIPI atual (OV5647) é limitado a 1080p pelo ISP do P4. Levar em conta a banda USB e possivelmente aceitar ~1920px no maior lado do A4, ou tentar um *stepping stone* 2–3 MP.

**Fontes**: Documentação Espressif e testes: ISP 1080p, decode 4K; exemplos oficiais 5MP; relatos de falha 1080p raw. Outros detalhes de UVC são inferidos de documentos gerais e discussões públicas.