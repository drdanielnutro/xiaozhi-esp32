#ifndef PV_UI_PV_ROUTE_SCREENS_H
#define PV_UI_PV_ROUTE_SCREENS_H

#include <lvgl.h>

#include "pv_session_mirror.h"
#include "ui/pv_ui_theme.h"

// As quatro telas placeholder de destino do roteamento de boot (§9.1):
// preparação, tutoria, celebração e failsafe. Nesta fase elas só existem e
// provam a hidratação — não há interação; os comandos chegam nas fases
// seguintes (F7 / F3-F5 / F5 / F6).
//
// Cada rota tem a SUA tela LVGL, criada sob demanda na primeira vez que é
// roteada e mantida viva depois disso: recarregar é barato e, principalmente,
// nenhum ponteiro de tela é invalidado enquanto a tela de configuração guarda
// a "tela anterior" para voltar (PvConfigScreen::Hide).
//
// Todos os métodos devem ser chamados na task do PvApp e pegam o
// DisplayLockGuard internamente. Em placa sem display viram no-op.
class PvRouteScreens {
public:
    PvRouteScreens() = default;
    PvRouteScreens(const PvRouteScreens&) = delete;
    PvRouteScreens& operator=(const PvRouteScreens&) = delete;

    // Preenche a tela da rota com o espelho recém-hidratado e a carrega.
    void Show(PvRoute route, const PvSessionState& state, const PvLesson& lesson);

    // Recarrega a tela da rota corrente sem recalcular o conteúdo. Usada
    // quando uma tela temporária (a câmera da F2) sai de cena e a criança
    // precisa voltar exatamente para onde estava. false quando nenhuma rota
    // foi carregada ainda — aí o chamador cai na tela de status.
    bool Reload();

    // Atualiza o indicador de conexão em TODAS as telas já criadas: quando o
    // health volta a falhar, a tela que estiver no ar precisa refletir isso.
    void SetConnected(bool connected);

    // true depois que alguma rota já foi carregada — o PvApp usa isso para não
    // roubar a tela da criança com a tela de status.
    bool IsLoaded() const { return loaded_; }
    PvRoute route() const { return route_; }

private:
    static constexpr int kRouteCount = 4;

    struct Entry {
        lv_obj_t* screen = nullptr;
        lv_obj_t* detail = nullptr;
        PvUi::ConnectionBadge badge;
    };

    // Cria a tela da rota se ainda não existir. Exige o lock do display.
    Entry& EnsureEntry(PvRoute route);

    Entry entries_[kRouteCount];
    PvRoute route_ = PvRoute::Preparation;
    bool loaded_ = false;
    bool connected_ = false;
};

#endif  // PV_UI_PV_ROUTE_SCREENS_H
