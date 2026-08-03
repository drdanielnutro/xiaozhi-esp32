#ifndef PV_ROUTE_TEXT_H
#define PV_ROUTE_TEXT_H

#include <string>

#include "pv_session_mirror.h"

// Textos das telas placeholder de rota (§9.1), separados do LVGL de propósito:
// este módulo é PURO (só C++ padrão + pv_session_mirror/pv_strings), então a
// tradução "espelho hidratado -> texto na tela" é compilável e testável no
// host junto com os parsers (ver host_test/run.sh).

// Título e subtítulo fixos de cada um dos quatro destinos do §9.1.
const char* PvRouteTitle(PvRoute route);
const char* PvRouteSubtitle(PvRoute route);

// Bloco de detalhe da tela de tutoria: item/tarefa correntes vindos de
// GET /api/state mais o título do item e o enunciado da tarefa vindos de
// GET /api/lesson. Campos ausentes no espelho viram um marcador pt-BR; nada
// aqui inventa dado nem depende de a lição conter a posição corrente.
// `max_statement_chars` corta o enunciado (0 = sem corte) para caber na tela.
std::string PvBuildTutoringDetail(const PvSessionState& state, const PvLesson& lesson,
                                  size_t max_statement_chars = 160);

#endif  // PV_ROUTE_TEXT_H
