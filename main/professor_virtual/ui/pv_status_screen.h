#ifndef PV_UI_PV_STATUS_SCREEN_H
#define PV_UI_PV_STATUS_SCREEN_H

#include <lvgl.h>

#include "ui/pv_ui_theme.h"

// Tela de status do Professor Virtual: marca, mensagem principal, bloco de
// detalhe (usado pelas instruções do modo de configuração Wi-Fi) e versão do
// firmware. É a tela de boot da F0 evoluída para cobrir todo o ciclo de rede.
//
// Todos os métodos devem ser chamados na task do PvApp e pegam o
// DisplayLockGuard internamente. Em placa sem display viram no-op.
class PvStatusScreen {
public:
    PvStatusScreen() = default;
    PvStatusScreen(const PvStatusScreen&) = delete;
    PvStatusScreen& operator=(const PvStatusScreen&) = delete;

    // Constrói os objetos LVGL e carrega a tela. Idempotente.
    void Create();

    // Traz a tela de volta para o display (usado ao fechar a tela de config).
    void Load();

    bool IsCreated() const { return screen_ != nullptr; }
    lv_obj_t* screen() const { return screen_; }

    // Mensagem principal (uma linha curta).
    void SetStatus(const char* text);

    // Bloco de detalhe multilinha; nullptr ou "" esconde o bloco.
    void SetDetail(const char* text);

    // Cor da mensagem principal (paleta em PvUi).
    void SetStatusColor(uint32_t color);

    // Indicador de conexão com o backend (§9.7).
    void SetConnected(bool connected);

private:
    lv_obj_t* screen_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* detail_label_ = nullptr;
    PvUi::ConnectionBadge badge_;
};

#endif  // PV_UI_PV_STATUS_SCREEN_H
