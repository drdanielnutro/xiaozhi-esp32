#include "ui/pv_status_screen.h"

#include <esp_app_desc.h>
#include <esp_log.h>

#include "board.h"
#include "display/display.h"
#include "pv_strings.h"
#include "ui/pv_ui_theme.h"

#define TAG "PvStatusScreen"

void PvStatusScreen::Create() {
    if (screen_ != nullptr) {
        return;
    }
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr) {
        ESP_LOGW(TAG, "Placa sem display; seguindo sem tela de status");
        return;
    }

    DisplayLockGuard lock(display);

    auto* screen = PvUi::CreateScreen();
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(screen, 24, 0);
    lv_obj_set_style_pad_row(screen, 16, 0);

    auto* title = lv_label_create(screen);
    lv_label_set_text(title, PvStrings::kAppName);

    auto* status = lv_label_create(screen);
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(status, LV_PCT(90));
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(status, PvStrings::kBootStatus);
    status_label_ = status;

    auto* detail = lv_label_create(screen);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(detail, LV_PCT(90));
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(detail, lv_color_hex(PvUi::kColorMuted), 0);
    lv_label_set_text(detail, "");
    lv_obj_add_flag(detail, LV_OBJ_FLAG_HIDDEN);
    detail_label_ = detail;

    auto* version = lv_label_create(screen);
    lv_obj_set_style_text_color(version, lv_color_hex(PvUi::kColorMuted), 0);
    lv_label_set_text_fmt(version, "v%s", esp_app_get_description()->version);
    lv_obj_align(version, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_add_flag(version, LV_OBJ_FLAG_IGNORE_LAYOUT);

    screen_ = screen;
    lv_screen_load(screen);
}

void PvStatusScreen::Load() {
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr || screen_ == nullptr) {
        return;
    }
    DisplayLockGuard lock(display);
    lv_screen_load(screen_);
}

void PvStatusScreen::SetStatus(const char* text) {
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr || status_label_ == nullptr || text == nullptr) {
        return;
    }
    DisplayLockGuard lock(display);
    lv_label_set_text(status_label_, text);
}

void PvStatusScreen::SetDetail(const char* text) {
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr || detail_label_ == nullptr) {
        return;
    }
    DisplayLockGuard lock(display);
    if (text == nullptr || text[0] == '\0') {
        lv_label_set_text(detail_label_, "");
        lv_obj_add_flag(detail_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(detail_label_, text);
    lv_obj_remove_flag(detail_label_, LV_OBJ_FLAG_HIDDEN);
}

void PvStatusScreen::SetStatusColor(uint32_t color) {
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr || status_label_ == nullptr) {
        return;
    }
    DisplayLockGuard lock(display);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(color), 0);
}
