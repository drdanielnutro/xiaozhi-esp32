#include "ui/pv_route_screens.h"

#include <esp_log.h>

#include <string>

#include <esp_app_desc.h>
#include "board.h"
#include "display/display.h"

#include "pv_app.h"
#include "pv_route_text.h"
#include "pv_strings.h"
#include "ui/pv_config_gesture.h"
#include "ui/pv_ui_theme.h"

#define TAG "PvRouteScreens"

namespace {

// Roda na task do LVGL: só sinaliza o PvApp, que decide se abre a tela (e
// liga o preview) na própria task dele.
void OnCameraClicked(lv_event_t* e) {
    LV_UNUSED(e);
    PvApp::GetInstance().RequestCameraScreen();
}

int RouteIndex(PvRoute route) {
    switch (route) {
        case PvRoute::Preparation:
            return 0;
        case PvRoute::Celebration:
            return 1;
        case PvRoute::Failsafe:
            return 2;
        case PvRoute::Tutoring:
            break;
    }
    return 3;
}

}  // namespace

PvRouteScreens::Entry& PvRouteScreens::EnsureEntry(PvRoute route) {
    Entry& entry = entries_[RouteIndex(route)];
    if (entry.screen != nullptr) {
        return entry;
    }

    const bool is_failsafe = route == PvRoute::Failsafe;

    auto* screen = PvUi::CreateScreen();
    if (is_failsafe) {
        // Failsafe em cores de alerta: a tela inteira muda de fundo para que a
        // situação seja reconhecível de longe, antes de qualquer leitura.
        lv_obj_set_style_bg_color(screen, lv_color_hex(PvUi::kColorAlertSurface), 0);
    }
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(screen, 24, 0);
    lv_obj_set_style_pad_row(screen, 16, 0);

    if (is_failsafe) {
        auto* tag = lv_label_create(screen);
        lv_label_set_text(tag, PvStrings::kRouteFailsafeBadge);
        lv_obj_set_style_bg_color(tag, lv_color_hex(PvUi::kColorAlert), 0);
        lv_obj_set_style_bg_opa(tag, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tag, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_hor(tag, 16, 0);
        lv_obj_set_style_pad_ver(tag, 6, 0);
    }

    auto* title = lv_label_create(screen);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(title, LV_PCT(90));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, PvRouteTitle(route));

    auto* subtitle = lv_label_create(screen);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(subtitle, LV_PCT(80));
    lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(PvUi::kColorMuted), 0);
    lv_label_set_text(subtitle, PvRouteSubtitle(route));

    // O bloco de detalhe só é usado pela tutoria (item/tarefa correntes); nas
    // outras rotas ele fica criado e escondido, para o código de Show() não
    // precisar de caso especial.
    auto* detail = lv_label_create(screen);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(detail, LV_PCT(80));
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(detail, lv_color_hex(PvUi::kColorSurface), 0);
    lv_obj_set_style_bg_opa(detail, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(detail, 12, 0);
    lv_obj_set_style_pad_all(detail, 16, 0);
    lv_label_set_text(detail, "");
    lv_obj_add_flag(detail, LV_OBJ_FLAG_HIDDEN);

    if (route == PvRoute::Preparation) {
        // ENTRADA PROVISÓRIA DA F2 para o preview da câmera, e só nesta rota.
        // Não é a jornada real: fotografar as páginas da lição (F7) e a foto
        // do exercício na tutoria (F3) chegam depois e substituem este botão.
        auto* camera_button = lv_button_create(screen);
        lv_obj_set_style_bg_color(camera_button, lv_color_hex(PvUi::kColorAccent), 0);
        lv_obj_add_event_cb(camera_button, OnCameraClicked, LV_EVENT_CLICKED, nullptr);
        auto* camera_label = lv_label_create(camera_button);
        lv_label_set_text(camera_label, PvStrings::kCameraButton);
        lv_obj_center(camera_label);
    }

    // Rodapé com a versão: mesmo alvo do gesto de recuperação das outras
    // telas (long-press de 3 s abre a configuração — decisão F1-ConfigGesture).
    auto* version = lv_label_create(screen);
    lv_obj_set_style_text_color(version, lv_color_hex(PvUi::kColorMuted), 0);
    lv_label_set_text_fmt(version, "v%s", esp_app_get_description()->version);
    lv_obj_align(version, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_add_flag(version, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_ext_click_area(version, 40);
    PvUi::AttachConfigGesture(version);

    entry.screen = screen;
    entry.detail = detail;
    entry.badge = PvUi::CreateConnectionBadge(screen);
    PvUi::SetConnectionBadge(entry.badge, connected_);
    return entry;
}

void PvRouteScreens::Show(PvRoute route, const PvSessionState& state, const PvLesson& lesson) {
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr) {
        ESP_LOGW(TAG, "Placa sem display; rota %s apenas registrada", PvRouteName(route));
        route_ = route;
        loaded_ = true;
        return;
    }

    // O texto é montado FORA do lock: PvBuildTutoringDetail só mexe em
    // std::string e não precisa do display.
    std::string detail_text;
    if (route == PvRoute::Tutoring) {
        detail_text = PvBuildTutoringDetail(state, lesson);
    }

    DisplayLockGuard lock(display);

    Entry& entry = EnsureEntry(route);
    if (entry.detail != nullptr) {
        if (detail_text.empty()) {
            lv_label_set_text(entry.detail, "");
            lv_obj_add_flag(entry.detail, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(entry.detail, detail_text.c_str());
            lv_obj_remove_flag(entry.detail, LV_OBJ_FLAG_HIDDEN);
        }
    }

    route_ = route;
    loaded_ = true;
    lv_screen_load(entry.screen);
}

bool PvRouteScreens::Reload() {
    if (!loaded_) {
        return false;
    }
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr) {
        // Sem display a rota é só registro; considerar "recarregada" evita que
        // o chamador tente a tela de status, que também é no-op.
        return true;
    }
    DisplayLockGuard lock(display);
    Entry& entry = entries_[RouteIndex(route_)];
    if (entry.screen == nullptr) {
        return false;
    }
    lv_screen_load(entry.screen);
    return true;
}

void PvRouteScreens::SetConnected(bool connected) {
    connected_ = connected;
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr) {
        return;
    }
    DisplayLockGuard lock(display);
    for (auto& entry : entries_) {
        if (entry.screen != nullptr) {
            PvUi::SetConnectionBadge(entry.badge, connected);
        }
    }
}
