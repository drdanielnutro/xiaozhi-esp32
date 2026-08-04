#ifndef PV_UI_PV_CAMERA_SCREEN_H
#define PV_UI_PV_CAMERA_SCREEN_H

#include <lvgl.h>

#include <cstddef>
#include <cstdint>
#include <functional>

#include "pv_camera.h"
#include "ui/pv_ui_theme.h"

// Tela de câmera do Professor Virtual: preview (F2/T3) e revisão da foto
// capturada (F2/T4).
//
// DONO ÚNICO de tudo que o LVGL referencia nesta tela:
//  - modo preview: a lv_image_dsc_t do preview e o empréstimo do frame
//    (`frame_borrowed_`). Os dois só podem mudar juntos e sob o MESMO
//    DisplayLockGuard: a descrição diz ao LVGL onde estão os pixels e o
//    empréstimo é o que impede a task da câmera de sobrescrevê-los;
//  - modo revisão: o JPEG retirado do PvCamera (`photo_`, liberado com
//    heap_caps_free) e o RGB565 já decodificado PELA TASK DA CÂMERA
//    (`decoded_`, idem). O PvApp TRANSFERE a posse dos DOIS em EnterReview() e
//    não guarda cópia: um único dono elimina a classe de bug em que a tela
//    desenha um buffer que outro dono já liberou.
//
// EXCEÇÃO ÚNICA à posse do JPEG: o dump de diagnóstico (PROVISÓRIO DA F2)
// trabalha por EMPRÉSTIMO, sem cópia. Ao soltar a foto, a tela oferece a posse
// do JPEG ao dump em andamento (PvPhotoDump::TryHandOff); se ele aceitar,
// quem libera passa a ser o dump. Ver pv_photo_dump.h.
//
// Liberação garantida em TODOS os caminhos de saída, porque todos passam por
// Hide() ou ExitReview(): "Nova foto" (ExitReview), "Voltar", gesto de
// configuração, rota nova por re-hidratação e tela de status (todos caem em
// PvApp::LeaveCameraScreen -> Hide).
//
// Ciclo de vida (quem manda é o PvApp, na task dele):
//   Show()        -> tela carregada em modo preview, imagem vazia, aviso
//                    "Preparando a câmera..."; o PvApp liga o preview depois;
//   UpdateFrame() -> um swap de frame por aviso (ver o .cc); no-op em revisão;
//   EnterReview() -> preview sai de cena e a FOTO decodificada entra no lugar;
//   ExitReview()  -> foto e decodificado liberados, volta ao modo preview;
//   Hide()        -> imagem esvaziada, empréstimo devolvido e foto liberada,
//                    nessa ordem, sob o lock.
//
// A tela é criada sob demanda e mantida viva depois disso, como as telas de
// rota: nenhum ponteiro de tela pode ser invalidado enquanto a tela de
// configuração guarda a "tela anterior" para voltar.
//
// Todos os métodos públicos rodam na task do PvApp e pegam o DisplayLockGuard
// internamente. Em placa sem display tudo vira no-op e IsActive() fica false.
class PvCameraScreen {
public:
    // Ações dos botões da barra. O callback do LVGL só as ENCAMINHA: quem
    // decide (e mexe em tela) é sempre a task do PvApp.
    enum class Action : uint8_t {
        Back,        // "Voltar": sai da tela
        Capture,     // "Tirar foto"
        Retake,      // "Nova foto": descarta a revisão e volta ao preview
        ZoomToggle,  // "100%" / "Ajustar"
        Export,      // "Exportar (diagnóstico)" — PROVISÓRIO DA F2
    };

    // Roda NA TASK DO LVGL (toque): deve apenas sinalizar.
    using ActionHandler = std::function<void(Action)>;

    PvCameraScreen() = default;
    PvCameraScreen(const PvCameraScreen&) = delete;
    PvCameraScreen& operator=(const PvCameraScreen&) = delete;

    // `camera` não é possuído: é o membro do PvApp, que vive para sempre.
    void Attach(PvCamera* camera, ActionHandler on_action);

    // Cria (na primeira vez) e carrega a tela. false quando não há display ou
    // câmera — nesse caso o chamador não deve ligar o preview.
    bool Show();

    // Esvazia a imagem, devolve o empréstimo e libera a foto em revisão. Não
    // desliga o preview: quem manda no motor da câmera é o PvApp (helper único
    // LeaveCameraScreen).
    void Hide();

    bool IsActive() const { return active_; }
    bool IsReviewing() const { return active_ && mode_ == Mode::Review; }

    // Estados "esperando uma conclusão que vem de outra task". O PvApp os lê na
    // reconciliação periódica: se o aviso correspondente não coube na fila de
    // eventos, a tela ficaria presa em "Processando..."/"Exportando..." para
    // sempre (revisão F2, P1).
    bool IsCapturing() const { return active_ && mode_ == Mode::Preview && capturing_; }
    bool IsExporting() const { return active_ && exporting_; }

    // Consome o aviso de frame novo: devolve o frame exibido, pega o mais
    // recente e troca a fonte da imagem — tudo sob um único lock do display.
    // No-op em modo revisão: um aviso que sobrou na fila depois do StopPreview
    // não pode substituir a foto que a criança está olhando.
    void UpdateFrame();

    // Feedback imediato do botão de captura (desabilitado + "Processando...").
    void SetCapturing(bool capturing);
    // Captura falhou: volta o botão e explica, sem sair do modo preview.
    void ShowCaptureFailed();

    // Entra em revisão TOMANDO A POSSE dos dois buffers de `result` (o JPEG e o
    // RGB565 já decodificado pela task da câmera) e exibe a foto no lugar do
    // preview. Em qualquer caminho de falha os DOIS buffers são liberados aqui
    // mesmo — o chamador nunca precisa desfazer nada.
    //
    // Como a decodificação já veio pronta, tudo o que sobra é a troca de tela:
    // o método é rápido e cabe inteiro sob um DisplayLockGuard.
    bool EnterReview(const PvCameraCaptureResult& result);

    // Libera foto + decodificado e volta ao modo preview (imagem vazia). O
    // PvApp religa o preview depois.
    void ExitReview();

    // Alterna entre "cabe na tela" (contain) e 1:1 com scroll/pan.
    void ToggleZoom();

    // Estado do botão de exportação (PROVISÓRIO DA F2).
    void SetExporting(bool exporting);
    void ShowExportDone();
    void ShowExportBusy();

    // Só para o PvApp montar o pedido de dump; o ponteiro pertence à tela e é
    // válido apenas na task do PvApp enquanto IsReviewing() for verdadeiro.
    const PvCameraJpeg* photo() const { return photo_.data != nullptr ? &photo_ : nullptr; }

    // Indicador de conexão com o backend, como nas demais telas do PV.
    void SetConnected(bool connected);

private:
    enum class Mode : uint8_t { Preview, Review };

    static void OnBackClicked(lv_event_t* e);
    static void OnCaptureClicked(lv_event_t* e);
    static void OnRetakeClicked(lv_event_t* e);
    static void OnZoomClicked(lv_event_t* e);
    static void OnExportClicked(lv_event_t* e);
    static void Dispatch(lv_event_t* e, Action action);

    // Exigem o lock do display já tomado pelo chamador.
    bool EnsureScreenLocked();
    // Faz o LVGL parar de referenciar o buffer emprestado (src nula + oculta).
    void DetachImageLocked();
    // Devolve o empréstimo ao PvCamera. Sempre DEPOIS do Detach.
    void ReleaseFrameLocked();
    // Libera o JPEG e o decodificado. Sempre DEPOIS do Detach.
    void ReleasePhotoLocked();
    void ApplyGeometryLocked(uint16_t width, uint16_t height);
    void ApplyZoomLocked();
    void UpdateActionBarLocked();
    void SetStatusLocked(const char* text);

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* preview_area_ = nullptr;
    lv_obj_t* image_ = nullptr;
    lv_obj_t* hint_label_ = nullptr;
    lv_obj_t* action_bar_ = nullptr;
    lv_obj_t* capture_button_ = nullptr;
    lv_obj_t* capture_label_ = nullptr;
    lv_obj_t* retake_button_ = nullptr;
    lv_obj_t* zoom_button_ = nullptr;
    lv_obj_t* zoom_label_ = nullptr;
    lv_obj_t* export_button_ = nullptr;
    lv_obj_t* export_label_ = nullptr;
    PvUi::ConnectionBadge badge_;

    // Descrição da imagem de PREVIEW. Aponta DIRETO para o buffer emprestado
    // do PvCamera (RGB565 LE, sem cópia): trocar de frame é trocar `data`.
    lv_image_dsc_t image_dsc_{};
    bool frame_borrowed_ = false;

    // Descrição da FOTO em revisão. Separada da de preview de propósito: cada
    // uma tem um dono de buffer diferente e misturá-las apagaria a fronteira.
    lv_image_dsc_t photo_dsc_{};
    PvCameraJpeg photo_{};
    uint8_t* decoded_ = nullptr;
    size_t decoded_len_ = 0;
    uint16_t decoded_width_ = 0;
    uint16_t decoded_height_ = 0;
    size_t decoded_stride_ = 0;

    // Geometria já aplicada ao objeto de imagem NO MODO PREVIEW; recalculada
    // só quando o tamanho do frame muda (na prática, uma vez) ou quando a
    // revisão mexeu no objeto (zerada em ExitReview).
    uint16_t applied_width_ = 0;
    uint16_t applied_height_ = 0;

    Mode mode_ = Mode::Preview;
    bool capturing_ = false;
    bool exporting_ = false;
    bool zoom_actual_size_ = false;
    bool active_ = false;
    bool connected_ = false;

    // Rótulo de medições da foto ("1280×960 · 342 KB"). Buffer próprio porque
    // o lv_label copia o texto no momento da chamada, mas montá-lo com
    // snprintf num membro deixa o estado inspecionável.
    char info_text_[48] = {0};

    PvCamera* camera_ = nullptr;
    ActionHandler action_handler_;
};

#endif  // PV_UI_PV_CAMERA_SCREEN_H
