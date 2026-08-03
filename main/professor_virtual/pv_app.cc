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
#include "pv_backend_client.h"
#include "pv_settings.h"
#include "pv_strings.h"
#include "ui/pv_ui_theme.h"

#define TAG "PvApp"

namespace {
// Fila curta: os eventos são raros e o consumidor (Run) está sempre pronto.
constexpr UBaseType_t kEventQueueLength = 8;
constexpr TickType_t kEventWaitTicks = pdMS_TO_TICKS(1000);

// Monitoramento de conectividade do §9.7. O mesmo tick também é o ritmo de
// nova tentativa da hidratação que falhou: nada de retry em rajada.
constexpr uint64_t kHealthPeriodUs = 10ULL * 1000 * 1000;
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

    // Task de rede do PV: executa as chamadas bloqueantes do PvBackendClient e
    // devolve só um aviso pela fila; o resultado é retirado aqui na task
    // principal, com Take*(). O handler roda NA TASK DO WORKER.
    worker_.Start([this](PvWorker::Job job) {
        PostEvent(
            job == PvWorker::Job::Hydrate ? PvEventType::HydrationDone : PvEventType::HealthDone,
            std::string(), PvConfigReason::Manual);
    });

    // Timer de health: o callback roda na task do esp_timer e NUNCA faz HTTP —
    // só posta o tick, exatamente como o clock_timer do Application.
    esp_timer_create_args_t health_timer_args = {.callback =
                                                     [](void* arg) {
                                                         static_cast<PvApp*>(arg)->PostEvent(
                                                             PvEventType::HealthTick, std::string(),
                                                             PvConfigReason::Manual);
                                                     },
                                                 .arg = this,
                                                 .dispatch_method = ESP_TIMER_TASK,
                                                 .name = "pv_health",
                                                 .skip_unhandled_events = true};
    if (esp_timer_create(&health_timer_args, &health_timer_) != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao criar o timer de health");
        health_timer_ = nullptr;
    }

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
        // Os disparos periódicos (health de 10 s, nova tentativa de hidratação
        // e roteamento §9.1) chegam como PvEventType::HealthTick, postado pelo
        // timer; este laço só consome a fila.
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
            // Sem rede o espelho envelhece: a volta obriga nova hidratação
            // (§9.7) e o health para de bater até a rede voltar.
            hydrated_ = false;
            StopHealthTimer();
            SetBackendHealthy(false);
            if (phase_ != PvPhase::WifiConfigMode && phase_ != PvPhase::AwaitingBackendConfig) {
                phase_ = PvPhase::Offline;
                status_screen_.SetStatusColor(PvUi::kColorWarning);
                status_screen_.SetStatus(PvStrings::kNetDisconnected);
                status_screen_.SetDetail(PvStrings::kNetDisconnectedHint);
                // Com uma rota já carregada a tela da criança permanece no ar
                // (só o indicador vira "desconectado"); a tela de status é
                // carregada apenas quando ainda não houve roteamento.
                if (!route_screens_.IsLoaded()) {
                    status_screen_.Load();
                }
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

        case PvEventType::HealthTick:
            HandleHealthTick();
            break;

        case PvEventType::HydrationDone:
            HandleHydrationDone();
            break;

        case PvEventType::HealthDone:
            HandleHealthDone();
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

    // Rede de pé não é "tudo pronto": só a hidratação bem-sucedida é. A tela
    // seguinte é pintada pelo StartHydration (ou pela rota, quando chegar).
    phase_ = PvPhase::Online;

    // (a) primeira conexão e (c) reconexão depois de queda: o espelho pode ter
    // envelhecido, então a hidratação recomeça do zero.
    hydrated_ = false;
    StartHydration();
}

void PvApp::HandleWifiConfigMode() {
    phase_ = PvPhase::WifiConfigMode;
    network_connected_ = false;
    hydrated_ = false;
    StopHealthTimer();
    SetBackendHealthy(false);

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
    // Enquanto o adulto configura, nada de bater no backend com a credencial
    // que acabou de ser recusada.
    StopHealthTimer();
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
        phase_ = PvPhase::Online;
        // (b) o adulto acabou de corrigir a configuração (inclusive em resposta
        // a 401/503): hidrata de novo com a credencial nova.
        hydrated_ = false;
        StartHydration();
    } else if (!network_connected_) {
        phase_ = PvPhase::Offline;
        status_screen_.SetStatusColor(PvUi::kColorWarning);
        status_screen_.SetStatus(PvStrings::kNetDisconnected);
        status_screen_.SetDetail(PvStrings::kNetDisconnectedHint);
    } else {
        ShowConfigScreen(PvConfigReason::MissingConfig);
    }
}

// ---------------------------------------------------------------------------
// Hidratação (§9.1), health periódico e indicador de conexão (§9.7).
//
// Divisão de trabalho: esta task decide, muda estado global e toca LVGL; o
// PvWorker só faz HTTP. Nenhuma chamada bloqueante acontece daqui para baixo.
// ---------------------------------------------------------------------------

void PvApp::StartHydration() {
    if (!network_connected_ || !PvSettings::IsConfigured() || config_screen_.IsVisible()) {
        return;
    }

    // Decisão F1-StateMachine (3b): a hidratação roda em 'activating'.
    // Starting -> Activating, WifiConfiguring -> Activating e Idle ->
    // Activating são todas legais (device_state_machine.cc); estar já em
    // 'activating' é um no-op aceito pela própria máquina.
    if (!Application::GetInstance().SetDeviceState(kDeviceStateActivating)) {
        ESP_LOGE(TAG, "Transição para 'activating' recusada pela máquina de estados");
    }

    // Antes do primeiro roteamento a tela de status conta o que está
    // acontecendo. Depois dele, não: trocar a tela da criança por um "falando
    // com o professor" a cada re-hidratação seria pior que o indicador.
    if (!route_screens_.IsLoaded()) {
        std::string detail;
        if (!network_name_.empty()) {
            detail = PvStrings::kNetConnected;
            detail += network_name_;
        }
        status_screen_.SetStatusColor(PvUi::kColorText);
        status_screen_.SetStatus(PvStrings::kHydrating);
        status_screen_.SetDetail(detail.empty() ? nullptr : detail.c_str());
        status_screen_.Load();
    }

    StartHealthTimer();

    if (!worker_.RequestHydrate()) {
        ESP_LOGI(TAG, "Hidratação já em andamento; pedido coalescido");
    }
}

void PvApp::HandleHydrationDone() {
    PvWorker::HydrationResult result;
    PvSessionState state;
    PvLesson lesson;
    if (!worker_.TakeHydration(result, state, lesson)) {
        // Aviso duplicado de um ciclo cujo resultado já foi consumido.
        return;
    }

    if (result.backend.ok()) {
        // Só aqui o espelho é substituído: rota nenhuma é decidida com dado
        // pela metade.
        session_state_ = std::move(state);
        lesson_ = std::move(lesson);
        hydrated_ = true;
        phase_ = PvPhase::Online;
        SetBackendHealthy(true);

        PvRoute route = DecideRoute(session_state_, lesson_);
        ESP_LOGI(TAG, "Hidratação concluída; rota de boot: %s", PvRouteName(route));
        // SetBackendHealthy já marcou o indicador; uma tela de rota criada
        // agora nasce com o estado correto.
        route_screens_.Show(route, session_state_, lesson_);

        if (!Application::GetInstance().SetDeviceState(kDeviceStateIdle)) {
            ESP_LOGE(TAG, "Transição para 'idle' recusada pela máquina de estados");
        }
        StartHealthTimer();
        return;
    }

    hydrated_ = false;
    SetBackendHealthy(false);

    // 401/503 interrompem o fluxo e vão para a tela de configuração — nunca
    // para uma tela pedagógica com dado inventado.
    if (result.backend.needs_credentials()) {
        ShowConfigScreen(result.backend.status == PvBackendStatus::Unauthorized
                             ? PvConfigReason::Unauthorized
                             : PvConfigReason::Unavailable);
        return;
    }

    // Falha recuperável. O estado global PERMANECE em 'activating': a máquina
    // de estados não aceita Activating -> Starting, e cair para
    // WifiConfiguring aqui abriria o portal de rede por um problema que é do
    // backend. A nova tentativa vem no próximo tick de health (10 s).
    switch (result.backend.status) {
        case PvBackendStatus::HttpError:
            ShowBackendError(PvStrings::kHydrateErrHttp);
            break;
        case PvBackendStatus::ParseError:
            ShowBackendError(PvStrings::kHydrateErrParse);
            break;
        default:
            ShowBackendError(PvStrings::kHydrateErrNetwork);
            break;
    }
}

void PvApp::HandleHealthTick() {
    if (!network_connected_ || !PvSettings::IsConfigured() || config_screen_.IsVisible()) {
        return;
    }
    if (!hydrated_) {
        // Ainda não há espelho válido: o tick é a nova tentativa do ciclo
        // completo (health -> state -> lesson), sem retry em rajada.
        StartHydration();
        return;
    }
    if (!worker_.RequestHealth()) {
        ESP_LOGD(TAG, "Health já em andamento; tick ignorado");
    }
}

void PvApp::HandleHealthDone() {
    PvBackendResult result;
    if (!worker_.TakeHealth(result)) {
        return;
    }

    if (result.ok()) {
        const bool was_healthy = backend_healthy_;
        SetBackendHealthy(true);
        if (!was_healthy || !hydrated_) {
            // §9.7: "tentar reconectar e re-hidratar ao voltar".
            ESP_LOGI(TAG, "Backend respondeu de novo; re-hidratando");
            StartHydration();
        }
        return;
    }

    SetBackendHealthy(false);
    if (result.needs_credentials()) {
        ShowConfigScreen(result.status == PvBackendStatus::Unauthorized
                             ? PvConfigReason::Unauthorized
                             : PvConfigReason::Unavailable);
        return;
    }
    ESP_LOGW(TAG, "Health falhou: %s (HTTP %d)", PvBackendClient::StatusName(result.status),
             result.http_status);
    ShowBackendError(PvStrings::kHealthLost);
}

void PvApp::ShowBackendError(const char* message) {
    // Com uma rota no ar, a mensagem técnica não invade a tela da criança: o
    // indicador de conexão já mostra "desconectado" e a tutoria não prossegue
    // (as telas só mudam de rota com hidratação nova e completa).
    if (route_screens_.IsLoaded()) {
        return;
    }
    status_screen_.SetStatusColor(PvUi::kColorWarning);
    status_screen_.SetStatus(message);
    status_screen_.SetDetail(PvStrings::kHydrateRetryHint);
    status_screen_.Load();
}

void PvApp::SetBackendHealthy(bool healthy) {
    backend_healthy_ = healthy;
    status_screen_.SetConnected(healthy);
    route_screens_.SetConnected(healthy);
}

void PvApp::StartHealthTimer() {
    if (health_timer_ == nullptr || esp_timer_is_active(health_timer_)) {
        return;
    }
    if (esp_timer_start_periodic(health_timer_, kHealthPeriodUs) != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar o timer de health");
    }
}

void PvApp::StopHealthTimer() {
    if (health_timer_ == nullptr || !esp_timer_is_active(health_timer_)) {
        return;
    }
    esp_timer_stop(health_timer_);
}
