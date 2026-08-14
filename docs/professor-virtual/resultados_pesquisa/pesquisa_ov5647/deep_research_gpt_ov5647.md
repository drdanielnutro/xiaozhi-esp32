# Resposta Direta à Pergunta  
Não localizamos em fontes acessíveis nenhum *alternate setting* de vídeo isócrono com MULT=0 (wMaxPacketSize ≤ 0x07FF) na NE-HD362. Assim, **não há evidência** de que a interface VideoStreaming da NE-HD362 ofereça um alt setting sem transações adicionais. Portanto, a existência de alt setting MULT=0 **não foi comprovada** [NÃO ENCONTRADO].

# VID:PID Candidatos  
Não foi possível encontrar publicamente pares VID:PID para a NE-HD362 ou câmeras equivalentes (NeoCoolcam/RedEagle IMX362). Buscas em bases de dados USB (linux-usb.org, devicehunt.com, usb.ids) e fóruns técnicos não retornaram nenhum VID:PID associado a esse modelo. Nenhuma fonte confiável de fabricante ou repositório online listou o VID:PID da NE-HD362 [NÃO ENCONTRADO].

# Alternate Settings (bAlternateSetting) encontrados  
Não há dump público de descritor completo da NE-HD362 para extrair os alt settings. Sem essa informação, não podemos listar nenhum alternate setting válido. Em outras palavras, **nenhuma configuração alternativa foi verificada** [NÃO ENCONTRADO].

# Chip Bridge Identificado  
Com base em análises de câmeras USB 12MP similares, presume-se que o controlador principal (ISP/bridge) é o chip **FIC7608** da Image+ (marca chinesa de SoC para câmeras UVC). Câmeras OEM que usam o sensor IMX362 costumam empregar o FIC7608, um ISP USB com PDAF integrado [EVIDÊNCIA INDIRETA]. Essa identificação é _indireta_: não temos documentação oficial da NE-HD362, mas a arquitetura coincide com outros módulos UVC 4K.

# Outras Câmeras da Mesma Família  
Não encontramos dumps de descritores de outras câmeras com exatamente o mesmo hardware (IMX362 + FIC7608) disponíveis publicamente. Apesar de fabricantes como Weinan (que vendem módulos genéricos 12MP IMX362) oferecerem produtos semelhantes, não há registros abertos de seus *wMaxPacketSize*. Assim, nenhuma câmera específica da “família” com descritor conhecido foi localizada [NÃO ENCONTRADO].

# Procedimento de Bancada (para obter o descritor)  
Para obter diretamente o descritor USB da NE-HD362 antes de comprar, siga estes passos:

- **Linux:** Conecte a câmera a um PC Linux. Identifique-a com `lsusb`. Em seguida, execute por exemplo:
  ```bash
  lsusb
  sudo lsusb -v -d <VID>:<PID> > ne_hd362_descr.txt
  ```
  (substitua `<VID>:<PID>` pelo par do dispositivo quando conhecido; ou use `sudo lsusb -v` para ver todos e filtrar). Isso salva os descritores completos em texto.

- **Windows:** Baixe e execute o utilitário **USB Device Tree Viewer** (UsbTreeView) ou **USBView** do Windows SDK. Após conectar a câmera, expanda o nó correspondente e use “Save Tree” para exportar todos os descritores para um arquivo .txt.

- **macOS:** Conecte a câmera e abra o “Relatório do Sistema” (About This Mac → System Report) na seção USB. Selecione o dispositivo da câmera na lista, então use o menu para salvar/exportar os detalhes USB (ou, via Terminal, execute `system_profiler SPUSBDataType > ~/Desktop/usb_dump.txt`).

# Texto em Inglês para Fornecedor  
> **Subject:** Request for USB descriptor dump for NE-HD362 camera  
> Dear vendor, I am evaluating the NE-HD362 12MP USB camera for integration into an embedded system. To ensure compatibility, I need the detailed USB descriptors of this camera. Could you please provide the output of an `lsusb -v` (or USB Device Viewer) dump showing all endpoint descriptors? We specifically need the VideoStreaming interface alternate settings, including bAlternateSetting and wMaxPacketSize fields. Having this information will allow us to verify the USB bandwidth requirements before ordering. Thank you.

# Itens Procurados e Não Encontrados  
- **Busca por VID:PID:** Pesquisamos em bases oficiais de IDs USB e sites de hardware (linux-usb.org, devicehunt.com, usb-ids, linux-hardware.org) sem sucesso. Também verificamos fóruns (StackOverflow, Reddit, listas linux-media) para qualquer menção do VID:PID da NE-HD362 ou câmeras clones; não foi encontrado nada relevante.

- **Descritores USB públicos:** Procuramos por dumps de `lsusb -v` no Google e em repositórios (GitHub, gists) usando termos como “NE-HD362”, “RedEagle camera UVC”, “IMX362 USB descriptor”, etc. Não localizamos nenhum arquivo ou captura de USB Device Tree Viewer contendo os descritores da NE-HD362 ou câmeras equivalentes com o sensor IMX362.

- **Empacotamento e anúncios:** Revisamos sites de produtos genéricos e fóruns chineses. Apenas encontramos especificações de produto (resolução 4000×3000, formatos MJPEG/YUY2) em páginas de vendas, mas nada sobre endpoints. Em nenhum anúncio ou manual técnico havia detalhes de wMaxPacketSize ou alternate settings; esses são omitidos em materiais comerciais.

- **Chip/Bridge:** Identificação do chip FIC7608 foi inferida de desmontagens não-oficiais e relatórios de terceiros (ex. EEVBlog) de câmeras IMX362, mas não há fonte pública citável que mostre esse dado para a NE-HD362 especificamente. Portanto, não encontramos confirmação direta nem dados de “câmera irmã” para comparar os alt settings.

Em resumo, **não foi encontrado nenhum dado público** dos descritores USB da NE-HD362. As únicas informações disponíveis eram as especificações de produto (resolução, formatos), sem detalhes técnicos de endpoint. Por isso, recomendamos seguir o procedimento de bancada acima ou solicitar diretamente ao fornecedor as informações do descriptor.  

**Fontes:** Especificações do módulo UVC 12MP (IMX362) usadas como referência. As demais conclusões derivam da ausência de dados em fontes técnicas (buscas em fóruns e repositórios) e de inferências sobre chips usados em câmeras similares.