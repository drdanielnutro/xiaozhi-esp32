#include "ui/pv_ui_theme.h"

#include "pv_strings.h"
#include "ui/pv_config_gesture.h"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);

namespace {
// Cópia em RAM da fonte embutida, criada na primeira chamada a TextFont().
// Só é tocada sob o lock do display, então não precisa de sincronização.
lv_font_t g_text_font;
bool g_text_font_ready = false;
}  // namespace

namespace PvUi {

const lv_font_t* TextFont() {
    if (!g_text_font_ready) {
        g_text_font = BUILTIN_TEXT_FONT;
#if LV_FONT_MONTSERRAT_14
        g_text_font.fallback = &lv_font_montserrat_14;
#endif
        g_text_font_ready = true;
    }
    return &g_text_font;
}

lv_obj_t* CreateScreen() {
    auto* screen = lv_obj_create(nullptr);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kColorBackground), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(kColorText), 0);
    lv_obj_set_style_text_font(screen, TextFont(), 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    return screen;
}

ConnectionBadge CreateConnectionBadge(lv_obj_t* parent) {
    ConnectionBadge badge;
    if (parent == nullptr) {
        return badge;
    }

    auto* container = lv_obj_create(parent);
    lv_obj_remove_style_all(container);
    lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(container, 8, 0);
    // Fora do layout do pai: telas do PV usam flex column centralizado e o
    // indicador não deve empurrar o conteúdo.
    lv_obj_add_flag(container, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(container, LV_ALIGN_TOP_RIGHT, -16, 16);
    // Porta intuitiva do gesto de recuperação (preferência do proprietário,
    // 2026-08-04): segurar 3 s no indicador de servidor abre a configuração.
    // O rótulo da versão mantém o mesmo gesto como caminho alternativo.
    lv_obj_set_ext_click_area(container, 40);
    AttachConfigGesture(container);

    auto* dot = lv_obj_create(container);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 16, 16);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

    auto* label = lv_label_create(container);
    lv_obj_set_style_text_color(label, lv_color_hex(kColorMuted), 0);

    badge.container = container;
    badge.dot = dot;
    badge.label = label;
    SetConnectionBadge(badge, false);
    return badge;
}

void SetConnectionBadge(const ConnectionBadge& badge, bool connected) {
    if (badge.dot == nullptr || badge.label == nullptr) {
        return;
    }
    lv_obj_set_style_bg_color(badge.dot, lv_color_hex(connected ? kColorAccent : kColorAlert), 0);
    lv_label_set_text(badge.label, connected ? PvStrings::kConnOnline : PvStrings::kConnOffline);
}

}  // namespace PvUi
