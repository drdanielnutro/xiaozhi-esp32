#include "ui/pv_camera_screen.h"

#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#include <cstdio>
#include <utility>

#include "board.h"
#include "display/display.h"
#include "pv_camera.h"
#include "pv_photo_dump.h"  // PROVISÓRIO DA F2 (handoff de posse do JPEG)
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

// Largura fixa da barra de ações, que vive na LATERAL DIREITA. Fixa DE
// PROPÓSITO: os botões de captura e de revisão entram e saem, e o rótulo do
// zoom alterna entre "100%" e "Ajustar" — com largura de conteúdo, cada uma
// dessas trocas mudaria a largura da área de preview e forçaria recálculo de
// escala no meio da sessão.
//
// A barra ficou na lateral porque a imagem agora é RETRATO numa tela
// paisagem: nessa combinação a escala é limitada pela ALTURA, então altura é
// o recurso caro e largura é barata. Tirar a barra de baixo devolveu ~80 px
// de altura à imagem; 160 px de largura custam quase nada (a largura só
// passaria a limitar acima de ~570 px) e ainda acomodam o indicador de
// conexão no topo da coluna.
constexpr int32_t kActionBarWidth = 160;

// Espaço reservado no topo da barra para o indicador de conexão, que é
// desenhado por cima (fica fora do layout, ancorado no canto superior
// direito). Sem esta folga ele cobriria o primeiro botão.
constexpr int32_t kBadgeClearance = 48;

constexpr int32_t kScreenPad = 16;
constexpr int32_t kRowGap = 12;

// Botão do PV: cor de superfície, e um estado desabilitado explícito. Sem esta
// última parte um botão desabilitado continuaria com a mesma cor do habilitado
// (o estilo é aplicado ao estado padrão) e a criança não veria diferença.
lv_obj_t* CreateActionButton(lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* user_data,
                             lv_obj_t** out_label) {
    auto* button = lv_button_create(parent);
    lv_obj_set_style_bg_color(button, lv_color_hex(PvUi::kColorSurface), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(PvUi::kColorSurface), LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(button, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(button, lv_color_hex(PvUi::kColorMuted), LV_STATE_DISABLED);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);
    // Todos com a mesma largura na coluna lateral. A ALTURA continua sendo de
    // conteúdo: rótulos longos ("Exportar (diagnóstico)") quebram em duas
    // linhas sem consequência, porque o que precisa ser fixo é a largura.
    lv_obj_set_width(button, LV_PCT(100));
    auto* label = lv_label_create(button);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    if (out_label != nullptr) {
        *out_label = label;
    }
    return button;
}

void SetHidden(lv_obj_t* obj, bool hidden) {
    if (obj == nullptr) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// Libera os DOIS buffers de uma captura cuja posse já é da tela. Existe para
// que nenhum caminho de erro do EnterReview() esqueça um deles: com o JPEG e o
// RGB565 andando juntos, meia liberação vaza centenas de KB ou megabytes.
void ReleaseCaptureResult(const PvCameraCaptureResult& result) {
    if (result.jpeg.data != nullptr) {
        heap_caps_free(result.jpeg.data);
    }
    if (result.rgb != nullptr) {
        heap_caps_free(result.rgb);
    }
}

void SetEnabled(lv_obj_t* obj, bool enabled) {
    if (obj == nullptr) {
        return;
    }
    if (enabled) {
        lv_obj_remove_state(obj, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(obj, LV_STATE_DISABLED);
    }
}

}  // namespace

void PvCameraScreen::Attach(PvCamera* camera, ActionHandler on_action) {
    camera_ = camera;
    action_handler_ = std::move(on_action);
}

bool PvCameraScreen::EnsureScreenLocked() {
    if (screen_ != nullptr) {
        return true;
    }

    // Raiz em LINHA: conteúdo à esquerda, barra de ações na lateral direita.
    auto* screen = PvUi::CreateScreen();
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(screen, kScreenPad, 0);
    lv_obj_set_style_pad_column(screen, kRowGap, 0);

    // Coluna de conteúdo: consome toda a largura que sobra da barra e empilha
    // o rótulo de estado sobre a área de preview. Puramente estrutural —
    // transparente e sem padding, para não roubar área da imagem.
    auto* content = lv_obj_create(screen);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_height(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, kRowGap, 0);
    content_ = content;

    // Sem título: o rótulo de estado logo abaixo já diz o que fazer, e numa
    // tela paisagem exibindo página RETRATO a altura é o recurso escasso —
    // cada linha a menos vira área de imagem. A constante kCameraTitle
    // permanece em pv_strings.h para outros usos.

    // Rótulo ÚNICO de estado/medições. Fica sempre visível e sempre com uma
    // linha (LONG_CLIP): mudar de estado — "Processando...", "1280×960 · 342
    // KB", "Exportando..." — nunca muda a altura da área de preview, e por
    // isso nunca invalida a escala já aplicada à imagem.
    auto* status = lv_label_create(content);
    lv_label_set_long_mode(status, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(status, LV_PCT(100));
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(PvUi::kColorMuted), 0);
    lv_label_set_text(status, PvStrings::kCameraPreviewHint);
    status_label_ = status;

    // Área do preview/foto: ocupa toda a altura que sobra entre o rótulo de
    // estado e a barra de ações. Sem layout próprio — a imagem e o aviso são
    // centralizados na mão, e ambos ocupam o mesmo lugar (um por vez).
    auto* area = lv_obj_create(content);
    lv_obj_remove_flag(area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(area, LV_PCT(100));
    lv_obj_set_flex_grow(area, 1);
    lv_obj_set_style_bg_color(area, lv_color_hex(PvUi::kColorSurface), 0);
    lv_obj_set_style_bg_opa(area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(area, 0, 0);
    lv_obj_set_style_radius(area, 12, 0);
    lv_obj_set_style_pad_all(area, 0, 0);
    lv_obj_set_scroll_dir(area, LV_DIR_ALL);
    lv_obj_set_scrollbar_mode(area, LV_SCROLLBAR_MODE_AUTO);
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
    // ApplyGeometryLocked()/ApplyZoomLocked() e o objeto fica centralizado.
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
    // Sem antialias: reduzir 1280x960 para caber na área já custa uma
    // transformação por frame (~2,4 MB lidos); filtrar em cima disso não
    // acrescenta nada num preview de enquadramento a 5 fps.
    lv_image_set_antialias(image, false);
    lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(image);
    image_ = image;

    // Barra de ações. SPACE_BETWEEN distribui os botões visíveis; o flex do
    // LVGL IGNORA filhos com LV_OBJ_FLAG_HIDDEN, então trocar de modo é só
    // esconder/mostrar botões — o layout se refaz sozinho.
    auto* bar = lv_obj_create(screen);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, kActionBarWidth, LV_PCT(100));
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    // Folga no topo para o indicador de conexão, que é desenhado por cima.
    lv_obj_set_style_pad_top(bar, kBadgeClearance, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(bar, kRowGap, 0);
    action_bar_ = bar;

    CreateActionButton(bar, PvStrings::kCameraBackButton, OnBackClicked, this, nullptr);

    // Captura no MEIO da barra (modo preview), em cor de destaque: é a ação
    // principal da tela.
    capture_button_ = CreateActionButton(bar, PvStrings::kCameraCaptureButton, OnCaptureClicked,
                                         this, &capture_label_);
    lv_obj_set_style_bg_color(capture_button_, lv_color_hex(PvUi::kColorAccent), 0);

    // Modo revisão: nascem escondidos e só aparecem com a foto na tela.
    retake_button_ =
        CreateActionButton(bar, PvStrings::kCameraRetakeButton, OnRetakeClicked, this, nullptr);
    lv_obj_set_style_bg_color(retake_button_, lv_color_hex(PvUi::kColorAccent), 0);
    zoom_button_ =
        CreateActionButton(bar, PvStrings::kCameraZoomActual, OnZoomClicked, this, &zoom_label_);
    // PROVISÓRIO DA F2: caminho de validação off-device (pv_photo_dump).
    export_button_ = CreateActionButton(bar, PvStrings::kCameraExportButton, OnExportClicked, this,
                                        &export_label_);

    // Rodapé com a versão: mesmo alvo do gesto de recuperação das outras telas
    // (long-press de 3 s abre a configuração — decisão F1-ConfigGesture). O
    // indicador de conexão carrega o mesmo gesto; os dois caminhos são
    // deliberadamente redundantes e não podem ser removidos.
    auto* version = lv_label_create(bar);
    lv_obj_set_style_text_color(version, lv_color_hex(PvUi::kColorMuted), 0);
    // Quebra de linha e centralização: a coluna é estreita e a string de
    // versão pode crescer.
    lv_label_set_long_mode(version, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(version, LV_PCT(100));
    lv_obj_set_style_text_align(version, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(version, "v%s", esp_app_get_description()->version);
    lv_obj_set_ext_click_area(version, 40);
    PvUi::AttachConfigGesture(version);

    badge_ = PvUi::CreateConnectionBadge(screen);
    PvUi::SetConnectionBadge(badge_, connected_);

    screen_ = screen;
    UpdateActionBarLocked();
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
    ReleasePhotoLocked();
    mode_ = Mode::Preview;
    capturing_ = false;
    exporting_ = false;
    zoom_actual_size_ = false;
    applied_width_ = 0;
    applied_height_ = 0;
    applied_area_w_ = 0;
    applied_area_h_ = 0;
    lv_obj_remove_flag(preview_area_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_scroll_to(preview_area_, 0, 0, LV_ANIM_OFF);
    lv_image_set_inner_align(image_, LV_IMAGE_ALIGN_CONTAIN);
    if (hint_label_ != nullptr) {
        lv_obj_remove_flag(hint_label_, LV_OBJ_FLAG_HIDDEN);
    }
    SetStatusLocked(PvStrings::kCameraPreviewHint);
    UpdateActionBarLocked();

    active_ = true;
    lv_screen_load(screen_);
    return true;
}

void PvCameraScreen::Hide() {
    active_ = false;
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr || screen_ == nullptr) {
        // Sem tela criada não há nada emprestado nem decodificado; a foto,
        // porém, pode ter sido tomada num caminho degradado.
        ReleasePhotoLocked();
        mode_ = Mode::Preview;
        capturing_ = false;
        exporting_ = false;
        return;
    }

    DisplayLockGuard lock(display);
    // ORDEM OBRIGATÓRIA: primeiro o LVGL para de referenciar os buffers, só
    // depois eles voltam a ser do escritor (preview) ou são liberados (foto).
    // Ainda sob o lock, portanto sem nenhuma janela em que a task do LVGL
    // desenhe memória já devolvida.
    DetachImageLocked();
    ReleaseFrameLocked();
    ReleasePhotoLocked();
    mode_ = Mode::Preview;
    capturing_ = false;
    exporting_ = false;
    if (hint_label_ != nullptr) {
        lv_obj_remove_flag(hint_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void PvCameraScreen::UpdateFrame() {
    if (camera_ == nullptr || !active_ || image_ == nullptr || mode_ != Mode::Preview) {
        // Aviso que sobrou na fila depois de sair da tela ou de entrar em
        // revisão: nada foi emprestado agora, então não há nada a devolver — e
        // a foto em revisão NÃO pode ser substituída por um frame atrasado.
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

// ---------------------------------------------------------------------------
// Captura e revisão (F2/T4).
// ---------------------------------------------------------------------------

void PvCameraScreen::SetCapturing(bool capturing) {
    if (screen_ == nullptr) {
        return;
    }
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr) {
        return;
    }
    DisplayLockGuard lock(display);
    capturing_ = capturing;
    if (capturing) {
        SetStatusLocked(PvStrings::kCameraCapturing);
    } else if (mode_ == Mode::Preview) {
        SetStatusLocked(PvStrings::kCameraPreviewHint);
    }
    UpdateActionBarLocked();
}

void PvCameraScreen::ShowCaptureFailed() {
    if (screen_ == nullptr) {
        return;
    }
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr) {
        return;
    }
    DisplayLockGuard lock(display);
    capturing_ = false;
    SetStatusLocked(PvStrings::kCameraCaptureFailed);
    UpdateActionBarLocked();
}

bool PvCameraScreen::EnterReview(const PvCameraCaptureResult& result) {
    // POSSE: os DOIS buffers entram aqui e não saem. Todo `return false` abaixo
    // libera ambos — nunca só um.
    if (result.jpeg.data == nullptr || result.jpeg.len == 0 || result.rgb == nullptr ||
        result.rgb_width == 0 || result.rgb_height == 0 || result.rgb_stride == 0) {
        ReleaseCaptureResult(result);
        return false;
    }
    auto display = Board::GetInstance().GetDisplay();
    if (!active_ || screen_ == nullptr || image_ == nullptr || display == nullptr) {
        ReleaseCaptureResult(result);
        return false;
    }

    // A DECODIFICAÇÃO NÃO ACONTECE MAIS AQUI: ela roda na task da câmera e
    // chega pronta em `result` (correção da revisão F2, P1). O que sobra é uma
    // troca de tela — barata o bastante para caber inteira sob o lock do
    // display sem congelar a task do LVGL.
    DisplayLockGuard lock(display);

    // Sai do preview: o LVGL para de referenciar o frame emprestado e o buffer
    // volta a ser do escritor. O PvApp já mandou parar o preview antes.
    DetachImageLocked();
    ReleaseFrameLocked();
    // Revisão anterior que não tenha passado por ExitReview (não acontece hoje,
    // mas liberar aqui é o que torna o dono único inquebrável).
    ReleasePhotoLocked();

    photo_ = result.jpeg;
    // Só DEPOIS do ReleasePhotoLocked acima, que zera este mesmo buffer: as
    // medições que a T6 vai ler na tela são o tamanho do JPEG e as dimensões.
    const unsigned kib = static_cast<unsigned>((photo_.len + 512) / 1024);
    std::snprintf(info_text_, sizeof(info_text_), "%u×%u · %u KB", (unsigned)photo_.width,
                  (unsigned)photo_.height, kib);
    decoded_ = result.rgb;
    decoded_len_ = result.rgb_len;
    decoded_width_ = result.rgb_width;
    decoded_height_ = result.rgb_height;
    decoded_stride_ = result.rgb_stride;

    photo_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    photo_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
    photo_dsc_.header.flags = 0;
    photo_dsc_.header.w = decoded_width_;
    photo_dsc_.header.h = decoded_height_;
    photo_dsc_.header.stride = static_cast<uint32_t>(decoded_stride_);
    photo_dsc_.data = decoded_;
    photo_dsc_.data_size = static_cast<uint32_t>(decoded_len_);

    mode_ = Mode::Review;
    capturing_ = false;
    exporting_ = false;
    zoom_actual_size_ = false;

    lv_image_set_src(image_, &photo_dsc_);
    lv_obj_remove_flag(image_, LV_OBJ_FLAG_HIDDEN);
    ApplyZoomLocked();
    if (hint_label_ != nullptr) {
        lv_obj_add_flag(hint_label_, LV_OBJ_FLAG_HIDDEN);
    }
    SetStatusLocked(info_text_);
    UpdateActionBarLocked();
    return true;
}

void PvCameraScreen::ExitReview() {
    auto display = Board::GetInstance().GetDisplay();
    if (screen_ == nullptr || display == nullptr) {
        ReleasePhotoLocked();
        mode_ = Mode::Preview;
        capturing_ = false;
        zoom_actual_size_ = false;
        return;
    }
    DisplayLockGuard lock(display);
    DetachImageLocked();
    ReleasePhotoLocked();

    mode_ = Mode::Preview;
    capturing_ = false;
    zoom_actual_size_ = false;
    // A revisão mexeu no tamanho, no alinhamento interno e no scroll do
    // objeto: zerar a geometria aplicada obriga o próximo frame de preview a
    // recalcular tudo do zero.
    applied_width_ = 0;
    applied_height_ = 0;
    applied_area_w_ = 0;
    applied_area_h_ = 0;
    lv_obj_remove_flag(preview_area_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_scroll_to(preview_area_, 0, 0, LV_ANIM_OFF);
    lv_image_set_inner_align(image_, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_center(image_);
    if (hint_label_ != nullptr) {
        lv_obj_remove_flag(hint_label_, LV_OBJ_FLAG_HIDDEN);
    }
    SetStatusLocked(PvStrings::kCameraPreviewHint);
    UpdateActionBarLocked();
}

void PvCameraScreen::ToggleZoom() {
    if (!IsReviewing() || screen_ == nullptr) {
        return;
    }
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr) {
        return;
    }
    DisplayLockGuard lock(display);
    zoom_actual_size_ = !zoom_actual_size_;
    ApplyZoomLocked();
    UpdateActionBarLocked();
}

void PvCameraScreen::SetExporting(bool exporting) {
    if (screen_ == nullptr) {
        return;
    }
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr) {
        return;
    }
    DisplayLockGuard lock(display);
    exporting_ = exporting;
    if (exporting) {
        SetStatusLocked(PvStrings::kCameraExporting);
    } else if (mode_ == Mode::Review) {
        SetStatusLocked(info_text_);
    }
    UpdateActionBarLocked();
}

void PvCameraScreen::ShowExportDone() {
    if (screen_ == nullptr) {
        return;
    }
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr) {
        return;
    }
    DisplayLockGuard lock(display);
    exporting_ = false;
    if (mode_ == Mode::Review) {
        SetStatusLocked(PvStrings::kCameraExportDone);
    }
    UpdateActionBarLocked();
}

void PvCameraScreen::ShowExportBusy() {
    if (screen_ == nullptr) {
        return;
    }
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr) {
        return;
    }
    DisplayLockGuard lock(display);
    SetStatusLocked(PvStrings::kCameraExportBusy);
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
    photo_dsc_.data = nullptr;
    photo_dsc_.data_size = 0;
}

void PvCameraScreen::ReleaseFrameLocked() {
    if (!frame_borrowed_) {
        return;
    }
    frame_borrowed_ = false;
    camera_->ReleaseDisplayFrame();
}

void PvCameraScreen::ReleasePhotoLocked() {
    if (decoded_ != nullptr) {
        // Alocado pelo jpeg_to_image (jpeg_calloc_align, que por baixo é
        // heap_caps_*): a liberação documentada é heap_caps_free. O
        // decodificado NUNCA é emprestado a ninguém fora da tela.
        heap_caps_free(decoded_);
        decoded_ = nullptr;
    }
    decoded_len_ = 0;
    decoded_width_ = 0;
    decoded_height_ = 0;
    decoded_stride_ = 0;
    if (photo_.data != nullptr) {
        // PROVISÓRIO DA F2 — HANDOFF DE POSSE: o dump serial lê este mesmo
        // buffer por dezenas de segundos, SEM cópia. Se ele ainda estiver
        // usando exatamente este ponteiro, a posse passa para ele e quem libera
        // é o dump, no fim da task; caso contrário (nenhum dump, ou dump de
        // outra foto, ou dump já terminado) a liberação é nossa, aqui. Ver o
        // protocolo em pv_photo_dump.h.
        if (!PvPhotoDump::TryHandOff(photo_.data)) {
            heap_caps_free(photo_.data);
        }
    }
    photo_ = PvCameraJpeg{};
    info_text_[0] = '\0';
}

void PvCameraScreen::ApplyGeometryLocked(uint16_t width, uint16_t height) {
    if (width == 0 || height == 0 || preview_area_ == nullptr || image_ == nullptr) {
        return;
    }
    // A leitura da área vem ANTES do early-return: ela faz parte da chave do
    // cache. `lv_obj_update_layout` sai de imediato quando não há layout
    // invalidado, então medir a cada frame é barato.
    lv_obj_update_layout(screen_);
    const int32_t area_w = lv_obj_get_content_width(preview_area_);
    const int32_t area_h = lv_obj_get_content_height(preview_area_);
    if (area_w <= 0 || area_h <= 0) {
        // Ainda sem layout resolvido; não marca o cache, tenta no próximo.
        return;
    }
    if (width == applied_width_ && height == applied_height_ && area_w == applied_area_w_ &&
        area_h == applied_area_h_) {
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
    applied_area_w_ = area_w;
    applied_area_h_ = area_h;
    ESP_LOGI(TAG, "Preview %ux%u em %" LV_PRId32 "x%" LV_PRId32 " (escala %" LV_PRId32 "/256)",
             (unsigned)width, (unsigned)height, area_w, area_h, scale);
}

void PvCameraScreen::ApplyZoomLocked() {
    if (image_ == nullptr || preview_area_ == nullptr || decoded_width_ == 0 ||
        decoded_height_ == 0) {
        return;
    }
    lv_obj_update_layout(screen_);
    const int32_t area_w = lv_obj_get_content_width(preview_area_);
    const int32_t area_h = lv_obj_get_content_height(preview_area_);
    if (area_w <= 0 || area_h <= 0) {
        return;
    }

    if (zoom_actual_size_) {
        // 1:1 — nenhuma transformação, o pixel do sensor vira pixel do painel.
        // A imagem passa a ser maior que a área e o SCROLL NATIVO do LVGL
        // resolve o pan: basta a área virar rolável e a imagem ficar ancorada
        // no canto (alinhamento centralizado colocaria conteúdo à esquerda do
        // zero e confundiria os limites de rolagem).
        lv_image_set_inner_align(image_, LV_IMAGE_ALIGN_DEFAULT);
        // OBRIGATÓRIO e nesta ordem (LVGL 9.5.0): sair de CONTAIN não restaura
        // a escala sozinho. `lv_image_set_inner_align()` chama
        // `lv_image_set_scale(LV_SCALE_NONE)` ANTES de trocar o alinhamento, e
        // `lv_image_set_scale()` tem a guarda `if (align >
        // _LV_IMAGE_ALIGN_AUTO_TRANSFORM) return;` — com align ainda em
        // CONTAIN (que vem depois do marcador no enum), a chamada é engolida.
        // Resultado sem esta linha: o objeto fica 1280x960 e rolável, mas a
        // foto continua desenhada a ~45%, encolhida no canto — um "100%"
        // falso. Refazer a chamada DEPOIS de DEFAULT passa pela guarda.
        lv_image_set_scale(image_, LV_SCALE_NONE);
        lv_obj_set_size(image_, decoded_width_, decoded_height_);
        const bool overflows = decoded_width_ > area_w || decoded_height_ > area_h;
        if (overflows) {
            lv_obj_add_flag(preview_area_, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_align(image_, LV_ALIGN_TOP_LEFT);
            lv_obj_set_pos(image_, 0, 0);
            lv_obj_update_layout(screen_);
            // Abre a foto pelo MEIO: é onde está o texto da página na prática.
            const int32_t to_x = decoded_width_ > area_w ? (decoded_width_ - area_w) / 2 : 0;
            const int32_t to_y = decoded_height_ > area_h ? (decoded_height_ - area_h) / 2 : 0;
            lv_obj_scroll_to(preview_area_, to_x, to_y, LV_ANIM_OFF);
        } else {
            lv_obj_remove_flag(preview_area_, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_center(image_);
        }
    } else {
        // "Ajustar": a foto inteira cabe na área, com a mesma escala inteira do
        // preview.
        lv_obj_remove_flag(preview_area_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_scroll_to(preview_area_, 0, 0, LV_ANIM_OFF);
        lv_image_set_inner_align(image_, LV_IMAGE_ALIGN_CONTAIN);
        int32_t scale_x = area_w * LV_SCALE_NONE / decoded_width_;
        int32_t scale_y = area_h * LV_SCALE_NONE / decoded_height_;
        int32_t scale = scale_x < scale_y ? scale_x : scale_y;
        if (scale > LV_SCALE_NONE) {
            scale = LV_SCALE_NONE;
        }
        if (scale < 1) {
            scale = 1;
        }
        lv_obj_set_size(image_, decoded_width_ * scale / LV_SCALE_NONE,
                        decoded_height_ * scale / LV_SCALE_NONE);
        lv_obj_center(image_);
    }
    lv_obj_invalidate(image_);
}

void PvCameraScreen::UpdateActionBarLocked() {
    const bool review = mode_ == Mode::Review;

    SetHidden(capture_button_, review);
    SetEnabled(capture_button_, !capturing_);

    SetHidden(retake_button_, !review);
    SetHidden(zoom_button_, !review);
    SetHidden(export_button_, !review);
    SetEnabled(export_button_, !exporting_);
    // "Nova foto" durante uma exportação é seguro: soltar a foto entrega a
    // POSSE do JPEG ao dump em andamento (PvPhotoDump::TryHandOff), então o
    // buffer que ele está lendo continua vivo até o dump acabar.
    SetEnabled(retake_button_, true);

    if (zoom_label_ != nullptr) {
        lv_label_set_text(zoom_label_, zoom_actual_size_ ? PvStrings::kCameraZoomFit
                                                         : PvStrings::kCameraZoomActual);
    }
}

void PvCameraScreen::SetStatusLocked(const char* text) {
    if (status_label_ == nullptr) {
        return;
    }
    lv_label_set_text(status_label_,
                      text != nullptr && text[0] != '\0' ? text : PvStrings::kCameraPreviewHint);
}

void PvCameraScreen::Dispatch(lv_event_t* e, Action action) {
    auto* self = static_cast<PvCameraScreen*>(lv_event_get_user_data(e));
    // Roda na task do LVGL: só sinaliza o PvApp, que decide e mexe nas telas na
    // própria task dele. NENHUMA ação de botão altera estado aqui.
    if (self != nullptr && self->action_handler_) {
        self->action_handler_(action);
    }
}

void PvCameraScreen::OnBackClicked(lv_event_t* e) { Dispatch(e, Action::Back); }
void PvCameraScreen::OnCaptureClicked(lv_event_t* e) { Dispatch(e, Action::Capture); }
void PvCameraScreen::OnRetakeClicked(lv_event_t* e) { Dispatch(e, Action::Retake); }
void PvCameraScreen::OnZoomClicked(lv_event_t* e) { Dispatch(e, Action::ZoomToggle); }
void PvCameraScreen::OnExportClicked(lv_event_t* e) { Dispatch(e, Action::Export); }
