#ifndef PV_CONFIG_GESTURE_H
#define PV_CONFIG_GESTURE_H

#include <lvgl.h>

namespace PvUi {

// Porta de recuperação manual da tela de configuração (decisão F1-ConfigGesture):
// long-press de 3 s no alvo abre a tela de configuração do backend com
// PvConfigReason::Manual. O limiar de 3 s é medido localmente (lv_tick), sem
// mexer no long_press_time global do dispositivo de entrada. Dispara uma única
// vez por pressão e cancela ao soltar ou perder o toque. A F6 NÃO deve remover
// este gesto: se a URL estiver errada, o PIN adulto via backend é inalcançável
// e o gesto é o único caminho para corrigir (exceção registrada).
void AttachConfigGesture(lv_obj_t* target);

}  // namespace PvUi

#endif  // PV_CONFIG_GESTURE_H
