#include "pv_app.h"

#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <wifi_manager.h>

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "application.h"
#include "board.h"
#include "pv_backend_client.h"
#include "pv_photo_dump.h"  // PROVISÓRIO DA F2
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

// Som local disparado pelo veredicto ao chegar a resposta (§9.3). `teach` e
// `unidentifiable` não têm som nenhum: eles não afirmam acerto nem erro, e um
// som ali daria à criança um veredicto que o professor não deu.
PvAudio::Feedback FeedbackForVerdict(PvVerdict verdict) {
    switch (verdict) {
        case PvVerdict::Correct:
            return PvAudio::Feedback::Correct;
        case PvVerdict::Wrong:
            return PvAudio::Feedback::Wrong;
        case PvVerdict::Teach:
        case PvVerdict::Unidentifiable:
            break;
    }
    return PvAudio::Feedback::None;
}

// Sessão pronta para receber um turno: o espelho precisa ser de uma sessão de
// verdade e ainda ativa. "completed"/"closed"/"expired" não recebem turno —
// mandar um seria pedir ao backend que recusasse (409/4xx) por algo que o
// cliente já sabia.
bool CanSendTurn(const PvSessionState& state) {
    return state.kind == PvStateKind::Session && state.session_status == "active" &&
           !state.session_id.empty();
}
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

    // Motor de câmera (F2). O handler roda NA TASK DA CÂMERA: só sinaliza.
    // Placa sem câmera (ou sem as extensões opcionais) degrada para no-op —
    // daí a tela de câmera simplesmente nunca abre.
    if (!camera_.Start([this](PvCamera::Event event) { OnCameraEventFromCameraTask(event); })) {
        ESP_LOGW(TAG, "Câmera indisponível; a tela de câmera fica desligada nesta placa");
    }
    // Os handlers dos botões da tela de câmera rodam na task do LVGL e só
    // postam na fila; quem decide (e mexe em tela) é sempre este Run().
    camera_screen_.Attach(
        &camera_, [this](PvCameraScreen::Action action) { OnCameraActionFromLvglTask(action); });

    // PROVISÓRIO DA F2: conclusão do dump serial de diagnóstico. O handler roda
    // NA TASK DO DUMP e só sinaliza — mesmo contrato do PvWorker/PvCamera.
    PvPhotoDump::SetDoneHandler([this]() {
        PostEvent(PvEventType::CameraExportDone, std::string(), PvConfigReason::Manual);
    });

    // Reprodução da resposta do turno (F3): som de feedback + voz do tutor, em
    // task própria. Placa sem codec degrada para no-op — o fluxo do turno
    // continua, só que sem voz (o terminal (a) fecha na hora). O handler roda
    // NA TASK DO PvAudio: só sinaliza.
    if (!audio_.Start([this](PvAudio::Event event) { OnAudioEventFromAudioTask(event); })) {
        ESP_LOGW(TAG, "Áudio indisponível; a resposta do professor sai sem voz nesta placa");
    }

    // Task de rede do PV: executa as chamadas bloqueantes do PvBackendClient e
    // devolve só um aviso pela fila; o resultado é retirado aqui na task
    // principal, com Take*(). O handler roda NA TASK DO WORKER.
    worker_.Start([this](PvWorker::Job job) {
        PvEventType type = PvEventType::HealthDone;
        switch (job) {
            case PvWorker::Job::Hydrate:
                type = PvEventType::HydrationDone;
                break;
            case PvWorker::Job::HealthCheck:
                type = PvEventType::HealthDone;
                break;
            case PvWorker::Job::Turn:
                type = PvEventType::TurnDone;
                break;
        }
        PostEvent(type, std::string(), PvConfigReason::Manual);
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

    // O wrapper HttpClient (78__esp-ml307) loga TODOS os cabeçalhos da
    // requisição em ESP_LOGD — incluindo Authorization. Em build release o
    // DEBUG é eliminado em compilação, mas num build de diagnóstico o token
    // vazaria (revisão F1, P1). Trava o tag em INFO como defesa em
    // profundidade: o token JAMAIS aparece em log.
    esp_log_level_set("HttpClient", ESP_LOG_INFO);

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
        } else if (event_queue_ == nullptr) {
            vTaskDelay(kEventWaitTicks);
        }
        // Os disparos periódicos (health de 10 s, nova tentativa de hidratação
        // e roteamento §9.1) chegam como PvEventType::HealthTick, postado pelo
        // timer; este laço só consome a fila.
        //
        // A RECONCILIAÇÃO da câmera roda a CADA volta do laço — depois de cada
        // evento E no timeout de 1 s —, nunca só no timeout: com o preview a
        // 5 fps a fila não esvazia e o timeout pode simplesmente não acontecer
        // (revisão F2 rodada 2, P1). Os eventos continuam sendo o caminho
        // rápido; PostEvent é best-effort. Ver ReconcileCameraState().
        ReconcileCameraState();
        ReconcileTurnState();
    }
}

void PvApp::RequestConfigScreen(PvConfigReason reason) {
    PostEvent(PvEventType::ConfigScreenRequested, std::string(), reason);
}

void PvApp::RequestCameraScreen() {
    PostEvent(PvEventType::CameraScreenRequested, std::string(), PvConfigReason::Manual);
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

bool PvApp::PostEvent(PvEventType type, const std::string& data, PvConfigReason reason) {
    if (event_queue_ == nullptr) {
        return false;
    }
    PvEvent event{};
    event.type = type;
    event.reason = reason;
    if (!data.empty()) {
        std::strncpy(event.data, data.c_str(), sizeof(event.data) - 1);
    }
    if (xQueueSend(event_queue_, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Fila de eventos cheia; evento %d descartado", static_cast<int>(type));
        return false;
    }
    return true;
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
            // Fronteira de conectividade: resultados HTTP que ficaram em voo
            // pertencem à geração anterior e serão descartados ao chegar.
            net_generation_++;
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
                    LoadStatusScreen();
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
                LoadStatusScreen();
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

        case PvEventType::CameraScreenRequested:
            ShowCameraScreen();
            break;

        case PvEventType::CameraScreenClosed:
            // Comandos da criança ficam bloqueados fora de `idle` (§9.2). O
            // botão já está desabilitado; esta guarda cobre o toque que ficou
            // na fila antes do bloqueio. Ela NÃO vale para as saídas do
            // sistema (gesto de configuração, rota nova por re-hidratação),
            // que chamam ReturnFromCameraScreen/LeaveCameraScreen direto.
            if (turn_phase_ != PvTurnPhase::Idle) {
                ESP_LOGI(TAG, "Voltar ignorado: turno em andamento (fase %d)",
                         static_cast<int>(turn_phase_));
                break;
            }
            ReturnFromCameraScreen();
            break;

        case PvEventType::CameraPreviewFrame:
            // Libera a coalescência ANTES de desenhar: um frame produzido
            // durante o desenho já pode reagendar o próximo aviso, e o
            // último-frame-vence do PvCamera garante que nada se acumula.
            preview_frame_pending_.store(false);
            camera_screen_.UpdateFrame();
            break;

        case PvEventType::CameraCaptureRequested:
            HandleCaptureRequested();
            break;

        case PvEventType::CameraJpegReady:
            HandleJpegReady();
            break;

        case PvEventType::CameraJpegFailed:
            HandleJpegFailed();
            break;

        case PvEventType::CameraRetakeRequested:
            HandleRetakeRequested();
            break;

        case PvEventType::CameraZoomToggle:
            if (turn_phase_ != PvTurnPhase::Idle) {
                ESP_LOGI(TAG, "Zoom ignorado: turno em andamento");
                break;
            }
            camera_screen_.ToggleZoom();
            break;

        case PvEventType::CameraExportRequested:
            HandleExportRequested();
            break;

        case PvEventType::CameraExportDone:
            // PROVISÓRIO DA F2: reabilita o botão de exportação.
            camera_screen_.ShowExportDone();
            break;

        case PvEventType::CameraSendRequested:
            HandleSendRequested();
            break;

        case PvEventType::TurnDone:
            HandleTurnDone();
            break;

        case PvEventType::VoiceDone:
        case PvEventType::VoiceFailed:
            // EVENTO OBSOLETO (revisão F3, P1e): o PvAudio derruba `busy_`
            // ANTES de avisar, então um evento que chega com o áudio ocupado é
            // de uma voz ANTERIOR — a de um turno abandonado, cujo aviso ficou
            // atrasado na fila enquanto outra resposta já começava. Fechar o
            // terminal do turno NOVO com o fim do turno VELHO tiraria a tela da
            // resposta no meio da explicação. O fim verdadeiro chega por outro
            // evento ou pela reconciliação, que só fecha com !busy().
            if (audio_.busy()) {
                ESP_LOGW(TAG, "Evento de voz obsoleto ignorado: o PvAudio ainda está tocando");
                break;
            }
            if (event.type == PvEventType::VoiceFailed) {
                // §9.7: falha de voz NUNCA trava o fluxo. Avisa e segue pelo
                // MESMO caminho do fim de áudio. O aviso só faz sentido com a
                // resposta na tela; fora dela seria texto órfão.
                ESP_LOGW(TAG, "A voz do professor falhou; seguindo pelo fim de áudio");
                if (turn_phase_ == PvTurnPhase::ShowingResponse) {
                    camera_screen_.ShowNotice(PvStrings::kTurnErrVoice);
                }
            }
            CloseVoiceTerminal();
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
    net_generation_++;
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
    LoadStatusScreen();
}

void PvApp::LoadStatusScreen() {
    // A tela de câmera é a única temporária do PV: qualquer coisa que carregue
    // a tela de status precisa tirá-la de cena antes, senão o preview
    // continuaria rodando (e desenhando) atrás.
    LeaveCameraScreen();
    status_screen_.Load();
}

// ---------------------------------------------------------------------------
// Câmera (F2): preview no LVGL (T3) e captura/revisão da foto (T4).
//
// Divisão de trabalho: a task da câmera produz frames e AVISA; esta task
// decide e desenha; a PvCameraScreen é a dona da lv_image_dsc_t, do empréstimo
// do frame e — na revisão — do JPEG e do RGB565 decodificado. No preview
// nenhum buffer é copiado e nada é alocado por frame; na revisão a alocação é
// pontual e liberada por LeaveCameraScreen()/ExitReview().
// ---------------------------------------------------------------------------

void PvApp::OnCameraEventFromCameraTask(PvCamera::Event event) {
    switch (event) {
        case PvCamera::Event::PreviewFrame: {
            // COALESCÊNCIA (decisão F2-Preview): no máximo UM aviso de frame
            // pendente na fila. A ~5 fps, sem isso, os 8 slots seriam tomados
            // pelo preview e eventos de rede começariam a ser descartados.
            bool expected = false;
            if (!preview_frame_pending_.compare_exchange_strong(expected, true)) {
                return;
            }
            if (!PostEvent(PvEventType::CameraPreviewFrame, std::string(),
                           PvConfigReason::Manual)) {
                // Fila cheia: sem limpar a flag aqui, o preview congelaria
                // para sempre porque nenhum aviso novo seria postado.
                preview_frame_pending_.store(false);
            }
            break;
        }
        case PvCamera::Event::JpegReady:
            // Só sinaliza: o resultado pesado (JPEG + RGB565 decodificado) NÃO
            // vai pela fila; é retirado com TakeCapture() já na task principal,
            // como o padrão Take* do PvWorker. Evento raro, sem coalescência.
            // Se este aviso não couber na fila, a reconciliação periódica
            // (ReconcileCameraState) recupera a tela.
            PostEvent(PvEventType::CameraJpegReady, std::string(), PvConfigReason::Manual);
            break;
        case PvCamera::Event::JpegFailed:
            PostEvent(PvEventType::CameraJpegFailed, std::string(), PvConfigReason::Manual);
            break;
    }
}

void PvApp::OnCameraActionFromLvglTask(PvCameraScreen::Action action) {
    switch (action) {
        case PvCameraScreen::Action::Back:
            PostEvent(PvEventType::CameraScreenClosed, std::string(), PvConfigReason::Manual);
            break;
        case PvCameraScreen::Action::Capture:
            PostEvent(PvEventType::CameraCaptureRequested, std::string(), PvConfigReason::Manual);
            break;
        case PvCameraScreen::Action::Send:
            PostEvent(PvEventType::CameraSendRequested, std::string(), PvConfigReason::Manual);
            break;
        case PvCameraScreen::Action::Retake:
            PostEvent(PvEventType::CameraRetakeRequested, std::string(), PvConfigReason::Manual);
            break;
        case PvCameraScreen::Action::ZoomToggle:
            PostEvent(PvEventType::CameraZoomToggle, std::string(), PvConfigReason::Manual);
            break;
        case PvCameraScreen::Action::Export:
            PostEvent(PvEventType::CameraExportRequested, std::string(), PvConfigReason::Manual);
            break;
    }
}

void PvApp::HandleCaptureRequested() {
    // Comandos bloqueados fora de `idle` (§9.2): o toque pode ter entrado na
    // fila antes de os botões serem desabilitados.
    if (turn_phase_ != PvTurnPhase::Idle) {
        ESP_LOGI(TAG, "Captura ignorada: turno em andamento");
        return;
    }
    // A tela de câmera precisa estar no ar: um toque que sobrou na fila depois
    // de a tela sair não pode disparar uma codificação de meio segundo.
    if (!camera_screen_.IsActive() || camera_screen_.IsReviewing()) {
        return;
    }
    if (!camera_.RequestCaptureJpeg()) {
        // Coalescido (já havia captura em voo) ou câmera indisponível: o botão
        // já está desabilitado no primeiro caso e a tela nem abre no segundo.
        ESP_LOGI(TAG, "Captura ignorada: pedido coalescido ou câmera indisponível");
        return;
    }
    // Feedback imediato: o botão fica desabilitado e o rótulo de estado passa
    // a "Processando..." até JpegReady/JpegFailed.
    camera_screen_.SetCapturing(true);
}

bool PvApp::HandleJpegReady() {
    PvCameraCaptureResult capture;
    if (!camera_.TakeCapture(capture)) {
        // Aviso duplicado de uma captura já retirada (ou, na reconciliação,
        // sinal de que a captura terminou em falha).
        return false;
    }
    if (!camera_screen_.IsCapturing()) {
        // Ninguém está esperando ESTA captura. IsActive() não basta: a tela
        // pode ter fechado durante a codificação e REABERTO antes da conclusão
        // — ativa de novo, mas numa sessão nova, sem captura pedida. Exibir a
        // foto antiga aqui mostraria à criança uma imagem que ela não pediu
        // (revisão F2 rodada final, P1). A posse dos DOIS buffers é nossa e
        // morre aqui, senão vazariam megabytes de PSRAM.
        ESP_LOGW(TAG, "Foto pronta sem tela esperando; descartada");
        heap_caps_free(capture.jpeg.data);
        if (capture.rgb != nullptr) {
            heap_caps_free(capture.rgb);
        }
        return true;
    }

    // Preview e revisão são mutuamente exclusivos (decisão F2-Preview): com a
    // foto na tela, o motor da câmera para de produzir frames.
    camera_.StopPreview();
    if (!camera_screen_.EnterReview(capture)) {
        // EnterReview já liberou os dois buffers em qualquer caminho de falha;
        // aqui só resta explicar e voltar ao preview.
        camera_screen_.ShowCaptureFailed();
        camera_.StartPreview();
    }
    return true;
}

void PvApp::HandleJpegFailed() {
    // Mesma guarda do JpegReady: uma tela reaberta (ativa, mas sem captura
    // pedida NESTA sessão) não pode receber o aviso de falha de uma captura
    // antiga (revisão F2 rodada final, P1).
    if (!camera_screen_.IsCapturing()) {
        return;
    }
    camera_screen_.ShowCaptureFailed();
}

void PvApp::ReconcileCameraState() {
    // Por que existe: CameraJpegReady/JpegFailed/ExportDone são postados com
    // PostEvent(), que é BEST-EFFORT — a fila tem 8 slots e é compartilhada com
    // os eventos de rede. Perder um desses avisos deixaria a tela presa em
    // "Processando..." (botão de captura desabilitado) ou em "Exportando..."
    // para sempre, sem nenhum caminho de recuperação além de sair da tela
    // (revisão F2, P1). Os eventos continuam sendo o caminho rápido; isto aqui
    // é a rede de segurança, verificada a cada timeout de 1 s do laço.
    //
    // Todas as condições são de ESTADO (não de aviso), então executar isto
    // depois de um evento já tratado é apenas um no-op.
    if (camera_screen_.IsCapturing() && !camera_.capture_in_flight()) {
        // A captura terminou e a tela continua esperando. Sucesso e falha se
        // distinguem pelo próprio slot de resultado: se havia captura a
        // retirar, o fluxo é o do JpegReady; senão, o do JpegFailed.
        ESP_LOGW(TAG, "Reconciliação: conclusão de captura perdida; recuperando a tela");
        if (!HandleJpegReady()) {
            HandleJpegFailed();
        }
    }
    // Resultado ÓRFÃO: a captura terminou, mas nenhuma tela espera por ela —
    // a tela fechou durante a codificação e o aviso se perdeu. Sem esta
    // drenagem, o par JPEG+RGB565 ficaria pendurado no slot (megabytes de
    // PSRAM) e, pior, uma captura futura cujo aviso de FALHA também se
    // perdesse faria HandleJpegReady() encontrar o resultado antigo e exibir a
    // foto errada como se fosse a nova (revisão F2 rodada 2, P1). A condição
    // !IsCapturing() garante que nunca drenamos um resultado que a tela ainda
    // vai consumir: enquanto ela espera, o ramo acima é quem o retira.
    if (!camera_screen_.IsCapturing() && !camera_.capture_in_flight()) {
        PvCameraCaptureResult orphan;
        if (camera_.TakeCapture(orphan)) {
            ESP_LOGW(TAG, "Reconciliação: captura órfã drenada e liberada");
            heap_caps_free(orphan.jpeg.data);
            if (orphan.rgb != nullptr) {
                heap_caps_free(orphan.rgb);
            }
        }
    }
    // PROVISÓRIO DA F2: mesmo raciocínio para o dump serial.
    if (camera_screen_.IsExporting() && !PvPhotoDump::busy()) {
        ESP_LOGW(TAG, "Reconciliação: conclusão de exportação perdida; recuperando a tela");
        camera_screen_.ShowExportDone();
    }
}

void PvApp::HandleRetakeRequested() {
    if (turn_phase_ != PvTurnPhase::Idle) {
        ESP_LOGI(TAG, "Nova foto ignorada: turno em andamento");
        return;
    }
    if (!camera_screen_.IsReviewing()) {
        return;
    }
    // Libera a foto e o decodificado ANTES de religar o preview: os buffers de
    // preview já existem, mas manter 2,4 MB de RGB565 pendurado por engano
    // seria o começo da fragmentação que o log de PSRAM da captura denuncia.
    camera_screen_.ExitReview();
    camera_.StartPreview();
}

void PvApp::HandleExportRequested() {
    // PROVISÓRIO DA F2 (decisão F2-LegibilityValidation): dump serial do JPEG
    // só por acionamento explícito, nunca automático.
    if (turn_phase_ != PvTurnPhase::Idle) {
        ESP_LOGI(TAG, "Exportação ignorada: turno em andamento");
        return;
    }
    if (!camera_screen_.IsReviewing()) {
        return;
    }
    const PvCameraJpeg* photo = camera_screen_.photo();
    if (photo == nullptr) {
        return;
    }
    if (!PvPhotoDump::Request(photo->data, photo->len, photo->width, photo->height)) {
        camera_screen_.ShowExportBusy();
        return;
    }
    camera_screen_.SetExporting(true);
}

void PvApp::ShowCameraScreen() {
    if (!camera_.available()) {
        ESP_LOGW(TAG, "Pedido de câmera ignorado: placa sem câmera disponível");
        return;
    }
    if (config_screen_.IsVisible()) {
        // O adulto está no meio da configuração; não roubar a tela.
        return;
    }
    // GUARDA DE ROTA (revisão F2, P1): só Preparação (preview provisório da F2)
    // e Tutoria (turno por foto da F3) têm botão de câmera, e a rota CORRENTE é
    // a autoridade — não a tela de onde o toque saiu. Um pedido que ficou na
    // fila e é processado DEPOIS de uma re-hidratação trocar a rota abriria a
    // câmera por cima de Failsafe (que existe justamente para tirar a criança
    // do dispositivo) ou de Celebration, e por isso essas duas continuam
    // bloqueadas explicitamente.
    const bool route_allows_camera =
        route_screens_.IsLoaded() && (route_screens_.route() == PvRoute::Preparation ||
                                      route_screens_.route() == PvRoute::Tutoring);
    if (!route_allows_camera) {
        ESP_LOGW(TAG, "Pedido de câmera recusado: rota corrente é %s",
                 route_screens_.IsLoaded() ? PvRouteName(route_screens_.route()) : "(nenhuma)");
        return;
    }
    if (camera_screen_.IsActive()) {
        return;
    }
    if (!camera_screen_.Show()) {
        return;
    }
    // Só depois de a tela estar no ar: um frame que chegue antes disso viraria
    // um empréstimo sem consumidor.
    camera_.StartPreview();
}

void PvApp::LeaveCameraScreen() {
    if (!camera_screen_.IsActive()) {
        // Mesmo sem tela no ar a fase precisa voltar a Idle: um caminho de
        // saída que corra com a tela já fechada não pode deixar o PV
        // bloqueando comandos para sempre.
        ResetTurnFlow();
        return;
    }
    // Sair da tela ABANDONA o turno em curso (gesto de configuração, rota nova
    // por re-hidratação, tela de status). O que estiver em voo continua e é
    // descartado ao chegar — por geração, quando houve fronteira de rede, ou
    // por fase, quando não houve. A VOZ que já estiver tocando não é cortada:
    // ela não referencia nada desta tela, e cortá-la no meio de uma explicação
    // seria pior que deixá-la terminar.
    ResetTurnFlow();
    // Ordem obrigatória: parar de produzir, depois soltar o empréstimo,
    // esvaziar a imagem e LIBERAR a foto em revisão (a tela faz as três coisas
    // sob um único lock). Avisos de frame que já estejam na fila viram no-op,
    // porque a tela sai de IsActive() aqui. Este é o ponto por onde passam
    // TODOS os caminhos de saída — "Voltar", gesto de configuração, rota nova
    // por re-hidratação e tela de status —, e por isso é a única garantia de
    // liberação de que o JPEG e o RGB565 decodificado precisam.
    camera_.StopPreview();
    camera_screen_.Hide();
}

void PvApp::ReturnFromCameraScreen() {
    if (!camera_screen_.IsActive()) {
        return;
    }
    LeaveCameraScreen();
    // A tela de câmera é sobreposta à rota corrente. Sem rota carregada (só
    // acontece se o botão for alcançado por um caminho futuro), volta ao
    // status: nunca deixar a criança numa tela morta.
    if (!route_screens_.Reload()) {
        status_screen_.Load();
    }
}

// ---------------------------------------------------------------------------
// Turno por foto (F3/T6): revisão -> "Enviar" -> resposta -> preview ou rota.
//
// Divisão de trabalho: esta task decide e desenha; o PvWorker faz o HTTP e a
// decodificação do JPEG do tutor; o PvAudio toca o feedback e a voz. Nenhum
// dos dois toca LVGL, e nada aqui bloqueia.
//
// Mapa dos caminhos de erro (§9.7 + decisões F3-D4/D5/D6), todos SEM retry
// automático e todos apagando `pending_request_id_` — um envio futuro é sempre
// um TURNO NOVO, com UUID NOVO:
//
//   401/503 (needs_credentials) -> tela de configuração. Nunca resposta
//       pedagógica, nunca aviso na tela da criança.
//   erro COM resposta do servidor (409, 502, 4xx, 5xx) -> fase
//       ErrorRecovering: a tela fica BLOQUEADA num rótulo neutro e NADA é
//       decidido até a tentativa de re-hidratação terminar (§9.7). Só então a
//       rota nova vence (no failsafe, o overlay É o aviso) ou a tela é
//       destravada na REVISÃO, com o aviso curto. O espelho é marcado como
//       velho, então só depois da re-hidratação um turno novo é aceito — que é
//       exatamente o que o contrato exige depois de um 409.
//   erro SEM resposta (rede/timeout) -> aviso IMEDIATO; volta à revisão;
//       espelho marcado como velho e re-hidratação tentada (a reconexão também
//       re-hidrata sozinha). Sem resposta não há estado novo a esperar.
//   falha de download/decode DEPOIS do 200 -> o turno FOI aplicado no
//       servidor, logo também é "com resposta": mesmo ErrorRecovering, com
//       destino PREVIEW. Voltar à revisão convidaria a reenviar uma foto que
//       já virou turno.
//   voz que não toca -> §9.7: avisa e segue pelo mesmo caminho do fim de
//       áudio; nunca trava o fluxo.
// ---------------------------------------------------------------------------

void PvApp::OnAudioEventFromAudioTask(PvAudio::Event event) {
    // Roda na task do PvAudio: só sinaliza. Se este aviso não couber na fila,
    // a ReconcileTurnState() recupera o fluxo pelo estado do PvAudio.
    PostEvent(
        event == PvAudio::Event::VoiceDone ? PvEventType::VoiceDone : PvEventType::VoiceFailed,
        std::string(), PvConfigReason::Manual);
}

void PvApp::HandleSendRequested() {
    if (turn_phase_ != PvTurnPhase::Idle) {
        ESP_LOGI(TAG, "Envio ignorado: já há um turno em andamento");
        return;
    }
    if (!camera_screen_.IsReviewing()) {
        return;
    }

    // Pré-condições do turno. Nenhuma delas é "quase": sem espelho fresco não
    // se sabe para qual tarefa a foto é, e sem sessão ativa o backend
    // recusaria. Melhor recusar aqui, sem gastar 120 s e sem queimar um UUID.
    //
    // `hydration_pending` entra na lista (revisão F3, P1d): com uma hidratação
    // pendente o espelho está sendo TROCADO — em execução OU já pronto e ainda
    // não aplicado (revisão F3 rodada 2, P1) —, e um espelho em troca não é
    // base para turno: a foto poderia sair marcada para a tarefa antiga.
    // `audio_.busy()` também (P1e): a voz de um turno anterior, abandonada pela
    // UI, precisa terminar antes que outra resposta toque por cima dela.
    if (!network_connected_ || !PvSettings::IsConfigured() || !hydrated_ ||
        !CanSendTurn(session_state_) || worker_.hydration_pending() || audio_.busy()) {
        ESP_LOGW(TAG,
                 "Envio recusado: rede=%d configurado=%d hidratado=%d sessão utilizável=%d "
                 "hidratação pendente=%d voz tocando=%d",
                 (int)network_connected_, (int)PvSettings::IsConfigured(), (int)hydrated_,
                 (int)CanSendTurn(session_state_), (int)worker_.hydration_pending(),
                 (int)audio_.busy());
        camera_screen_.ShowNotice(PvStrings::kTurnNotReady);
        return;
    }

    const PvCameraJpeg* photo = camera_screen_.photo();
    if (photo == nullptr || photo->data == nullptr || photo->len == 0) {
        camera_screen_.ShowNotice(PvStrings::kTurnNotReady);
        return;
    }

    // CÓPIA proposital: a tela continua dona do JPEG dela (a criança ainda o
    // está vendo, e o dump de diagnóstico pode estar lendo o mesmo buffer),
    // enquanto o worker precisa de um buffer que sobreviva por até 120 s. São
    // algumas centenas de KB e, com CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL, a
    // cópia nasce em PSRAM.
    std::vector<uint8_t> jpeg(photo->data, photo->data + photo->len);

    std::string request_id = PvBackendClient::NewRequestId();
    if (!worker_.RequestTurnPhoto(net_generation_, session_state_.session_id, request_id,
                                  std::move(jpeg))) {
        // Um turno por vez, por contrato (cliente único/serial). Acontece
        // quando um turno anterior foi abandonado pela UI e ainda está em voo:
        // o UUID recém-gerado é simplesmente descartado, sem nunca ter ido à
        // rede.
        ESP_LOGW(TAG, "Envio recusado pelo worker: turno anterior ainda em voo");
        camera_screen_.ShowNotice(PvStrings::kTurnNotReady);
        return;
    }

    pending_request_id_ = std::move(request_id);
    turn_phase_ = PvTurnPhase::Sending;
    voice_terminal_ = false;
    hydration_terminal_ = false;
    response_route_valid_ = false;
    // Feedback imediato: rótulo "Enviando..." e TODOS os botões bloqueados.
    camera_screen_.SetSending(true);
    // O request_id é do CLIENTE e não é segredo (o token, esse, nunca aparece):
    // registrá-lo é o que permite rastrear no log qual turno lógico virou 409.
    ESP_LOGI(TAG, "Turno enviado: request_id %s, %u bytes de foto", pending_request_id_.c_str(),
             (unsigned)photo->len);
}

bool PvApp::HandleTurnDone() {
    PvWorker::TurnResult result;
    if (!worker_.TakeTurn(result)) {
        // Aviso duplicado de um turno já consumido (ou, na reconciliação,
        // sinal de que não havia nada em voo).
        return false;
    }
    // Daqui para baixo, o RGB decodificado é liberado pelo destrutor de
    // `result` em TODOS os caminhos que não o entregarem à tela.

    if (result.generation != net_generation_ || !network_connected_) {
        // Resultado de uma geração anterior: a rede caiu ou o adulto abriu a
        // configuração com o HTTP em voo. Não decide rota, não toca a máquina
        // de estados e não vira resposta pedagógica. Mas a EVIDÊNCIA do
        // http_status não é apagada (revisão F3 rodada 2, P0): um 200 (mesmo
        // com corpo perdido) significa turno APLICADO, e um 409 significa
        // resultado indeterminado — nos dois casos a foto em revisão não pode
        // voltar a ficar enviável, senão um clique novo aplicaria o mesmo
        // conteúdo de novo, silenciosamente, com outro UUID.
        const bool maybe_applied =
            result.backend.http_status == 200 || result.backend.http_status == 409;
        ESP_LOGW(TAG, "Turno obsoleto descartado (geração %u != %u, HTTP %d)",
                 static_cast<unsigned>(result.generation), static_cast<unsigned>(net_generation_),
                 result.backend.http_status);
        if (turn_phase_ == PvTurnPhase::Sending) {
            // A tela pode ter continuado no ar (queda de rede com rota
            // carregada mantém a tela da criança): destravá-la é obrigatório.
            // Sem `answered`: a geração já mudou, então não há re-hidratação
            // desta geração para esperar (a da reconexão fará a re-consulta;
            // `rehydrate` marca o espelho como velho até lá).
            FinishTurnWithError(PvStrings::kTurnErrNetwork, /*rehydrate=*/maybe_applied,
                                /*to_preview=*/maybe_applied, /*answered=*/false);
        } else {
            pending_request_id_.clear();
        }
        return true;
    }

    if (turn_phase_ != PvTurnPhase::Sending) {
        // A tela saiu no meio do turno (gesto de configuração, rota nova por
        // re-hidratação). O turno pode ter sido aplicado no servidor, e é a
        // hidratação — não este resultado — que vai contar isso.
        ESP_LOGW(TAG, "Turno concluído fora da fase de envio; descartado");
        pending_request_id_.clear();
        return true;
    }

    // 401/503 interrompem o fluxo e vão para a configuração, nunca para uma
    // resposta pedagógica.
    if (result.backend.needs_credentials()) {
        ESP_LOGW(TAG, "Turno recusado por credencial: %s",
                 PvBackendClient::StatusName(result.backend.status));
        pending_request_id_.clear();
        turn_phase_ = PvTurnPhase::Idle;
        // ShowConfigScreen sai da tela de câmera (e, com ela, do fluxo do
        // turno) antes de abrir a configuração.
        ShowConfigScreen(result.backend.status == PvBackendStatus::Unauthorized
                             ? PvConfigReason::Unauthorized
                             : PvConfigReason::Unavailable);
        return true;
    }

    if (result.stage != PvWorker::TurnStage::Complete) {
        ESP_LOGW(TAG, "Turno falhou na etapa %d: %s (HTTP %d)", static_cast<int>(result.stage),
                 PvBackendClient::StatusName(result.backend.status), result.backend.http_status);
        if (result.stage == PvWorker::TurnStage::Post) {
            // O POST não completou. Com resposta do servidor (409/502/4xx/5xx)
            // o estado PODE ter mudado, então re-hidrata antes de decidir a
            // interface; sem resposta, a re-hidratação vem da reconexão — mas
            // marcamos o espelho como velho do mesmo jeito, porque um timeout
            // pode ter deixado o turno aplicado sem ninguém saber.
            //
            // O DESTINO da foto vem do http_status, não só da etapa (revisão
            // F3 rodada 2, P0): 200 com corpo interrompido/inválido/eco
            // divergente = turno APLICADO; 409 = indeterminado (pode ter sido
            // aplicado). Nos dois casos a foto sai da revisão — reenviá-la
            // seria dupla aplicação silenciosa. 502 (veredicto descartado no
            // servidor, §7.5) e 4xx sem efeito mantêm a revisão.
            const bool answered = result.backend.http_status != 0;
            const bool maybe_applied =
                result.backend.http_status == 200 || result.backend.http_status == 409;
            FinishTurnWithError(answered ? PvStrings::kTurnErrServer : PvStrings::kTurnErrNetwork,
                                /*rehydrate=*/true, /*to_preview=*/maybe_applied, answered);
            return true;
        }
        // Etapa de mídia: o 200 já chegou, logo o turno FOI aplicado. Voltar à
        // revisão convidaria a criança a reenviar uma foto que já virou turno.
        // E, como o servidor respondeu, a interface só pode ser decidida depois
        // da re-consulta: `answered` é verdadeiro aqui.
        FinishTurnWithError(PvStrings::kTurnErrMedia, /*rehydrate=*/true, /*to_preview=*/true,
                            /*answered=*/true);
        return true;
    }

    // ---- Caminho feliz ----
    ESP_LOGI(TAG, "Resposta do turno: veredicto %s, sessão %s",
             PvVerdictName(result.response.veredicto), result.response.session_status.c_str());

    // O UUID cumpriu o papel: a resposta chegou e foi aceita (o eco do
    // request_id já foi conferido pelo PvBackendClient). Um envio futuro é
    // outro turno lógico, com outro id.
    pending_request_id_.clear();

    // A voz é a resposta (§9.3): o feedback local sai antes dela, dentro do
    // PvAudio, e o WAV vai por movimento — a partir daqui o dono é a task de
    // áudio.
    const PvAudio::Feedback feedback = FeedbackForVerdict(result.response.veredicto);
    const bool voice_accepted = audio_.PlayTurnAudio(std::move(result.wav), feedback);

    // Posse do RGB565 transferida à tela, como no EnterReview: ForgetRgb()
    // ANTES da chamada deixa explícito que `result` não libera mais nada.
    PvCameraScreen::ResponseImage image;
    image.data = result.rgb;
    image.len = result.rgb_len;
    image.width = result.rgb_width;
    image.height = result.rgb_height;
    image.stride = result.rgb_stride;
    result.ForgetRgb();
    if (!camera_screen_.EnterResponse(image)) {
        // A tela recusou (buffer inválido ou tela fora do ar). O buffer já foi
        // liberado lá dentro; o fluxo continua, porque a voz é a resposta.
        ESP_LOGW(TAG, "Imagem do professor não pôde ser exibida");
    }

    turn_phase_ = PvTurnPhase::ShowingResponse;
    // Terminal (a): sem voz aceita, ele já nasce fechado — PlayTurnAudio só
    // recusa quando não há áudio na placa ou quando outra voz está tocando, e
    // em nenhum dos casos virá evento algum.
    voice_terminal_ = !voice_accepted;
    hydration_terminal_ = false;
    response_route_valid_ = false;
    if (!voice_accepted) {
        ESP_LOGW(TAG, "Voz recusada pelo PvAudio; terminal de voz já fechado");
        camera_screen_.ShowNotice(PvStrings::kTurnErrVoice);
    }

    // Terminal (b): re-hidratação IMEDIATA (§9.4 — áudio e estado terminam em
    // qualquer ordem, e a decisão só sai quando os dois terminarem).
    StartHydration();
    // Caso extremo: PvAudio indisponível E hidratação que nem saiu do lugar.
    // Os dois terminais podem já estar fechados aqui.
    FinishResponseIfComplete();
    return true;
}

void PvApp::FinishTurnWithError(const char* message, bool rehydrate, bool to_preview,
                                bool answered) {
    // DESCARTE DO ID PENDENTE (contrato v1.1, regra do 409): o turno lógico
    // morre aqui. Um envio futuro gera outro UUID; retransmitir o mesmo não
    // existe na F3.
    if (!pending_request_id_.empty()) {
        ESP_LOGW(TAG, "Turno abandonado; request_id %s descartado", pending_request_id_.c_str());
        pending_request_id_.clear();
    }
    voice_terminal_ = false;
    hydration_terminal_ = false;
    response_route_valid_ = false;

    if (rehydrate) {
        // ANTES de decidir a interface (§9.7): o estado do servidor pode ter
        // mudado (inclusive para failsafe). Marcar o espelho como velho é o
        // que impede um turno novo antes da re-consulta — a regra do 409. Se a
        // hidratação falhar, o tick de health tenta de novo a cada 10 s.
        hydrated_ = false;
    }

    if (answered) {
        // ERRO COM RESPOSTA (revisão F3, P1a): nada é decidido agora. Mostrar o
        // aviso e devolver os botões aqui deixaria a criança de volta na
        // revisão meio segundo antes de o failsafe assumir a tela — e o §9.7 é
        // explícito: com resposta do servidor, re-consultar ANTES de mostrar
        // qualquer coisa. A tela continua bloqueada (SetSending segue ligado)
        // com um rótulo neutro; a mensagem e o destino ficam guardados.
        turn_phase_ = PvTurnPhase::ErrorRecovering;
        pending_error_message_ = message;
        pending_error_to_preview_ = to_preview;
        if (camera_screen_.IsActive()) {
            camera_screen_.ShowNotice(PvStrings::kTurnRecovering);
        }
        // `rehydrate` é sempre true neste caminho (quem responde muda estado).
        // Se o pedido não chegar a sair (rede caída, tela de configuração
        // aberta), a ReconcileTurnState fecha a fase no próximo laço: ninguém
        // fica preso no "Só um instante...".
        StartHydration();
        return;
    }

    turn_phase_ = PvTurnPhase::Idle;
    if (rehydrate) {
        StartHydration();
    }

    if (!camera_screen_.IsActive()) {
        return;
    }
    camera_screen_.SetSending(false);
    if (to_preview) {
        camera_screen_.ExitReview();
        camera_.StartPreview();
    }
    // Depois de SetSending/ExitReview, que reescrevem o rótulo de estado: o
    // aviso é a última palavra na tela. Ele é curto, em pt-BR, e não carrega
    // status HTTP nem nada vindo do corpo da resposta.
    camera_screen_.ShowNotice(message);
}

void PvApp::FinishErrorRecovery(bool route_valid, PvRoute route) {
    if (turn_phase_ != PvTurnPhase::ErrorRecovering) {
        return;
    }
    // Lidos ANTES de qualquer coisa: LeaveCameraScreen() zera o fluxo do turno.
    const char* message = pending_error_message_;
    const bool to_preview = pending_error_to_preview_;
    turn_phase_ = PvTurnPhase::Idle;
    pending_error_message_ = nullptr;
    pending_error_to_preview_ = false;

    if (route_valid && route != PvRoute::Tutoring) {
        // A ROTA VENCE o aviso (§9.7): no failsafe o overlay É o aviso, e um
        // texto genérico por cima só competiria com o que a tela já diz. Vale
        // para Preparation e Celebration pela mesma razão — a tela nova já
        // conta o que aconteceu. Tutoria NÃO tira a criança da câmera na F3.
        ESP_LOGI(TAG, "Erro do turno: rota %s assume a tela", PvRouteName(route));
        LeaveCameraScreen();
        route_screens_.Show(route, session_state_, lesson_);
        return;
    }

    if (!camera_screen_.IsActive()) {
        return;
    }
    camera_screen_.SetSending(false);
    if (to_preview) {
        camera_screen_.ExitReview();
        camera_.StartPreview();
    }
    camera_screen_.ShowNotice(message != nullptr ? message : PvStrings::kTurnErrServer);
}

void PvApp::CloseVoiceTerminal() {
    if (turn_phase_ != PvTurnPhase::ShowingResponse || voice_terminal_) {
        // Evento atrasado de um turno que já terminou (ou já contabilizado
        // pela reconciliação): fechar duas vezes não pode adiantar a saída.
        return;
    }
    voice_terminal_ = true;
    FinishResponseIfComplete();
}

void PvApp::CloseHydrationTerminal() {
    if (turn_phase_ != PvTurnPhase::ShowingResponse || hydration_terminal_) {
        return;
    }
    hydration_terminal_ = true;
    FinishResponseIfComplete();
}

void PvApp::FinishResponseIfComplete() {
    if (turn_phase_ != PvTurnPhase::ShowingResponse || !voice_terminal_ || !hydration_terminal_) {
        return;
    }
    // Lidos ANTES de qualquer coisa: LeaveCameraScreen() zera o fluxo do turno,
    // inclusive a rota guardada.
    const bool route_wins = response_route_valid_ && response_route_ != PvRoute::Tutoring;
    const PvRoute route = response_route_;

    turn_phase_ = PvTurnPhase::Idle;
    voice_terminal_ = false;
    hydration_terminal_ = false;
    response_route_valid_ = false;

    if (route_wins) {
        // A hidratação é a única autoridade sobre a rota (§9.1): failsafe,
        // celebração e preparação vencem a tela de câmera. Tutoria NÃO tira a
        // criança da câmera na F3 — a tela de tutoria de verdade é da F5, e
        // mandá-la para um placeholder mataria o fluxo de fotos.
        ESP_LOGI(TAG, "Fim da resposta: rota %s assume a tela", PvRouteName(route));
        LeaveCameraScreen();
        route_screens_.Show(route, session_state_, lesson_);
        return;
    }

    ESP_LOGI(TAG, "Fim da resposta: de volta ao preview da câmera");
    camera_screen_.ExitResponse();
    camera_.StartPreview();
}

void PvApp::ResetTurnFlow() {
    if (turn_phase_ != PvTurnPhase::Idle) {
        ESP_LOGI(TAG, "Fluxo do turno abandonado na fase %d (request_id %s)",
                 static_cast<int>(turn_phase_),
                 pending_request_id_.empty() ? "-" : pending_request_id_.c_str());
    }
    turn_phase_ = PvTurnPhase::Idle;
    pending_request_id_.clear();
    voice_terminal_ = false;
    hydration_terminal_ = false;
    response_route_valid_ = false;
    // Sair da tela também ABANDONA o aviso guardado do ErrorRecovering: quem
    // decide o que a criança vê agora é a tela nova, não um erro do turno que
    // ela já deixou para trás.
    pending_error_message_ = nullptr;
    pending_error_to_preview_ = false;
}

void PvApp::ReconcileTurnState() {
    // Mesma razão da ReconcileCameraState: TurnDone, VoiceDone/VoiceFailed e
    // HydrationDone são postados com PostEvent(), que é BEST-EFFORT. Perder um
    // deles deixaria a tela presa em "Enviando..." ou em "Professor
    // respondendo...", com todos os botões desabilitados — sem saída nenhuma
    // para a criança. Todas as condições abaixo são de ESTADO, então rodar
    // isto depois de um evento já tratado é apenas um no-op.
    if (!worker_.turn_in_flight()) {
        // Drena SEMPRE que houver resultado: dentro da fase ele é processado;
        // fora dela, descartado (o que LIBERA o RGB). Um resultado esquecido
        // no slot seria megabytes de PSRAM pendurados.
        const bool took = HandleTurnDone();
        if (!took && turn_phase_ == PvTurnPhase::Sending) {
            // Nada em voo e nada a retirar, mas a tela ainda espera: o pedido
            // se perdeu antes de virar resultado. Destrava sem inventar
            // resposta nenhuma.
            ESP_LOGW(TAG, "Reconciliação: turno sem resultado; destravando a tela");
            // Sem resultado não houve resposta do servidor: aviso imediato.
            FinishTurnWithError(PvStrings::kTurnErrNetwork, /*rehydrate=*/true,
                                /*to_preview=*/false, /*answered=*/false);
        }
    }

    if (!worker_.hydrate_in_flight()) {
        // Drenagem GENÉRICA de hidratação, em QUALQUER fase (revisão F3
        // rodada 3, P1): um HydrationDone perdido com o turno em Idle deixaria
        // `hydration_ready_` armado para sempre — e a guarda
        // `hydration_pending()` passaria a recusar TODOS os envios, sem que
        // nenhum tick voltasse a consumir o resultado. HandleHydrationDone é
        // no-op quando não há nada a retirar, e fora de turno faz exatamente o
        // que o evento perdido teria feito (aplicar espelho e rota).
        HandleHydrationDone();
        if (turn_phase_ == PvTurnPhase::ErrorRecovering) {
            // Rede de segurança do sub-estado (revisão F3, P1a): a tentativa
            // de re-hidratação acabou sem fechar o terminal (ou nem chegou a
            // sair — rede caída, pedido não enfileirado). Sem isto a tela
            // ficaria presa no "Só um instante..." com tudo desabilitado.
            ESP_LOGW(TAG, "Reconciliação: re-hidratação do erro sem resultado; decidindo sem rota");
            FinishErrorRecovery(/*route_valid=*/false, PvRoute::Preparation);
        }
    }

    if (turn_phase_ != PvTurnPhase::ShowingResponse) {
        return;
    }
    if (!voice_terminal_ && !audio_.busy()) {
        // O PvAudio não está mais tocando: o áudio de fato parou, então o
        // terminal (a) fecha como concluído. Conservador de propósito — o
        // caminho de falha já teria fechado o terminal do mesmo jeito (§9.7).
        ESP_LOGW(TAG, "Reconciliação: fim de voz perdido; terminal fechado");
        CloseVoiceTerminal();
    }
    if (!hydration_terminal_ && !worker_.hydrate_in_flight()) {
        // Pode ser um aviso perdido (há resultado a retirar) ou uma hidratação
        // que nem chegou a sair (rede caída, pedido não enfileirado).
        HandleHydrationDone();
        if (!hydration_terminal_) {
            ESP_LOGW(TAG, "Reconciliação: re-hidratação pós-turno sem resultado; terminal fechado");
            CloseHydrationTerminal();
        }
    }
}

void PvApp::ShowConfigScreen(PvConfigReason reason) {
    // A tela de câmera não pode ficar como "tela anterior" da configuração: ao
    // voltar, ela estaria com o preview desligado e a imagem vazia. Sai dela
    // primeiro, devolvendo a tela de baixo, e só então a configuração entra.
    ReturnFromCameraScreen();

    phase_ = PvPhase::AwaitingBackendConfig;
    // Invalida qualquer resultado HTTP em voo (ressalva F1-ConfigGesture):
    // uma hidratação que termine agora não pode fechar/roubar a tela de
    // configuração que o adulto acabou de abrir.
    net_generation_++;
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
        LoadStatusScreen();
    }

    StartHealthTimer();

    if (!worker_.RequestHydrate(net_generation_)) {
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

    if (result.generation != net_generation_ || !network_connected_) {
        // Resultado de uma geração de conectividade anterior (a rede caiu ou
        // entrou em config mode com o HTTP em voo): descartar, sem tocar
        // fase, telas ou máquina de estados (revisão F1, P1).
        ESP_LOGW(TAG, "Hidratação obsoleta descartada (geração %u != %u)",
                 static_cast<unsigned>(result.generation), static_cast<unsigned>(net_generation_));
        // O terminal (b) fecha como FALHA: sem hidratação válida não há
        // autorização para inferir avanço nenhum — mas a tela também não pode
        // ficar presa esperando um resultado que nunca virá (F3-D5). O mesmo
        // vale para o ErrorRecovering: sem espelho novo, rota nenhuma vence.
        CloseHydrationTerminal();
        FinishErrorRecovery(/*route_valid=*/false, PvRoute::Preparation);
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
        ESP_LOGI(TAG, "Hidratação concluída; rota: %s", PvRouteName(route));

        if (turn_phase_ == PvTurnPhase::ShowingResponse) {
            // Re-hidratação PÓS-TURNO: a tela pertence à resposta do professor
            // até a voz terminar (F3-D5/§9.4). Trocar de tela agora cortaria a
            // explicação no meio, então a rota é GUARDADA e aplicada no
            // fechamento dos dois terminais.
            response_route_ = route;
            response_route_valid_ = true;
            if (!Application::GetInstance().SetDeviceState(kDeviceStateIdle)) {
                ESP_LOGE(TAG, "Transição para 'idle' recusada pela máquina de estados");
            }
            StartHealthTimer();
            CloseHydrationTerminal();
            return;
        }

        if (turn_phase_ == PvTurnPhase::Sending) {
            // Hidratação que terminou com o POST do turno EM VOO (revisão F3,
            // P1d). Trocar a tela agora mataria o consumidor de uma chamada de
            // até 120 s: o resultado chegaria fora da fase de envio e seria
            // descartado, e a criança perderia o turno que já foi cobrado do
            // servidor. Só o ESPELHO é atualizado; a rota fica para o
            // fechamento do fluxo — a re-hidratação pós-turno existe
            // exatamente para decidi-la.
            ESP_LOGI(TAG, "Hidratação concluída com turno em voo; só o espelho foi atualizado");
            if (!Application::GetInstance().SetDeviceState(kDeviceStateIdle)) {
                ESP_LOGE(TAG, "Transição para 'idle' recusada pela máquina de estados");
            }
            StartHealthTimer();
            return;
        }

        if (turn_phase_ == PvTurnPhase::ErrorRecovering) {
            // É ESTA hidratação que o sub-estado de erro estava esperando
            // (§9.7): com o espelho novo em mãos, a interface pode enfim ser
            // decidida — rota nova, se houver, ou o aviso guardado.
            if (!Application::GetInstance().SetDeviceState(kDeviceStateIdle)) {
                ESP_LOGE(TAG, "Transição para 'idle' recusada pela máquina de estados");
            }
            StartHealthTimer();
            FinishErrorRecovery(/*route_valid=*/true, route);
            return;
        }

        // SetBackendHealthy já marcou o indicador; uma tela de rota criada
        // agora nasce com o estado correto. Uma rota nova por cima da tela de
        // câmera é saída de tela como qualquer outra: o preview precisa parar.
        LeaveCameraScreen();
        route_screens_.Show(route, session_state_, lesson_);

        if (!Application::GetInstance().SetDeviceState(kDeviceStateIdle)) {
            ESP_LOGE(TAG, "Transição para 'idle' recusada pela máquina de estados");
        }
        StartHealthTimer();
        return;
    }

    hydrated_ = false;
    SetBackendHealthy(false);
    // Terminal (b) fecha mesmo com falha: ele é "a TENTATIVA de re-hidratação
    // terminou", não "o estado novo chegou". Sem espelho novo, nenhuma rota é
    // trocada — a tela volta ao preview quando a voz acabar.
    CloseHydrationTerminal();
    // Mesma regra para o sub-estado de erro: a tentativa acabou, então o aviso
    // guardado sai agora, na tela em que a criança já estava.
    FinishErrorRecovery(/*route_valid=*/false, PvRoute::Preparation);

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
    if (!worker_.RequestHealth(net_generation_)) {
        ESP_LOGD(TAG, "Health já em andamento; tick ignorado");
    }
}

void PvApp::HandleHealthDone() {
    PvBackendResult result;
    uint32_t generation = 0;
    if (!worker_.TakeHealth(result, generation)) {
        return;
    }

    if (generation != net_generation_ || !network_connected_) {
        ESP_LOGW(TAG, "Health obsoleto descartado (geração %u != %u)",
                 static_cast<unsigned>(generation), static_cast<unsigned>(net_generation_));
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
    LoadStatusScreen();
}

void PvApp::SetBackendHealthy(bool healthy) {
    backend_healthy_ = healthy;
    status_screen_.SetConnected(healthy);
    route_screens_.SetConnected(healthy);
    camera_screen_.SetConnected(healthy);
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
