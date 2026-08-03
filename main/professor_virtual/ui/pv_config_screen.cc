#include "ui/pv_config_screen.h"

#include <esp_log.h>

#include <utility>

#include "board.h"
#include "display/display.h"
#include "pv_settings.h"
#include "pv_strings.h"
#include "ui/pv_ui_theme.h"

#define TAG "PvConfigScreen"

namespace {

constexpr uint32_t kMaxUrlLength = 160;
constexpr uint32_t kMaxTokenLength = 160;

// Altura reservada ao teclado LVGL; o formulário fica na faixa de cima e
// rola sozinho se o conteúdo não couber.
constexpr int32_t kKeyboardHeightPercent = 42;

const char* ReasonText(PvConfigReason reason) {
    switch (reason) {
        case PvConfigReason::MissingConfig:
            return PvStrings::kCfgReasonMissing;
        case PvConfigReason::Unauthorized:
            return PvStrings::kCfgReasonUnauthorized;
        case PvConfigReason::Unavailable:
            return PvStrings::kCfgReasonUnavailable;
        case PvConfigReason::Manual:
            break;
    }
    return PvStrings::kCfgReasonManual;
}

lv_obj_t* CreateFieldLabel(lv_obj_t* parent, const char* text) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(PvUi::kColorMuted), 0);
    return label;
}

lv_obj_t* CreateTextarea(lv_obj_t* parent, uint32_t max_length) {
    auto* textarea = lv_textarea_create(parent);
    lv_textarea_set_one_line(textarea, true);
    lv_textarea_set_max_length(textarea, max_length);
    lv_obj_set_width(textarea, LV_PCT(100));
    lv_obj_set_style_bg_color(textarea, lv_color_hex(PvUi::kColorSurface), 0);
    lv_obj_set_style_text_color(textarea, lv_color_hex(PvUi::kColorText), 0);
    lv_obj_set_style_border_width(textarea, 2, 0);
    lv_obj_set_style_border_color(textarea, lv_color_hex(PvUi::kColorMuted), 0);
    lv_obj_set_style_border_color(textarea, lv_color_hex(PvUi::kColorAccent), LV_STATE_FOCUSED);
    return textarea;
}

}  // namespace

void PvConfigScreen::SetSaveHandler(SaveHandler handler) { save_handler_ = std::move(handler); }

void PvConfigScreen::Show(PvConfigReason reason) {
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr) {
        ESP_LOGW(TAG, "Placa sem display; configuração precisa ser provisionada fora da tela");
        return;
    }

    // Lido aqui (task do PvApp) para manter NVS fora dos callbacks do LVGL.
    token_already_saved_ = PvSettings::HasApiToken();

    if (screen_ != nullptr) {
        // Já visível: só troca a mensagem, preservando o que foi digitado.
        SetMessage(ReasonText(reason), PvUi::kColorWarning);
        return;
    }

    DisplayLockGuard lock(display);

    previous_screen_ = lv_screen_active();

    auto* screen = PvUi::CreateScreen();

    auto* form = lv_obj_create(screen);
    lv_obj_set_size(form, LV_PCT(100), LV_PCT(100 - kKeyboardHeightPercent));
    lv_obj_align(form, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(form, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(form, 0, 0);
    lv_obj_set_style_pad_all(form, 16, 0);
    lv_obj_set_style_pad_row(form, 8, 0);
    lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);

    auto* title = lv_label_create(form);
    lv_label_set_text(title, PvStrings::kCfgTitle);

    auto* subtitle = lv_label_create(form);
    lv_label_set_text(subtitle, PvStrings::kCfgSubtitle);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(PvUi::kColorMuted), 0);

    auto* message = lv_label_create(form);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(message, LV_PCT(100));
    lv_label_set_text(message, ReasonText(reason));
    lv_obj_set_style_text_color(message, lv_color_hex(PvUi::kColorWarning), 0);
    message_label_ = message;

    CreateFieldLabel(form, PvStrings::kCfgUrlLabel);
    url_textarea_ = CreateTextarea(form, kMaxUrlLength);
    lv_textarea_set_placeholder_text(url_textarea_, PvStrings::kCfgUrlPlaceholder);
    // A URL pode ser pré-preenchida; o token, nunca (decisão F1-Token 2a).
    lv_textarea_set_text(url_textarea_, PvSettings::GetBackendUrl().c_str());
    lv_obj_add_event_cb(url_textarea_, OnTextareaClicked, LV_EVENT_CLICKED, this);

    CreateFieldLabel(form, PvStrings::kCfgTokenLabel);
    token_textarea_ = CreateTextarea(form, kMaxTokenLength);
    lv_textarea_set_password_mode(token_textarea_, true);
    // O default do LVGL (1500 ms) revela o último caractere digitado.
    lv_textarea_set_password_show_time(token_textarea_, 0);
    lv_textarea_set_placeholder_text(token_textarea_, token_already_saved_
                                                          ? PvStrings::kCfgTokenPlaceholderKeep
                                                          : PvStrings::kCfgTokenPlaceholderNew);
    lv_textarea_set_text(token_textarea_, "");
    lv_obj_add_event_cb(token_textarea_, OnTextareaClicked, LV_EVENT_CLICKED, this);

    auto* save_button = lv_button_create(form);
    lv_obj_set_style_bg_color(save_button, lv_color_hex(PvUi::kColorAccent), 0);
    lv_obj_add_event_cb(save_button, OnSaveClicked, LV_EVENT_CLICKED, this);
    auto* save_label = lv_label_create(save_button);
    lv_label_set_text(save_label, PvStrings::kCfgSaveButton);
    lv_obj_center(save_label);

    keyboard_ = lv_keyboard_create(screen);
    lv_obj_set_size(keyboard_, LV_PCT(100), LV_PCT(kKeyboardHeightPercent));
    lv_obj_align(keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(keyboard_, OnKeyboardReady, LV_EVENT_READY, this);

    screen_ = screen;
    FocusTextarea(url_textarea_);
    lv_screen_load(screen);
}

void PvConfigScreen::Hide() {
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr || screen_ == nullptr) {
        return;
    }

    DisplayLockGuard lock(display);

    // Zera o campo do token antes de destruir a tela, para não deixar o
    // segredo em algum buffer reaproveitado pelo alocador do LVGL.
    if (token_textarea_ != nullptr) {
        lv_textarea_set_text(token_textarea_, "");
    }

    lv_obj_t* to_delete = screen_;
    lv_obj_t* previous = previous_screen_;
    screen_ = nullptr;
    previous_screen_ = nullptr;
    message_label_ = nullptr;
    url_textarea_ = nullptr;
    token_textarea_ = nullptr;
    keyboard_ = nullptr;

    if (previous != nullptr && previous != to_delete) {
        // auto_del = true: o LVGL destrói a tela de configuração assim que ela
        // deixa de ser a ativa, junto com os textareas.
        lv_screen_load_anim(previous, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
    } else if (lv_screen_active() != to_delete) {
        lv_obj_delete(to_delete);
    }
}

void PvConfigScreen::SetMessage(const char* text, uint32_t color) {
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr || message_label_ == nullptr || text == nullptr) {
        return;
    }
    DisplayLockGuard lock(display);
    lv_label_set_text(message_label_, text);
    lv_obj_set_style_text_color(message_label_, lv_color_hex(color), 0);
}

void PvConfigScreen::FocusTextarea(lv_obj_t* textarea) {
    if (keyboard_ == nullptr || textarea == nullptr) {
        return;
    }
    lv_keyboard_set_textarea(keyboard_, textarea);
    // Sem grupo de entrada (a placa só tem touch), o estado de foco — que é
    // quem acende o cursor do textarea — precisa ser mantido na mão.
    if (url_textarea_ != nullptr) {
        lv_obj_remove_state(url_textarea_, LV_STATE_FOCUSED);
    }
    if (token_textarea_ != nullptr) {
        lv_obj_remove_state(token_textarea_, LV_STATE_FOCUSED);
    }
    lv_obj_add_state(textarea, LV_STATE_FOCUSED);
}

void PvConfigScreen::SubmitFromLvglTask() {
    if (url_textarea_ == nullptr || token_textarea_ == nullptr) {
        return;
    }

    std::string url = lv_textarea_get_text(url_textarea_);
    std::string token = lv_textarea_get_text(token_textarea_);

    if (url.empty()) {
        PvSettings::ScrubString(token);
        SetMessage(PvStrings::kCfgErrUrlEmpty, PvUi::kColorWarning);
        return;
    }
    if (token.empty() && !token_already_saved_) {
        SetMessage(PvStrings::kCfgErrTokenEmpty, PvUi::kColorWarning);
        return;
    }

    // Limpa o campo do token assim que ele sai da UI.
    lv_textarea_set_text(token_textarea_, "");
    SetMessage(PvStrings::kCfgSaving, PvUi::kColorMuted);

    if (save_handler_) {
        // O handler apenas copia e sinaliza o PvApp; a gravação em NVS
        // acontece na task do PvApp, fora do LVGL.
        save_handler_(std::move(url), std::move(token));
    }
    PvSettings::ScrubString(url);
    PvSettings::ScrubString(token);
}

void PvConfigScreen::OnTextareaClicked(lv_event_t* e) {
    auto* self = static_cast<PvConfigScreen*>(lv_event_get_user_data(e));
    auto* textarea = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (self != nullptr) {
        self->FocusTextarea(textarea);
    }
}

void PvConfigScreen::OnSaveClicked(lv_event_t* e) {
    auto* self = static_cast<PvConfigScreen*>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->SubmitFromLvglTask();
    }
}

void PvConfigScreen::OnKeyboardReady(lv_event_t* e) {
    auto* self = static_cast<PvConfigScreen*>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->SubmitFromLvglTask();
    }
}
