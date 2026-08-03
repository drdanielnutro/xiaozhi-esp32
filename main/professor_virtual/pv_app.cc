#include "pv_app.h"

#include <esp_app_desc.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <wifi_manager.h>

#include <cstring>
#include <string>
#include <utility>

#include "application.h"
#include "board.h"
#include "pv_settings.h"
#include "pv_strings.h"
#include "ui/pv_ui_theme.h"

#define TAG "PvApp"

namespace {
// Fila curta: os eventos são raros e o consumidor (Run) está sempre pronto.
constexpr UBaseType_t kEventQueueLength = 8;
constexpr TickType_t kEventWaitTicks = pdMS_TO_TICKS(1000);
}  // namespace

void PvApp::Initialize() {
    auto& board = Board::GetInstance();
    ESP_LOGI(TAG, "Professor Virtual %s na placa %s", esp_app_get_description()->version,
             board.GetBoardType().c_str());

    event_queue_ = xQueueCreate(kEventQueueLength, sizeof(PvEvent));
    if (event_queue_ == nullptr) {
        ESP_LOGE(TAG, "Falha ao criar a fila de eventos do PV");
    }

    // Tela de boot/status. O fluxo do assistente não é ativado: SetupUI() nunca
    // é chamado, então a tela fica livre para as telas LVGL próprias do PV.
    status_screen_.Create();
    status_screen_.SetStatus(PvStrings::kBootStatus);

    // Provisão do dispositivo (contrato v1.1): backend_url + api_token no
    // NVS "pv". O token nunca é logado — só a presença.
    ESP_LOGI(TAG, "Configuração: backend %s, token %s",
             PvSettings::GetBackendUrl().empty() ? "(vazio)" : PvSettings::GetBackendUrl().c_str(),
             PvSettings::HasApiToken() ? "provisionado" : "ausente");

    // O handler roda na task do LVGL; ele só copia os textos e acorda o Run().
    config_screen_.SetSaveHandler([this](std::string url, std::string token) {
        OnSaveRequestedFromLvglTask(std::move(url), std::move(token));
    });

    // O PV é o dono do callback de rede (decisão Q1a): registrado antes de
    // StartNetwork() para que o fluxo do assistente nunca receba os eventos.
    RegisterNetworkCallback();

    // Ordem obrigatória (decisão F1-StateMachine 3b): o estado global vai para
    // Starting ANTES de StartNetwork(). Unknown -> Starting é a única saída de
    // Unknown na DeviceStateMachine; a partir daí o WifiBoard pode levar
    // sozinho para WifiConfiguring, que também é transição legal.
    if (!Application::GetInstance().SetDeviceState(kDeviceStateStarting)) {
        ESP_LOGE(TAG, "Transição para 'starting' recusada pela máquina de estados");
    }

    phase_ = PvPhase::WifiConnecting;
    status_screen_.SetStatus(PvStrings::kNetStartingWifi);

    // Pode bloquear por ~1,5 s quando não há SSID salvo (o WifiBoard espera a
    // tela de versão antes de subir o AP de configuração).
    board.StartNetwork();
}

void PvApp::Run() {
    while (true) {
        PvEvent event{};
        if (event_queue_ != nullptr &&
            xQueueReceive(event_queue_, &event, kEventWaitTicks) == pdTRUE) {
            HandleEvent(event);
            continue;
        }
        if (event_queue_ == nullptr) {
            vTaskDelay(kEventWaitTicks);
        }
        // T5: aqui entram os disparos periódicos (health-check, hidratação e
        // roteamento §9.1) quando a fase estiver em Online.
    }
}

void PvApp::RequestConfigScreen(PvConfigReason reason) {
    PostEvent(PvEventType::ConfigScreenRequested, std::string(), reason);
}

void PvApp::RegisterNetworkCallback() {
    // Os callbacks chegam na task de eventos do Wi-Fi. Nada de LVGL nem de
    // máquina de estados aqui: só postam na fila do PvApp (decisão 3b).
    Board::GetInstance().SetNetworkEventCallback([this](NetworkEvent event,
                                                        const std::string& data) {
        switch (event) {
            case NetworkEvent::Scanning:
                PostEvent(PvEventType::NetScanning, data, PvConfigReason::Manual);
                break;
            case NetworkEvent::Connecting:
                PostEvent(PvEventType::NetConnecting, data, PvConfigReason::Manual);
                break;
            case NetworkEvent::Connected:
                PostEvent(PvEventType::NetConnected, data, PvConfigReason::Manual);
                break;
            case NetworkEvent::Disconnected:
                PostEvent(PvEventType::NetDisconnected, data, PvConfigReason::Manual);
                break;
            case NetworkEvent::WifiConfigModeEnter:
                PostEvent(PvEventType::NetConfigModeEnter, data, PvConfigReason::Manual);
                break;
            case NetworkEvent::WifiConfigModeExit:
                PostEvent(PvEventType::NetConfigModeExit, data, PvConfigReason::Manual);
                break;
            default:
                ESP_LOGI(TAG, "Evento de rede sem tratamento na F1: %d", static_cast<int>(event));
                break;
        }
    });
}

void PvApp::PostEvent(PvEventType type, const std::string& data, PvConfigReason reason) {
    if (event_queue_ == nullptr) {
        return;
    }
    PvEvent event{};
    event.type = type;
    event.reason = reason;
    if (!data.empty()) {
        std::strncpy(event.data, data.c_str(), sizeof(event.data) - 1);
    }
    if (xQueueSend(event_queue_, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Fila de eventos cheia; evento %d descartado", static_cast<int>(type));
    }
}

void PvApp::HandleEvent(const PvEvent& event) {
    switch (event.type) {
        case PvEventType::NetScanning:
            network_connected_ = false;
            phase_ = PvPhase::WifiConnecting;
            status_screen_.SetStatusColor(PvUi::kColorText);
            status_screen_.SetStatus(PvStrings::kNetScanning);
            status_screen_.SetDetail(nullptr);
            break;

        case PvEventType::NetConnecting: {
            phase_ = PvPhase::WifiConnecting;
            std::string message = PvStrings::kNetConnecting;
            message += event.data;
            message += "...";
            status_screen_.SetStatusColor(PvUi::kColorText);
            status_screen_.SetStatus(message.c_str());
            status_screen_.SetDetail(nullptr);
            break;
        }

        case PvEventType::NetConnected:
            HandleNetworkConnected(event.data);
            break;

        case PvEventType::NetDisconnected:
            network_connected_ = false;
            if (phase_ != PvPhase::WifiConfigMode && phase_ != PvPhase::AwaitingBackendConfig) {
                phase_ = PvPhase::Offline;
                status_screen_.SetStatusColor(PvUi::kColorWarning);
                status_screen_.SetStatus(PvStrings::kNetDisconnected);
                status_screen_.SetDetail(PvStrings::kNetDisconnectedHint);
            }
            break;

        case PvEventType::NetConfigModeEnter:
            HandleWifiConfigMode();
            break;

        case PvEventType::NetConfigModeExit:
            phase_ = PvPhase::WifiConnecting;
            status_screen_.SetStatusColor(PvUi::kColorText);
            status_screen_.SetStatus(PvStrings::kNetConfigModeExit);
            status_screen_.SetDetail(nullptr);
            if (!config_screen_.IsVisible()) {
                status_screen_.Load();
            }
            break;

        case PvEventType::ConfigScreenRequested:
            ShowConfigScreen(event.reason);
            break;

        case PvEventType::ConfigSaveRequested:
            HandlePendingSave();
            break;
    }
}

void PvApp::HandleNetworkConnected(const char* ssid) {
    network_connected_ = true;
    network_name_ = ssid != nullptr ? ssid : "";

    if (config_screen_.IsVisible()) {
        // O adulto está no meio da configuração; não roubar a tela.
        return;
    }

    if (!PvSettings::IsConfigured()) {
        ShowConfigScreen(PvConfigReason::MissingConfig);
        return;
    }

    ShowReadyStatus();
    // T5: com rede e provisão prontas, é daqui que parte a hidratação
    // (GET /api/session), o roteamento §9.1 e as transições
    // Starting/WifiConfiguring -> Activating -> Idle.
}

void PvApp::ShowReadyStatus() {
    phase_ = PvPhase::Online;
    std::string detail = PvStrings::kNetConnected;
    detail += network_name_;
    status_screen_.SetStatusColor(PvUi::kColorAccent);
    status_screen_.SetStatus(PvStrings::kNetReady);
    status_screen_.SetDetail(detail.c_str());
    status_screen_.Load();
}

void PvApp::HandleWifiConfigMode() {
    phase_ = PvPhase::WifiConfigMode;
    network_connected_ = false;

    // Sem rede, configurar o backend não leva a nada: fecha a tela de config
    // e mostra as instruções do portal do WifiBoard.
    if (config_screen_.IsVisible()) {
        config_screen_.Hide();
    }

    auto& wifi_manager = WifiManager::GetInstance();
    std::string ap_ssid = wifi_manager.GetApSsid();
    std::string ap_url = wifi_manager.GetApWebUrl();

    status_screen_.SetStatusColor(PvUi::kColorWarning);
    status_screen_.SetStatus(PvStrings::kNetConfigMode);
    if (ap_ssid.empty() || ap_url.empty()) {
        ESP_LOGW(TAG, "AP de configuração sem SSID/URL disponíveis");
        status_screen_.SetDetail(nullptr);
    } else {
        std::string detail = PvStrings::kWifiApHintPrefix;
        detail += ap_ssid;
        detail += PvStrings::kWifiApHintMiddle;
        detail += ap_url;
        detail += PvStrings::kWifiApHintSuffix;
        status_screen_.SetDetail(detail.c_str());
    }
    status_screen_.Load();
}

void PvApp::ShowConfigScreen(PvConfigReason reason) {
    phase_ = PvPhase::AwaitingBackendConfig;
    status_screen_.SetStatusColor(PvUi::kColorWarning);
    status_screen_.SetStatus(PvStrings::kNetWaitingConfig);
    config_screen_.Show(reason);
}

void PvApp::OnSaveRequestedFromLvglTask(std::string url, std::string token) {
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        PvSettings::ScrubString(pending_url_);
        PvSettings::ScrubString(pending_token_);
        pending_url_ = std::move(url);
        pending_token_ = std::move(token);
        pending_save_valid_ = true;
    }
    PostEvent(PvEventType::ConfigSaveRequested, std::string(), PvConfigReason::Manual);
}

void PvApp::HandlePendingSave() {
    std::string url;
    std::string token;
    {
        // Escopo curto de propósito: o mutex nunca é mantido enquanto se
        // toca o LVGL, senão a task do LVGL (que segura o lock do display e
        // quer este mutex no Salvar) e esta task fechariam um ciclo.
        std::lock_guard<std::mutex> lock(pending_mutex_);
        if (!pending_save_valid_) {
            return;
        }
        url = std::move(pending_url_);
        token = std::move(pending_token_);
        PvSettings::ScrubString(pending_url_);
        PvSettings::ScrubString(pending_token_);
        pending_save_valid_ = false;
    }

    PvSettings::SetBackendUrl(url);
    // Token vazio com token já provisionado significa "manter o atual"
    // (decisão F1-Token 2a); o campo nunca é pré-preenchido a partir do NVS.
    if (!token.empty()) {
        PvSettings::SetApiToken(token);
    }
    PvSettings::ScrubString(token);

    ESP_LOGI(TAG, "Configuração gravada: backend %s, token %s", url.c_str(),
             PvSettings::HasApiToken() ? "provisionado" : "ausente");

    config_screen_.Hide();

    if (network_connected_ && PvSettings::IsConfigured()) {
        ShowReadyStatus();
        // T5: a hidratação também deve ser disparada aqui, depois que o
        // adulto corrige a configuração em resposta a 401/503.
    } else if (!network_connected_) {
        phase_ = PvPhase::Offline;
        status_screen_.SetStatusColor(PvUi::kColorWarning);
        status_screen_.SetStatus(PvStrings::kNetDisconnected);
        status_screen_.SetDetail(PvStrings::kNetDisconnectedHint);
    } else {
        ShowConfigScreen(PvConfigReason::MissingConfig);
    }
}
