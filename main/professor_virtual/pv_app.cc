#include "pv_app.h"

#include <esp_app_desc.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

#include <string>

#include "board.h"
#include "display/display.h"
#include "pv_settings.h"
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

    // Provisão do dispositivo (contrato v1.1): backend_url + api_token no
    // NVS "pv". O token nunca é logado — só a presença.
    ESP_LOGI(TAG, "Configuração: backend %s, token %s",
             PvSettings::GetBackendUrl().empty() ? "(vazio)" : PvSettings::GetBackendUrl().c_str(),
             PvSettings::HasApiToken() ? "provisionado" : "ausente");

    // O PV é o dono do callback de rede (decisão Q1a): registrado antes de
    // qualquer StartNetwork() futuro, para que o fluxo assistente nunca
    // receba os eventos. Na F0 a rede não é iniciada; a F1 assume as
    // transições de estado quando StartNetwork() entrar.
    RegisterNetworkCallback();
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
    status_label_ = status;

    lv_screen_load(screen);
}

void PvApp::SetStatusText(const char* text) {
    auto display = Board::GetInstance().GetDisplay();
    if (display == nullptr || status_label_ == nullptr) {
        return;
    }
    DisplayLockGuard lock(display);
    lv_label_set_text(static_cast<lv_obj_t*>(status_label_), text);
}

void PvApp::RegisterNetworkCallback() {
    // Callbacks chegam fora da task principal; aqui só se atualiza o label
    // de status sob DisplayLockGuard, sem tocar no fluxo do assistente.
    Board::GetInstance().SetNetworkEventCallback(
        [this](NetworkEvent event, const std::string& data) {
            switch (event) {
                case NetworkEvent::Scanning:
                    SetStatusText(PvStrings::kNetScanning);
                    break;
                case NetworkEvent::Connecting:
                    SetStatusText((PvStrings::kNetConnecting + data + "...").c_str());
                    break;
                case NetworkEvent::Connected:
                    SetStatusText((PvStrings::kNetConnected + data).c_str());
                    break;
                case NetworkEvent::Disconnected:
                    SetStatusText(PvStrings::kNetDisconnected);
                    break;
                case NetworkEvent::WifiConfigModeEnter:
                    SetStatusText(PvStrings::kNetConfigMode);
                    break;
                default:
                    ESP_LOGI(TAG, "Evento de rede ignorado na F0: %d", static_cast<int>(event));
                    break;
            }
        });
}
