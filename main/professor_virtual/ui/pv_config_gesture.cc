#include "pv_config_gesture.h"

#include "../pv_app.h"

namespace {

constexpr uint32_t kHoldMs = 3000;

struct GestureState {
    uint32_t press_tick = 0;
    bool fired = false;
};

// Roda na task do LVGL: só mede o tempo de pressão e sinaliza o PvApp
// (RequestConfigScreen posta na fila; a task principal troca a tela).
void OnGestureEvent(lv_event_t* e) {
    auto* state = static_cast<GestureState*>(lv_event_get_user_data(e));
    switch (lv_event_get_code(e)) {
        case LV_EVENT_PRESSED:
            state->press_tick = lv_tick_get();
            state->fired = false;
            break;
        case LV_EVENT_PRESSING:
            if (!state->fired && lv_tick_elaps(state->press_tick) >= kHoldMs) {
                state->fired = true;
                PvApp::GetInstance().RequestConfigScreen(PvConfigReason::Manual);
            }
            break;
        case LV_EVENT_RELEASED:
        case LV_EVENT_PRESS_LOST:
            state->fired = false;
            break;
        default:
            break;
    }
}

}  // namespace

namespace PvUi {

void AttachConfigGesture(lv_obj_t* target) {
    lv_obj_add_flag(target, LV_OBJ_FLAG_CLICKABLE);
    // Uma tela do PV vive para sempre depois de criada; o estado do gesto
    // acompanha o alvo e nunca é liberado (de propósito, sem dono melhor).
    auto* state = new GestureState();
    lv_obj_add_event_cb(target, OnGestureEvent, LV_EVENT_ALL, state);
}

}  // namespace PvUi
