#ifndef PV_UI_PV_CONFIG_SCREEN_H
#define PV_UI_PV_CONFIG_SCREEN_H

#include <lvgl.h>

#include <cstdint>
#include <functional>
#include <string>

// Motivo pelo qual a tela de configuração foi aberta. É um enum fechado de
// propósito: quem pede a tela (T5, em 401/503) não escolhe texto livre, então
// não há caminho para vazar token ou detalhe do servidor para a UI.
enum class PvConfigReason : uint8_t {
    MissingConfig,  // backend_url e/ou api_token ausentes no NVS "pv"
    Unauthorized,   // 401 do backend
    Unavailable,    // 503 / servidor inalcançável
    Manual,         // pedido explícito do adulto
};

// Tela de configuração do backend do Professor Virtual: endereço do servidor,
// token do dispositivo, teclado LVGL e botão Salvar.
//
// Regras rígidas do token (decisão F1-Token 2a):
//  - o campo é password mode com show time 0 (o default do LVGL revela o
//    último caractere por 1500 ms);
//  - o campo NUNCA é pré-preenchido a partir do NVS;
//  - salvar com o campo vazio preserva o token já gravado;
//  - o token não aparece em log, título, mensagem de erro ou toast.
//
// Show()/Hide()/SetMessage() devem ser chamados na task do PvApp e pegam o
// DisplayLockGuard internamente. O SaveHandler, ao contrário, é invocado na
// task do LVGL: ele deve apenas copiar os textos e sinalizar o PvApp.
class PvConfigScreen {
public:
    using SaveHandler = std::function<void(std::string url, std::string token)>;

    PvConfigScreen() = default;
    PvConfigScreen(const PvConfigScreen&) = delete;
    PvConfigScreen& operator=(const PvConfigScreen&) = delete;

    void SetSaveHandler(SaveHandler handler);

    // Constrói e carrega a tela. Se já estiver visível, apenas troca a
    // mensagem — sem recriar (não perde o que o adulto já digitou na URL).
    void Show(PvConfigReason reason);

    // Volta para a tela anterior e destrói esta (junto com o conteúdo dos
    // campos, inclusive o token digitado).
    void Hide();

    bool IsVisible() const { return screen_ != nullptr; }

    void SetMessage(const char* text, uint32_t color);

private:
    static void OnTextareaClicked(lv_event_t* e);
    static void OnSaveClicked(lv_event_t* e);
    static void OnKeyboardReady(lv_event_t* e);

    void FocusTextarea(lv_obj_t* textarea);
    // Roda na task do LVGL: valida, limpa o campo do token e chama o handler.
    void SubmitFromLvglTask();

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* previous_screen_ = nullptr;
    lv_obj_t* message_label_ = nullptr;
    lv_obj_t* url_textarea_ = nullptr;
    lv_obj_t* token_textarea_ = nullptr;
    lv_obj_t* keyboard_ = nullptr;

    // Lido do NVS em Show() (task do PvApp) para que o callback do LVGL não
    // precise tocar a flash: diz se já existe token gravado, o que muda o
    // placeholder e permite salvar só a URL.
    bool token_already_saved_ = false;

    SaveHandler save_handler_;
};

#endif  // PV_UI_PV_CONFIG_SCREEN_H
