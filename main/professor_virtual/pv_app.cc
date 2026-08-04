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
            camera_screen_.ToggleZoom();
            break;

        case PvEventType::CameraExportRequested:
            HandleExportRequested();
            break;

        case PvEventType::CameraExportDone:
            // PROVISÓRIO DA F2: reabilita o botão de exportação.
            camera_screen_.ShowExportDone();
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
    // GUARDA DE ROTA (revisão F2, P1): o botão que pede a câmera só existe na
    // tela de Preparação. Um pedido que ficou na fila e é processado DEPOIS de
    // uma re-hidratação trocar a rota abriria a câmera por cima de Failsafe ou
    // de Tutoria — e o failsafe existe justamente para tirar a criança do
    // dispositivo. A rota corrente é a autoridade, não a origem do pedido.
    if (!route_screens_.IsLoaded() || route_screens_.route() != PvRoute::Preparation) {
        ESP_LOGW(TAG, "Pedido de câmera recusado: rota corrente é %s (só Preparação abre a câmera)",
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
        return;
    }
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
