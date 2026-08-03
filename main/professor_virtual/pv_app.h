#ifndef PV_APP_H
#define PV_APP_H

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <cstdint>
#include <mutex>
#include <string>

#include "pv_session_mirror.h"
#include "pv_worker.h"
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
    void PostEvent(PvEventType type, const std::string& data, PvConfigReason reason);
    void HandleEvent(const PvEvent& event);

    void HandleNetworkConnected(const char* ssid);
    void HandleWifiConfigMode();
    void HandlePendingSave();
    void ShowConfigScreen(PvConfigReason reason);

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

    PvWorker worker_;
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
