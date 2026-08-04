#ifndef PV_APP_H
#define PV_APP_H

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "pv_camera.h"
#include "pv_session_mirror.h"
#include "pv_worker.h"
#include "ui/pv_camera_screen.h"
#include "ui/pv_config_screen.h"
#include "ui/pv_route_screens.h"
#include "ui/pv_status_screen.h"

// Fase fina do Professor Virtual. É um enum próprio do PV: a
// DeviceStateMachine do assistente continua sendo a fonte de verdade dos
// estados globais (Starting/WifiConfiguring/...), e só é tocada na task do
// PvApp (decisão F1-StateMachine 3b).
enum class PvPhase : uint8_t {
    Booting,                // antes de StartNetwork()
    WifiConnecting,         // procurando/conectando
    WifiConfigMode,         // AP de provisionamento do WifiBoard no ar
    AwaitingBackendConfig,  // tela de configuração do backend aberta
    Online,                 // Wi-Fi conectado e backend provisionado
    Offline,                // rede caiu
};

// Aplicativo Professor Virtual: substitui o assistente XiaoZhi quando
// CONFIG_PROFESSOR_VIRTUAL=y. O Application do assistente continua compilado,
// mas seu Run() nunca é chamado (decisão Q1a do decision-log) — por isso o PV
// não pode depender de Application::Schedule() nem de Application::Alert().
class PvApp {
public:
    static PvApp& GetInstance() {
        static PvApp instance;
        return instance;
    }

    void Initialize();
    [[noreturn]] void Run();

    // Pede à task do PvApp que abra a tela de configuração do backend.
    // Thread-safe (só posta na fila); é o ponto de entrada da T5 para 401/503.
    void RequestConfigScreen(PvConfigReason reason);

    // Pede à task do PvApp que abra a tela de câmera (F2). Thread-safe (só
    // posta na fila); chamada pelo botão provisório da tela de preparação.
    void RequestCameraScreen();

    PvPhase phase() const { return phase_; }

private:
    enum class PvEventType : uint8_t {
        NetScanning,
        NetConnecting,
        NetConnected,
        NetDisconnected,
        NetConfigModeEnter,
        NetConfigModeExit,
        ConfigScreenRequested,
        ConfigSaveRequested,
        // Postados pelo timer de 10 s e pela task de rede (PvWorker). O dado
        // pesado (espelho da sessão/lição) NÃO vem na fila: fica no worker e é
        // retirado com Take*() já dentro da task principal.
        HealthTick,
        HydrationDone,
        HealthDone,
        // Câmera (F2). CameraPreviewFrame é postado pela TASK DA CÂMERA a ~5
        // fps e por isso é coalescido em `preview_frame_pending_`: a fila de 8
        // slots é compartilhada com os eventos de rede e não pode ser inundada.
        CameraScreenRequested,
        CameraScreenClosed,
        CameraPreviewFrame,
        // Captura e revisão da foto (F2/T4). Todos raros (um toque de dedo ou
        // uma foto pronta), por isso NENHUM deles é coalescido na fila: a
        // coalescência que importa está um nível abaixo, no
        // PvCamera::RequestCaptureJpeg e no PvPhotoDump::Request.
        CameraCaptureRequested,
        CameraJpegReady,
        CameraJpegFailed,
        CameraRetakeRequested,
        CameraZoomToggle,
        CameraExportRequested,  // PROVISÓRIO DA F2
        CameraExportDone,       // PROVISÓRIO DA F2
    };

    // POD trafegado pela fila. `data` guarda o SSID (máx. 32 bytes + NUL) e
    // nunca carrega segredo: o token vai por `pending_token_`, sob mutex.
    struct PvEvent {
        PvEventType type;
        PvConfigReason reason;
        char data[33];
    };

    PvApp() = default;
    PvApp(const PvApp&) = delete;
    PvApp& operator=(const PvApp&) = delete;

    void RegisterNetworkCallback();
    // false quando o evento não coube na fila (ou não há fila).
    bool PostEvent(PvEventType type, const std::string& data, PvConfigReason reason);
    void HandleEvent(const PvEvent& event);

    void HandleNetworkConnected(const char* ssid);
    void HandleWifiConfigMode();
    void HandlePendingSave();
    void ShowConfigScreen(PvConfigReason reason);
    // Carrega a tela de status garantindo que a tela de câmera saia antes.
    void LoadStatusScreen();

    // Câmera (F2). O handler roda NA TASK DA CÂMERA e só sinaliza.
    void OnCameraEventFromCameraTask(PvCamera::Event event);
    // Roda NA TASK DO LVGL (toque nos botões da barra): só posta na fila.
    void OnCameraActionFromLvglTask(PvCameraScreen::Action action);
    void ShowCameraScreen();
    // Captura e revisão (T4). Tudo aqui roda na task do PvApp.
    void HandleCaptureRequested();
    // false quando não havia captura a retirar (aviso duplicado, ou a captura
    // na verdade falhou) — é o que a reconciliação usa para decidir.
    bool HandleJpegReady();
    void HandleJpegFailed();
    // REDE DE SEGURANÇA para os avisos de conclusão da câmera e do dump: os
    // eventos continuam sendo o caminho rápido, mas a fila é curta e um evento
    // que não coube deixaria a tela presa em "Processando..."/"Exportando..."
    // (revisão F2, P1). Roda no timeout do laço principal.
    void ReconcileCameraState();
    void HandleRetakeRequested();
    void HandleExportRequested();  // PROVISÓRIO DA F2
    // Ponto ÚNICO de saída da tela de câmera: desliga o preview e faz a tela
    // devolver o empréstimo. Todo caminho que carrega outra tela passa por
    // aqui — inclusive os que não vêm do botão "Voltar".
    void LeaveCameraScreen();
    // LeaveCameraScreen + volta para a tela que estava por baixo.
    void ReturnFromCameraScreen();

    // Hidratação (§9.1) e monitoramento de conectividade (§9.7). Tudo aqui
    // roda na task principal; só as chamadas HTTP vão para o PvWorker.
    void StartHydration();
    void HandleHydrationDone();
    void HandleHealthTick();
    void HandleHealthDone();
    void ShowBackendError(const char* message);
    void SetBackendHealthy(bool healthy);
    void StartHealthTimer();
    void StopHealthTimer();

    // Chamado na task do LVGL pelo botão Salvar: só copia e sinaliza.
    void OnSaveRequestedFromLvglTask(std::string url, std::string token);

    QueueHandle_t event_queue_ = nullptr;

    PvStatusScreen status_screen_;
    PvConfigScreen config_screen_;
    PvRouteScreens route_screens_;
    PvCameraScreen camera_screen_;

    PvWorker worker_;
    PvCamera camera_;

    // Coalescência do aviso de frame de preview: no máximo UM evento
    // CameraPreviewFrame pendente na fila. Setada na task da câmera ao postar,
    // limpada na task principal ao tratar. Sem isso, 5 fps encheriam a fila de
    // 8 slots e os eventos de rede começariam a ser descartados.
    std::atomic<bool> preview_frame_pending_{false};
    esp_timer_handle_t health_timer_ = nullptr;

    // Espelho corrente da sessão e da lição. Só é substituído por uma
    // hidratação bem-sucedida: rota nenhuma é decidida com dado velho.
    PvSessionState session_state_;
    PvLesson lesson_;
    bool hydrated_ = false;
    bool backend_healthy_ = false;

    PvPhase phase_ = PvPhase::Booting;
    bool network_connected_ = false;
    std::string network_name_;

    // Geração de conectividade: incrementada a cada fronteira (desconexão,
    // entrada em config mode Wi-Fi). Pedidos ao worker carregam a geração
    // corrente e resultados de geração anterior são descartados — um HTTP que
    // estava em voo durante a desconexão não pode "reviver" o estado Online
    // com dado velho (revisão F1, P1). Escrita/leitura só na task principal.
    uint32_t net_generation_ = 0;

    // Configuração pendente de gravação, entregue pela task do LVGL.
    std::mutex pending_mutex_;
    bool pending_save_valid_ = false;
    std::string pending_url_;
    std::string pending_token_;
};

#endif  // PV_APP_H
