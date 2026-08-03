#ifndef PV_APP_H
#define PV_APP_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <cstdint>
#include <mutex>
#include <string>

#include "ui/pv_config_screen.h"
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
    void ShowReadyStatus();

    // Chamado na task do LVGL pelo botão Salvar: só copia e sinaliza.
    void OnSaveRequestedFromLvglTask(std::string url, std::string token);

    QueueHandle_t event_queue_ = nullptr;

    PvStatusScreen status_screen_;
    PvConfigScreen config_screen_;

    PvPhase phase_ = PvPhase::Booting;
    bool network_connected_ = false;
    std::string network_name_;

    // Configuração pendente de gravação, entregue pela task do LVGL.
    std::mutex pending_mutex_;
    bool pending_save_valid_ = false;
    std::string pending_url_;
    std::string pending_token_;
};

#endif  // PV_APP_H
