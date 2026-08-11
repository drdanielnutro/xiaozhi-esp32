#ifndef PV_UVC_SPIKE_H
#define PV_UVC_SPIKE_H

// ===========================================================================
// SPIKE DE BANCADA DA F2B — FERRAMENTA DE DIAGNÓSTICO, NUNCA PRODUÇÃO.
//
// Existe para responder a UMA pergunta: até que resolução a câmera UVC
// (NE-HD362, MJPEG interno) entrega um frame íntegro pela porta USB-A OTG do
// ESP32-P4? A resposta decide a rota de captura da F2B, depois que a validação
// física provou que a CSI OV5647 não sustenta a extração do manuscrito e que o
// teto de 1920x1080 do ISP do P4 é de silício.
//
// O QUE ELE SUBSTITUI: com CONFIG_PV_UVC_SPIKE=y o main.cc não instancia PvApp
// nem Application. Como Board::GetInstance() é lazy, o board — e com ele o CSI,
// a tela, o áudio e a rede — NUNCA é construído: tela apagada é o esperado. O
// spike fala V4L2 direto com o device UVC do componente esp_video, sem passar
// pelo EspVideo nem pelo board (arquitetura "A-sem-board", decision-log F2B).
//
// O QUE ELE FAZ, uma vez, em task própria, antes de dormir para sempre:
//  1. esp_video_init só com `.usb_uvc` (os demais nós ficam NULL);
//  2. enumeração completa do que a câmera anuncia (ENUM_FMT x ENUM_FRAMESIZES
//     x ENUM_FRAMEINTERVALS), em linhas `PV-UVC-ENUM ...`;
//  3. escada FIXA de resoluções (800x600 -> 1920x1080 -> 2592x1944 ->
//     3264x2448 -> 4000x3000 opcional), com o device aberto UMA vez para a
//     escada inteira e reconfiguração por degrau (STREAMOFF -> S_FMT ->
//     REQBUFS -> STREAMON; o re-open por degrau quebra o esp_video 2.0.1 —
//     ver decision-log F2B-SpikeTuningR5), uma linha `PV-UVC-RUNG ...` por
//     degrau e um sumário no fim;
//  4. um dump serial base64 (PvPhotoDump) por degrau aprovado, que o
//     `scripts/pv/extract_jpeg_dump.py --all` reconstrói em JPEGs no Mac.
//
// Todas as linhas `PV-UVC-*` saem por printf, imunes ao nível de log: são a
// evidência da bancada e o script do host casa linha a linha.
//
// Este arquivo é autocontido de propósito: apagar pv_uvc_spike.{h,cc}, o ramo
// do main.cc, o `config PV_UVC_SPIKE` e a variante de release remove o caminho
// inteiro (destino do código decidido na T6 da fase).
// ===========================================================================

namespace PvUvcSpike {

// Cria a task do spike e devolve na hora: quem chama (app_main) pode retornar,
// porque daqui em diante o spike é a única coisa que roda nesta build.
//
// false só quando a task não pôde ser criada. Nesse caso NÃO caia no app
// normal — o gate existe justamente para o board nunca subir.
bool Start();

}  // namespace PvUvcSpike

#endif  // PV_UVC_SPIKE_H
