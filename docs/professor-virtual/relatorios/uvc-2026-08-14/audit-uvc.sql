-- Snapshot factual da investigação UVC, recontado em 2026-08-14.
-- Este arquivo serializa somente valores conferidos nos logs brutos e no
-- diário da F2B. Ele não atribui causa. Os caminhos são relativos ao repo.

-- Dataset usado na tabela cronológica do relatório.
WITH test_matrix (
    ordem,
    teste,
    camada,
    variavel,
    evidencia,
    resultado_factual,
    limite_da_conclusao
) AS (
    VALUES
    (1, 'Rodada 1', 'esp_video/V4L2 2.0.1 + usb_host_uvc 2.4.2', 'baseline: 2 URBs de 3072 B', 't5-rodada1-isoc-sem-frames.log', 'enumeração e formato funcionaram; zero frame; 19 asserts usbh_dev_close no log', 'o baseline falhou e o teardown também falhou'),
    (2, 'Rodada 2', 'esp_video/V4L2 2.0.1 + usb_host_uvc 2.4.2', 'DEBUG e checagem EOH ligada', 't5-rodada2-eoh-rejeita-pacotes.log', '13.140 linhas invalid UVC payload header; muitos headers 0c 4d', 'a checagem de header rejeitava payloads desta câmera'),
    (3, 'Rodada 3', 'esp_video/V4L2', 'EOH desligado e dreno no teardown', 'fase-2b.md; sem log bruto separado', 'o diário registra escada sem crash e sem frame', 'resultado tem força menor por não haver log separado'),
    (4, 'Rodada 4', 'esp_video/V4L2 + DEBUG por pacote', 'replug físico fresco', 't5-rodada4-800x600-pass.log + JPEG', '1 JPEG 800x600 de 32.405 B; 15.011 frame error; quatro degraus seguintes falharam no open', 'prova uma captura possível; ela não voltou a ocorrer, mas o mesmo ELF não foi reexecutado nos logs'),
    (5, 'Rodada 5', 'esp_video/V4L2', 'device único e reconfiguração por degrau', 't5-rodada5-ciclo-ok-camera-travada.log', '5 degraus reconfigurados; 5 falhas sem frame', 'reopen deixou de bloquear a escada, mas a captura não apareceu'),
    (6, 'Rodada 6', 'esp_video/V4L2', 'root port inicialmente sem power e posterior power-on', 't5-rodada6-vbus-nao-destrava.log', 'enumeração ocorreu; 5 falhas sem frame', 'o comando de root port não recuperou o streaming neste ensaio'),
    (7, 'Rodadas 7–14', 'esp_video/V4L2', 'replug, reset, laço, fps 10 e janela 30 s', 'run11, run13, run14 + fase-2b.md', 'run11 teve frame errors; runs 13/14 tiveram zero PASS; fps 10 foi revertido', 'não houve captura válida; nem todas as rodadas têm log separado'),
    (8, 'Rodada 15', 'esp_video 2.3.0 + usb_host_uvc 2.5.1', 'URBs de 4 pacotes ativadas', 't5-run15-stack251-silencio.log', '49 alocações com 4 pacotes; 47 RUNG FAIL registradas; zero PASS', 'a geometria antiga de 1 pacote não explica sozinha a falha'),
    (9, 'Rodada 17', 'controle V4L2 2.0.1/2.4.2', 'binário b8ed27e com flood DEBUG', 't5-run17-controle-242-canal-vivo.log', '81.798 linhas frame error; zero frame válido no arquivo', 'há atividade de transporte, mas não captura válida'),
    (10, 'Rodada 18', 'spike direto 2.5.1', '8 URBs, 1 pacote de 3072 B', 't5-run18-e1-urb1pacote-mudo.log', 'alocação confirma 1 pacote; stats do app ficaram em zero; zero PASS', 'igualar a geometria não restaurou a captura'),
    (11, 'Rodada 19', 'spike direto 2.5.1', 'sem sequência de VBUS por software', 't5-run19-e2-sem-vbus-mudo.log', 'stats do app em zero; zero PASS', 'retirar a sequência de VBUS não restaurou a captura'),
    (12, 'Rodada 20', 'spike direto 2.4.2', 'pilha antiga', 't5-run20-e5lite-242-mudo.log', 'zero PASS; o final registra desconexão e daemon ainda vivo', 'trocar somente para a pilha antiga não restaurou a captura'),
    (13, 'Rodada 21', 'spike direto 2.4.2', '4 URBs', 't5-run21-e6-4urbs-mudo.log', 'zero PASS; o final registra desconexão e daemon ainda vivo', '4 URBs não restauraram a captura'),
    (14, 'Rodada 22', 'spike direto 2.4.2', 'DEBUG por pacote', 't5-run22-e7-flood-acorda-canal.log', '10.484 linhas frame error; zero frame válido', 'o log alterou o comportamento observado, mas não identifica a causa'),
    (15, 'Rodada 23A', 'spike direto 2.5.1 instrumentado', 'high-bandwidth automático', 't5-run23a-controle-hb-headers-invalidos.log', 'no t=17 s do primeiro degrau: 248.915 completions, 13.202 pacotes com dados, 235.713 headers inválidos; zero frame', 'o canal tinha tráfego; o mecanismo da perda não foi demonstrado'),
    (16, 'Rodada 23B', 'spike direto 2.5.1 instrumentado', 'alt 6, 944 B, MULT=0', 't5-run23b-alt944-mult0-headers-invalidos.log', 'no t=17 s do primeiro degrau: 248.911 completions, 40.778 pacotes com dados, 208.133 headers inválidos; zero frame', 'o payload commitado seguia maior que o pipe; este teste sozinho não valida MULT=0'),
    (17, 'Rodada 24', 'exemplo oficial IDF 6.0.2', 'seleção automática; main.c idêntico ao oficial', 't5-run24-exemplo-oficial-idf602-mudo.log', '133 starts, 132 timeouts, zero New frame!', 'o exemplo oficial também falhou nesta combinação'),
    (18, 'Rodada 25', 'exemplo oficial + patch de alt', 'alt 4, payload 640, pipe 640, MULT=0', 't5-run25-exemplo-oficial-alt640-mult0-mudo.log', '198 starts, 197 timeouts, zero New frame!', 'MULT=0 corretamente casado não resolveu; a causa segue aberta')
)
SELECT * FROM test_matrix ORDER BY ordem;

-- Dataset usado no gráfico do relatório. Cada arquivo terminou durante a
-- última iteração, por isso starts = timeouts + 1.
WITH official_example_counts (
    teste,
    stream_starts_without_new_frame,
    timeout_lines,
    new_frame_lines
) AS (
    VALUES
    ('Rodada 24', 133, 132, 0),
    ('Rodada 25', 198, 197, 0)
)
SELECT * FROM official_example_counts ORDER BY teste;
