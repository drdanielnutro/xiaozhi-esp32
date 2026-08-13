#ifndef PV_UVC_DIRECT_H
#define PV_UVC_DIRECT_H

// ===========================================================================
// SPIKE DIRETO DE usb_host_uvc — FERRAMENTA DE BANCADA DA F2B, NUNCA PRODUÇÃO.
//
// Existe para responder a UMA pergunta (rodada 16, plano_codex_v2.md): o
// silêncio do canal ISOC da NE-HD362 nasce na costura esp_video<->usb_host_uvc
// ou abaixo dela? A rodada 15 provou que o bug de URB da 2.4.2 (1 pacote/URB)
// não era a causa: com a 2.5.1 as URBs sobem com 4 pacotes e o canal segue
// mudo. Este spike remove o V4L2/esp_video da equação e fala DIRETO com a API
// pública do usb_host_uvc 2.5.1, com o transporte desacoplado dos frame
// buffers: 8 URBs independentes (urb_size=0 => padrão oficial de 4x MPS) e 4
// frame buffers de 6 MB em PSRAM abertos UMA vez para a escada inteira.
//
// O QUE ELE SUBSTITUI: com CONFIG_PV_UVC_DIRECT_SPIKE=y o main.cc não
// instancia PvApp nem Application. Como Board::GetInstance() é lazy, o board —
// e com ele o CSI, a tela, o áudio e a rede — NUNCA é construído: tela apagada
// é o esperado. Exclusão mútua com CONFIG_PV_UVC_SPIKE garantida no Kconfig
// (o spike V4L2 continua existindo para A/B).
//
// O QUE ELE FAZ, em task própria, em laço por replug (mesmo protocolo da
// rodada 15 para comparabilidade 1:1):
//  1. instala o USB host ele mesmo (ciclo de VBUS da rodada 6) e o driver
//     uvc_host com task de background;
//  2. loga a lista de frames anunciada (uvc_host_get_frame_list) e o
//     descritor completo (uvc_host_desc_print);
//  3. escada FIXA (800x600 -> 1920x1080 -> 2592x1944 -> 3264x2448 ->
//     4000x3000 opcional) num único stream_open; por degrau:
//     format_select -> start -> warmup -> janela de 30 s -> stop;
//  4. telemetria AGREGADA por segundo (PV-UVC-STATS: callbacks, bytes,
//     eventos) — nunca log por pacote; callbacks só incrementam atômicos e
//     enfileiram o frame escolhido (dump SEMPRE na task, nunca no callback);
//  5. um dump serial base64 (PvPhotoDump) por degrau aprovado — mesmas linhas
//     PV-UVC-RUNG/SUMARIO/DUMP da rodada 15, que o
//     `scripts/pv/extract_jpeg_dump.py --all` já entende.
//
// Este arquivo é autocontido de propósito: apagar pv_uvc_direct.{h,cc}, o
// ramo do main.cc, o `config PV_UVC_DIRECT_SPIKE` e a variante de release
// remove o caminho inteiro (destino do código decidido na T6 da fase).
// ===========================================================================

namespace PvUvcDirect {

// Cria a task do spike e devolve na hora: quem chama (app_main) pode retornar,
// porque daqui em diante o spike é a única coisa que roda nesta build.
//
// false só quando a task não pôde ser criada. Nesse caso NÃO caia no app
// normal — o gate existe justamente para o board nunca subir.
bool Start();

}  // namespace PvUvcDirect

#endif  // PV_UVC_DIRECT_H
