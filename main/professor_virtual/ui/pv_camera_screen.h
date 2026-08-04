#ifndef PV_UI_PV_CAMERA_SCREEN_H
#define PV_UI_PV_CAMERA_SCREEN_H

#include <lvgl.h>

#include <cstdint>
#include <functional>

#include "ui/pv_ui_theme.h"

class PvCamera;

// Tela de preview da câmera do Professor Virtual (F2/T3).
//
// DONO ÚNICO do par (lv_image_dsc_t, empréstimo do frame). A descrição da
// imagem e a flag `frame_borrowed_` moram aqui, e não no PvApp, porque as duas
// coisas só podem mudar juntas e sob o MESMO DisplayLockGuard: a descrição diz
// ao LVGL onde estão os pixels e o empréstimo é o que impede a task da câmera
// de sobrescrevê-los. Separar os dois donos abriria a janela em que o LVGL
// ainda referencia um buffer já devolvido ao escritor.
//
// Ciclo de vida (quem manda é o PvApp, na task dele):
//   Show()  -> tela carregada, imagem vazia, aviso "Preparando a câmera...";
//              o PvApp liga o preview logo depois;
//   UpdateFrame() -> um swap de frame por aviso (ver o .cc);
//   Hide()  -> imagem esvaziada e empréstimo devolvido, nessa ordem, sob o
//              lock; o PvApp já desligou o preview antes de chamar.
//
// A tela é criada sob demanda e mantida viva depois disso, como as telas de
// rota: nenhum ponteiro de tela pode ser invalidado enquanto a tela de
// configuração guarda a "tela anterior" para voltar.
//
// Todos os métodos públicos rodam na task do PvApp e pegam o DisplayLockGuard
// internamente. Em placa sem display tudo vira no-op e IsActive() fica false.
class PvCameraScreen {
public:
    // Roda NA TASK DO LVGL (toque no botão "Voltar"): deve apenas sinalizar.
    using BackHandler = std::function<void()>;

    PvCameraScreen() = default;
    PvCameraScreen(const PvCameraScreen&) = delete;
    PvCameraScreen& operator=(const PvCameraScreen&) = delete;

    // `camera` não é possuído: é o membro do PvApp, que vive para sempre.
    void Attach(PvCamera* camera, BackHandler on_back);

    // Cria (na primeira vez) e carrega a tela. false quando não há display ou
    // câmera — nesse caso o chamador não deve ligar o preview.
    bool Show();

    // Esvazia a imagem e devolve o empréstimo. Não desliga o preview: quem
    // manda no motor da câmera é o PvApp (helper único LeaveCameraScreen).
    void Hide();

    bool IsActive() const { return active_; }

    // Consome o aviso de frame novo: devolve o frame exibido, pega o mais
    // recente e troca a fonte da imagem — tudo sob um único lock do display.
    void UpdateFrame();

    // Indicador de conexão com o backend, como nas demais telas do PV.
    void SetConnected(bool connected);

private:
    static void OnBackClicked(lv_event_t* e);

    // Exigem o lock do display já tomado pelo chamador.
    bool EnsureScreenLocked();
    // Faz o LVGL parar de referenciar o buffer emprestado (src nula + oculta).
    void DetachImageLocked();
    // Devolve o empréstimo ao PvCamera. Sempre DEPOIS do Detach.
    void ReleaseFrameLocked();
    void ApplyGeometryLocked(uint16_t width, uint16_t height);

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* preview_area_ = nullptr;
    lv_obj_t* image_ = nullptr;
    lv_obj_t* hint_label_ = nullptr;
    lv_obj_t* action_bar_ = nullptr;  // T4 pendura aqui o botão de captura
    PvUi::ConnectionBadge badge_;

    // Descrição da imagem exibida. Aponta DIRETO para o buffer emprestado do
    // PvCamera (RGB565 LE, sem cópia): trocar de frame é trocar `data`.
    lv_image_dsc_t image_dsc_{};
    bool frame_borrowed_ = false;

    // Geometria já aplicada ao objeto de imagem; recalculada só quando o
    // tamanho do frame muda (na prática, uma vez).
    uint16_t applied_width_ = 0;
    uint16_t applied_height_ = 0;

    bool active_ = false;
    bool connected_ = false;

    PvCamera* camera_ = nullptr;
    BackHandler back_handler_;
};

#endif  // PV_UI_PV_CAMERA_SCREEN_H
