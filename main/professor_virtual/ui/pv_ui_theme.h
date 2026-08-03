#ifndef PV_UI_PV_UI_THEME_H
#define PV_UI_PV_UI_THEME_H

#include <lvgl.h>

#include <cstdint>

// Recursos visuais compartilhados pelas telas LVGL do Professor Virtual.
// Todas as funções aqui pressupõem que o chamador já detém o lock do display
// (DisplayLockGuard); nenhuma delas pega o lock por conta própria.
namespace PvUi {

// Paleta do PV (mesmo azul da tela de boot da F0).
inline constexpr uint32_t kColorBackground = 0x16324F;
inline constexpr uint32_t kColorSurface = 0x1E4568;
inline constexpr uint32_t kColorText = 0xFFFFFF;
inline constexpr uint32_t kColorMuted = 0xB9CBDC;
inline constexpr uint32_t kColorAccent = 0x2F9E6B;
inline constexpr uint32_t kColorWarning = 0xE8B04B;
// Cores de alerta, usadas apenas pela tela de failsafe (§9.6).
inline constexpr uint32_t kColorAlert = 0xE05252;
inline constexpr uint32_t kColorAlertSurface = 0x4A1D24;

// Fonte de texto do PV. É uma cópia em RAM da BUILTIN_TEXT_FONT com o campo
// `fallback` apontando para a Montserrat 14: a Noto Sans do projeto não traz
// os glifos LV_SYMBOL_* que o lv_keyboard usa nas teclas de controle, e sem o
// fallback essas teclas apareceriam como retângulos vazios.
const lv_font_t* TextFont();

// Cria uma tela (pai nulo) já com fundo, cor de texto e fonte do PV.
lv_obj_t* CreateScreen();

// Indicador de conexão com o backend (§9.7): bolinha colorida + palavra curta.
// Fica no canto superior direito, fora do fluxo de layout do pai, para poder
// ser colado em telas com flex sem alterar o arranjo do conteúdo.
struct ConnectionBadge {
    lv_obj_t* container = nullptr;
    lv_obj_t* dot = nullptr;
    lv_obj_t* label = nullptr;
};

ConnectionBadge CreateConnectionBadge(lv_obj_t* parent);

// Verde + "conectado" / vermelho + "desconectado". Seguro com badge vazio.
void SetConnectionBadge(const ConnectionBadge& badge, bool connected);

}  // namespace PvUi

#endif  // PV_UI_PV_UI_THEME_H
