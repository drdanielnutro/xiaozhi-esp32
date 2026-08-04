#include "ui/pv_camera_screen.h"

#include <esp_app_desc.h>
#include <esp_log.h>

#include <utility>

#include "board.h"
#include "display/display.h"
#include "pv_camera.h"
#include "pv_strings.h"
#include "ui/pv_config_gesture.h"
#include "ui/pv_ui_theme.h"

#define TAG "PvCameraScreen"

// O preview reaproveita a MESMA lv_image_dsc_t a cada frame, trocando apenas o
// ponteiro de dados. O cache de imagem do LVGL é indexado pelo PONTEIRO da
// fonte: com ele ligado, o LVGL redesenharia a decodificação antiga e o
// preview congelaria na primeira imagem. Esta build tem o cache desligado
// (LV_CACHE_DEF_SIZE=0) e a função de invalidação (lv_image_cache_drop) mora
// em cabeçalho privado do LVGL, indisponível sem LV_USE_PRIVATE_API. Em vez de
// contornar, a premissa fica travada aqui: ligar o cache tem que quebrar o
// build, não o preview.
#if defined(LV_CACHE_DEF_SIZE) && LV_CACHE_DEF_SIZE > 0
#error "Preview do Professor Virtual exige o cache de imagem do LVGL desligado"
#endif

namespace {

// Altura fixa da barra de ações. Fixa DE PROPÓSITO: o botão de captura da T4
// entra no espaço vazio do meio sem mudar a altura da área de preview — e,
// portanto, sem mudar a escala calculada para a imagem.
constexpr int32_t kActionBarHeight = 68;

constexpr int32_t kScreenPad = 16;
constexpr int32_t kRowGap = 12;

}  // namespace

void PvCameraScreen::Attach(PvCamera* camera, BackHandler on_back) {
    camera_ = camera;
    back_handler_ = std::move(on_back);
}

bool PvCameraScreen::EnsureScreenLocked() {
    if (screen_ != nullptr) {
        return true;
    }

    auto* screen = PvUi::CreateScreen();
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(screen, kScreenPad, 0);
    lv_obj_set_style_pad_row(screen, kRowGap, 0);

    auto* title = lv_label_create(screen);
    lv_label_set_text(title, PvStrings::kCameraTitle);

    // Área do preview: ocupa toda a altura que sobra entre o título e a barra
    // de ações. Sem layout próprio — a imagem e o aviso são centralizados na
    // mão, e ambos ocupam o mesmo lugar (um por vez).
    auto* area = lv_obj_create(screen);
    lv_obj_remove_flag(area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(area, LV_PCT(100));
    lv_obj_set_flex_grow(area, 1);
    lv_obj_set_style_bg_color(area, lv_color_hex(PvUi::kColorSurface), 0);
    lv_obj_set_style_bg_opa(area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(area, 0, 0);
    lv_obj_set_style_radius(area, 12, 0);
    lv_obj_set_style_pad_all(area, 0, 0);
    preview_area_ = area;

    auto* hint = lv_label_create(area);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, LV_PCT(80));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(PvUi::kColorMuted), 0);
    lv_label_set_text(hint, PvStrings::kCameraWarmup);
    lv_obj_center(hint);
    hint_label_ = hint;

    auto* image = lv_image_create(area);
    // CONTAIN calcula sozinho a escala que faz o frame caber no tamanho do
    // objeto preservando a proporção; o tamanho do objeto é definido em
    // ApplyGeometryLocked() e o objeto fica centralizado na área.
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
    // Sem antialias: reduzir 1280x960 para caber na área já custa uma
    // transformação por frame (~2,4 MB lidos); filtrar em cima disso não
    // acrescenta nada num preview de enquadramento a 5 fps.
    lv_image_set_antialias(image, false);
    lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(image);
    image_ = image;

    // Barra de ações. SPACE_BETWEEN com dois filhos deixa o MEIO livre: é ali
    // que o botão de captura da T4 entra, sem mexer no resto do layout.
    auto* bar = lv_obj_create(screen);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, LV_PCT(100), kActionBarHeight);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    action_bar_ = bar;

    auto* back_button = lv_button_create(bar);
    lv_obj_set_style_bg_color(back_button, lv_color_hex(PvUi::kColorSurface), 0);
    lv_obj_add_event_cb(back_button, OnBackClicked, LV_EVENT_CLICKED, this);
    auto* back_label = lv_label_create(back_button);
    lv_label_set_text(back_label, PvStrings::kCameraBackButton);
    lv_obj_center(back_label);

    // Rodapé com a versão: mesmo alvo do gesto de recuperação das outras telas
    // (long-press de 3 s abre a configuração — decisão F1-ConfigGesture). O
    // indicador de conexão carrega o mesmo gesto; os dois caminhos são
    // deliberadamente redundantes e não podem ser removidos.
    auto* version = lv_label_create(bar);
    lv_obj_set_style_text_color(version, lv_color_hex(PvUi::kColorMuted), 0);
    lv_label_set_text_fmt(version, "v%s", esp_app_get_description()->version);
    lv_obj_set_ext_click_area(version, 40);
    PvUi::AttachConfigGesture(version);

    badge_ = PvUi::CreateConnectionBadge(screen);
    PvUi::SetConnectionBadge(badge_, connected_);

    screen_ = screen;
    return true;
}

bool PvCameraScreen::Show() {
    if (camera_ == nullptr) {
        return false;
    }
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr) {
        ESP_LOGW(TAG, "Placa sem display; tela de câmera indisponível");
        return false;
    }

    DisplayLockGuard lock(display);
    if (!EnsureScreenLocked()) {
        return false;
    }

    // Entrar na tela sempre começa do zero: o preview foi religado agora e o
    // ISP demora alguns segundos para entregar o primeiro frame.
    DetachImageLocked();
    ReleaseFrameLocked();
    if (hint_label_ != nullptr) {
        lv_obj_remove_flag(hint_label_, LV_OBJ_FLAG_HIDDEN);
    }

    active_ = true;
    lv_screen_load(screen_);
    return true;
}

void PvCameraScreen::Hide() {
    active_ = false;
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr || screen_ == nullptr) {
        return;
    }

    DisplayLockGuard lock(display);
    // ORDEM OBRIGATÓRIA: primeiro o LVGL para de referenciar o buffer, só
    // depois ele volta a ser do escritor. Ainda sob o lock, portanto sem
    // nenhuma janela em que a task do LVGL desenhe um buffer já devolvido.
    DetachImageLocked();
    ReleaseFrameLocked();
    if (hint_label_ != nullptr) {
        lv_obj_remove_flag(hint_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void PvCameraScreen::UpdateFrame() {
    if (camera_ == nullptr || !active_ || image_ == nullptr) {
        // Aviso que sobrou na fila depois de sair da tela: nada foi
        // emprestado, então não há nada a devolver.
        return;
    }
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr) {
        return;
    }

    // O swap INTEIRO acontece dentro de UM único lock do display. A task do LVGL
    // só desenha segurando esse mesmo lock, então enquanto estivermos aqui ela
    // não está lendo a imagem: devolver o frame antigo e apontar para o novo é
    // atômico do ponto de vista do desenho.
    DisplayLockGuard lock(display);

    // 1) Devolve o frame que estava na tela. Precisa vir antes do Acquire: a
    //    API do PvCamera recusa um segundo empréstimo com o anterior aberto.
    DetachImageLocked();
    ReleaseFrameLocked();

    // 2) Pega o frame estável mais recente (último-frame-vence).
    PvCameraPreviewFrame frame;
    if (!camera_->AcquireDisplayFrame(frame) || frame.data == nullptr || frame.width == 0 ||
        frame.height == 0) {
        // Só acontece antes do primeiro frame ou na janela estreita em que o
        // escritor invalidou o frame pronto entre o Release e o Acquire. A
        // imagem fica vazia por um período de frame (200 ms) — preferível a
        // continuar exibindo um buffer que já voltou a ser do escritor.
        return;
    }
    frame_borrowed_ = true;

    // 3) Atualiza a descrição da imagem. Zero cópia: o LVGL lê direto do
    //    buffer emprestado, em RGB565 little endian.
    image_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    image_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
    image_dsc_.header.flags = 0;
    image_dsc_.header.w = frame.width;
    image_dsc_.header.h = frame.height;
    image_dsc_.header.stride = static_cast<uint32_t>(frame.stride);
    image_dsc_.data = frame.data;
    image_dsc_.data_size = static_cast<uint32_t>(frame.len);

    ApplyGeometryLocked(frame.width, frame.height);

    // 4) Publica. lv_image_set_src() relê o cabeçalho da descrição e invalida
    //    o objeto; com o cache desligado (ver o #error no topo), cada desenho
    //    volta a ler `image_dsc_.data` — que é o buffer novo.
    lv_image_set_src(image_, &image_dsc_);
    lv_obj_remove_flag(image_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(image_);

    if (hint_label_ != nullptr && !lv_obj_has_flag(hint_label_, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(hint_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void PvCameraScreen::SetConnected(bool connected) {
    connected_ = connected;
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr || badge_.dot == nullptr) {
        return;
    }
    DisplayLockGuard lock(display);
    PvUi::SetConnectionBadge(badge_, connected);
}

void PvCameraScreen::DetachImageLocked() {
    if (image_ == nullptr) {
        return;
    }
    lv_image_set_src(image_, nullptr);
    lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);
    image_dsc_.data = nullptr;
    image_dsc_.data_size = 0;
}

void PvCameraScreen::ReleaseFrameLocked() {
    if (!frame_borrowed_) {
        return;
    }
    frame_borrowed_ = false;
    camera_->ReleaseDisplayFrame();
}

void PvCameraScreen::ApplyGeometryLocked(uint16_t width, uint16_t height) {
    if (width == 0 || height == 0 || preview_area_ == nullptr || image_ == nullptr) {
        return;
    }
    if (width == applied_width_ && height == applied_height_) {
        return;
    }

    // O tamanho útil só é conhecido depois que o flex resolve o flex_grow da
    // área de preview; a tela ainda pode não ter sido desenhada uma vez.
    lv_obj_update_layout(screen_);
    const int32_t area_w = lv_obj_get_content_width(preview_area_);
    const int32_t area_h = lv_obj_get_content_height(preview_area_);
    if (area_w <= 0 || area_h <= 0) {
        return;
    }

    // Escala inteira do LVGL (256 = 1:1). O frame do sensor é 1280x960 e a
    // área útil da 7B fica em torno de 992x430, então o preview roda perto de
    // 115/256 (~0,45): a transformação percorre a área de DESTINO, ~430 mil
    // pixels por frame. Se 5 fps não se sustentarem na medição física da T6,
    // os botões são o intervalo do pv_camera.cc ou a PPA do P4 — não este
    // cálculo.
    int32_t scale_x = area_w * LV_SCALE_NONE / width;
    int32_t scale_y = area_h * LV_SCALE_NONE / height;
    int32_t scale = scale_x < scale_y ? scale_x : scale_y;
    if (scale > LV_SCALE_NONE) {
        scale = LV_SCALE_NONE;  // nunca ampliar: só borraria a imagem
    }
    if (scale < 1) {
        scale = 1;
    }

    lv_obj_set_size(image_, width * scale / LV_SCALE_NONE, height * scale / LV_SCALE_NONE);
    lv_obj_center(image_);
    applied_width_ = width;
    applied_height_ = height;
    ESP_LOGI(TAG, "Preview %ux%u em %" LV_PRId32 "x%" LV_PRId32 " (escala %" LV_PRId32 "/256)",
             (unsigned)width, (unsigned)height, area_w, area_h, scale);
}

void PvCameraScreen::OnBackClicked(lv_event_t* e) {
    auto* self = static_cast<PvCameraScreen*>(lv_event_get_user_data(e));
    // Roda na task do LVGL: só sinaliza o PvApp, que troca a tela e desliga o
    // preview na própria task dele.
    if (self != nullptr && self->back_handler_) {
        self->back_handler_();
    }
}
