#include "ui/pv_ui_theme.h"

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

}  // namespace PvUi
