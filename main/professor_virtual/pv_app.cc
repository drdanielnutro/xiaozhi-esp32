#include "pv_app.h"

#include <esp_app_desc.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

#include "board.h"
#include "display/display.h"
#include "pv_strings.h"

#define TAG "PvApp"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);

void PvApp::Initialize() {
    // Constrói a placa (display, touch, câmera, backlight). O fluxo do
    // assistente nunca é ativado: SetupUI() e StartNetwork() não são chamados
    // aqui, então a tela fica livre para as telas LVGL próprias do PV.
    auto& board = Board::GetInstance();
    ESP_LOGI(TAG, "Professor Virtual %s na placa %s", esp_app_get_description()->version,
             board.GetBoardType().c_str());

    SetupBootScreen();
}

void PvApp::Run() {
    while (true) {
        // Loop de eventos do PV: nas próximas fases passa a tratar rede,
        // hidratação e máquina de fases. Por ora só mantém a task viva.
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void PvApp::SetupBootScreen() {
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr) {
        ESP_LOGW(TAG, "Placa sem display; seguindo sem tela de boot");
        return;
    }

    DisplayLockGuard lock(display);

    auto* screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x16324f), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(screen, &BUILTIN_TEXT_FONT, 0);

    auto* title = lv_label_create(screen);
    lv_label_set_text(title, PvStrings::kAppName);
    lv_obj_center(title);

    auto* version = lv_label_create(screen);
    lv_label_set_text_fmt(version, "v%s", esp_app_get_description()->version);
    lv_obj_align(version, LV_ALIGN_BOTTOM_MID, 0, -16);

    auto* status = lv_label_create(screen);
    lv_label_set_text(status, PvStrings::kBootStatus);
    lv_obj_align_to(status, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);

    lv_screen_load(screen);
}
