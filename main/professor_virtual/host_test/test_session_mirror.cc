// Teste de host do pv_session_mirror: parsers do §7.2/§7.3 e decisão de rota
// do §9.1, validados contra os JSONs literais do contrato.
//
// Compila e roda fora do ESP-IDF (ver run.sh). Este diretório fica FORA do
// GLOB do build de firmware (main/CMakeLists.txt varre apenas
// professor_virtual/*.cc e professor_virtual/ui/*.cc) — intencional.

#include <cstdio>
#include <string>

#include "pv_route_text.h"
#include "pv_session_mirror.h"
#include "pv_strings.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const char* what) {
    g_checks++;
    if (!condition) {
        g_failures++;
        std::printf("  FALHA: %s\n", what);
    }
}

void CheckEq(const std::string& actual, const std::string& expected, const char* what) {
    g_checks++;
    if (actual != expected) {
        g_failures++;
        std::printf("  FALHA: %s (esperado \"%s\", obtido \"%s\")\n", what, expected.c_str(),
                    actual.c_str());
    }
}

void CheckEqInt(int actual, int expected, const char* what) {
    g_checks++;
    if (actual != expected) {
        g_failures++;
        std::printf("  FALHA: %s (esperado %d, obtido %d)\n", what, expected, actual);
    }
}

void CheckRoute(PvRoute actual, PvRoute expected, const char* what) {
    g_checks++;
    if (actual != expected) {
        g_failures++;
        std::printf("  FALHA: %s (esperado %s, obtido %s)\n", what, PvRouteName(expected),
                    PvRouteName(actual));
    }
}

// JSON literal do §7.2 (sessão existente), com dois itens/duas tarefas para
// exercitar a ordem de iteração.
const char* kStateActive = R"({
  "session_id": "11111111-2222-3333-4444-555555555555",
  "student_id": "default",
  "student_name": "Aluno",
  "lesson_id": "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
  "session_status": "active",
  "waiting_for_photo": false,
  "adult_intervention_required": false,
  "created_at": "2026-08-03T10:00:00Z",
  "updated_at": "2026-08-03T10:05:00Z",
  "expires_at": "2026-08-03T14:00:00Z",
  "current_item": "item_2",
  "current_tarefa": "item_2_task_1",
  "item_progress": {
    "item_1": {
      "status": "completed",
      "tarefas": {
        "item_1_task_1": {
          "status": "completed",
          "image_uri": "data/images/page_1.jpg",
          "wrong_answer_count": 2,
          "technical_failure_count": 1,
          "clarification_count": 3,
          "completion_source": "correct"
        }
      }
    },
    "item_2": {
      "status": "in_progress",
      "tarefas": {
        "item_2_task_1": {
          "status": "pending",
          "image_uri": null,
          "wrong_answer_count": 0,
          "technical_failure_count": 0,
          "clarification_count": 0,
          "completion_source": null
        },
        "item_2_task_2": {
          "status": "pending",
          "image_uri": null,
          "wrong_answer_count": 0,
          "technical_failure_count": 0,
          "clarification_count": 0,
          "completion_source": null
        }
      }
    }
  },
  "usage_counters": { "llm_calls": 7, "image_generation_calls": 2, "tts_chars_used": 1234 }
})";

// JSON literal do §7.3 (lição completa, SEM campo "status").
const char* kLessonFull = R"({
  "lesson_id": "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
  "created_at": "2026-08-03T10:00:00Z",
  "source_pages": ["data/images/page_1.jpg", "data/images/page_2.jpg"],
  "items": {
    "item_1": {
      "titulo": "Exercício 1",
      "enunciado": "texto do item",
      "tarefas": {
        "item_1_task_1": {
          "enunciado": "texto da pergunta",
          "origem": "livro X",
          "disciplina": "matemática",
          "pagina": 12
        }
      }
    },
    "item_2": {
      "titulo": "Exercício 2",
      "enunciado": "segundo item",
      "tarefas": {
        "item_2_task_1": {
          "enunciado": "primeira pergunta",
          "origem": null,
          "disciplina": null,
          "pagina": 13
        },
        "item_2_task_2": {
          "enunciado": "segunda pergunta",
          "origem": null,
          "disciplina": null,
          "pagina": 13
        }
      }
    }
  }
})";

// Copia o estado ativo trocando um campo textual (helper simples de fixture).
std::string WithReplacement(const std::string& source, const std::string& from,
                            const std::string& to) {
    std::string out = source;
    size_t pos = out.find(from);
    if (pos != std::string::npos) {
        out.replace(pos, from.size(), to);
    }
    return out;
}

void TestStateActive() {
    std::printf("caso: state de sessão ativa (§7.2)\n");
    PvSessionState state;
    Check(ParseState(kStateActive, state), "ParseState do §7.2 retorna true");
    Check(state.kind == PvStateKind::Session, "kind == Session");
    CheckEq(state.session_id, "11111111-2222-3333-4444-555555555555", "session_id");
    CheckEq(state.student_id, "default", "student_id");
    CheckEq(state.student_name, "Aluno", "student_name");
    CheckEq(state.lesson_id, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee", "lesson_id");
    CheckEq(state.session_status, "active", "session_status");
    Check(!state.waiting_for_photo, "waiting_for_photo false");
    Check(!state.adult_intervention_required, "adult_intervention_required false");
    CheckEq(state.created_at, "2026-08-03T10:00:00Z", "created_at");
    CheckEq(state.updated_at, "2026-08-03T10:05:00Z", "updated_at");
    CheckEq(state.expires_at, "2026-08-03T14:00:00Z", "expires_at");
    CheckEq(state.current_item, "item_2", "current_item");
    CheckEq(state.current_tarefa, "item_2_task_1", "current_tarefa");
    CheckEq(state.error, "", "error vazio em sessão");

    // Ordem de iteração preservada (comparação de posições da F5).
    CheckEqInt(static_cast<int>(state.item_progress.size()), 2, "dois itens em item_progress");
    CheckEq(state.item_progress[0].first, "item_1", "primeiro item na ordem do JSON");
    CheckEq(state.item_progress[1].first, "item_2", "segundo item na ordem do JSON");
    CheckEq(state.item_progress[0].second.status, "completed", "status do item_1");
    CheckEq(state.item_progress[1].second.status, "in_progress", "status do item_2");

    const PvItemProgress* item1 = state.FindItem("item_1");
    Check(item1 != nullptr, "FindItem(item_1)");
    const PvTaskProgress* task1 = item1 != nullptr ? item1->FindTarefa("item_1_task_1") : nullptr;
    Check(task1 != nullptr, "FindTarefa(item_1_task_1)");
    if (task1 != nullptr) {
        CheckEq(task1->status, "completed", "status da tarefa concluída");
        CheckEq(task1->image_uri, "data/images/page_1.jpg", "image_uri");
        CheckEq(task1->completion_source, "correct", "completion_source");
        CheckEqInt(task1->wrong_answer_count, 2, "wrong_answer_count");
        CheckEqInt(task1->technical_failure_count, 1, "technical_failure_count");
        CheckEqInt(task1->clarification_count, 3, "clarification_count");
    }

    const PvItemProgress* item2 = state.FindItem("item_2");
    Check(item2 != nullptr, "FindItem(item_2)");
    const PvTaskProgress* task2 = item2 != nullptr ? item2->FindTarefa("item_2_task_1") : nullptr;
    Check(task2 != nullptr, "FindTarefa(item_2_task_1)");
    if (task2 != nullptr) {
        CheckEq(task2->image_uri, "", "image_uri null vira vazio");
        CheckEq(task2->completion_source, "", "completion_source null vira vazio");
        CheckEqInt(task2->wrong_answer_count, 0, "wrong_answer_count zerado");
    }
    if (item2 != nullptr) {
        CheckEqInt(item2->IndexOfTarefa("item_2_task_2"), 1, "ordem das tarefas preservada");
        CheckEqInt(item2->IndexOfTarefa("inexistente"), -1, "tarefa ausente devolve -1");
    }

    CheckEqInt(state.usage_counters.llm_calls, 7, "llm_calls");
    CheckEqInt(state.usage_counters.image_generation_calls, 2, "image_generation_calls");
    CheckEqInt(state.usage_counters.tts_chars_used, 1234, "tts_chars_used");
    Check(!state.usage_counters.IsZero(), "usage_counters não zerados");
    Check(!state.IsUntouched(), "sessão já teve turno");

    PvPosition position = state.CurrentPosition();
    CheckEqInt(position.item_index, 1, "posição corrente: índice do item");
    CheckEqInt(position.tarefa_index, 0, "posição corrente: índice da tarefa");

    PvLesson lesson;
    Check(ParseLesson(kLessonFull, lesson), "ParseLesson do §7.3 retorna true");
    Check(IsSessionUsable(state), "sessão ativa é utilizável");
    CheckRoute(DecideRoute(state, lesson), PvRoute::Tutoring, "ativa + lição -> tutoria");
}

void TestStateNoSession() {
    std::printf("caso: no_session e invalid_state\n");
    PvLesson lesson;
    Check(ParseLesson(kLessonFull, lesson), "lição presente para o teste");

    PvSessionState no_session;
    Check(ParseState(R"({"status": "no_session"})", no_session), "ParseState(no_session)");
    Check(no_session.kind == PvStateKind::NoSession, "kind == NoSession");
    Check(!IsSessionUsable(no_session), "no_session não é utilizável");
    CheckRoute(DecideRoute(no_session, lesson), PvRoute::Preparation, "no_session -> preparação");

    PvSessionState invalid;
    Check(ParseState(R"({"status": "invalid_state", "error": "state.json corrompido"})", invalid),
          "ParseState(invalid_state)");
    Check(invalid.kind == PvStateKind::InvalidState, "kind == InvalidState");
    CheckEq(invalid.error, "state.json corrompido", "mensagem de erro preservada");
    CheckRoute(DecideRoute(invalid, lesson), PvRoute::Preparation, "invalid_state -> preparação");
}

void TestRouteCompleted() {
    std::printf("caso: session_status completed -> celebração\n");
    PvLesson lesson;
    ParseLesson(kLessonFull, lesson);

    std::string json = WithReplacement(kStateActive, "\"session_status\": \"active\"",
                                       "\"session_status\": \"completed\"");
    PvSessionState state;
    Check(ParseState(json.c_str(), state), "ParseState(completed)");
    CheckEq(state.session_status, "completed", "session_status completed");
    Check(IsSessionUsable(state), "completed continua utilizável (vai à celebração)");
    CheckRoute(DecideRoute(state, lesson), PvRoute::Celebration, "completed -> celebração");

    // Celebração vence failsafe: a ordem do §9.1 é completed antes de adulto.
    std::string with_adult = WithReplacement(json, "\"adult_intervention_required\": false",
                                             "\"adult_intervention_required\": true");
    PvSessionState both;
    Check(ParseState(with_adult.c_str(), both), "ParseState(completed + adulto)");
    CheckRoute(DecideRoute(both, lesson), PvRoute::Celebration,
               "completed vence adult_intervention_required");
}

void TestRouteFailsafe() {
    std::printf("caso: adult_intervention_required -> failsafe\n");
    PvLesson lesson;
    ParseLesson(kLessonFull, lesson);

    std::string json = WithReplacement(kStateActive, "\"adult_intervention_required\": false",
                                       "\"adult_intervention_required\": true");
    PvSessionState state;
    Check(ParseState(json.c_str(), state), "ParseState(adulto)");
    Check(state.adult_intervention_required, "adult_intervention_required true");
    CheckEq(state.session_status, "active", "sessão continua ativa");
    CheckRoute(DecideRoute(state, lesson), PvRoute::Failsafe, "active + adulto -> failsafe");
}

void TestRouteClosedExpired() {
    std::printf("caso: closed e expired -> preparação\n");
    PvLesson lesson;
    ParseLesson(kLessonFull, lesson);

    std::string closed_json = WithReplacement(kStateActive, "\"session_status\": \"active\"",
                                              "\"session_status\": \"closed\"");
    PvSessionState closed;
    Check(ParseState(closed_json.c_str(), closed), "ParseState(closed)");
    Check(!IsSessionUsable(closed), "closed não é utilizável");
    CheckRoute(DecideRoute(closed, lesson), PvRoute::Preparation, "closed -> preparação");

    std::string expired_json = WithReplacement(kStateActive, "\"session_status\": \"active\"",
                                               "\"session_status\": \"expired\"");
    PvSessionState expired;
    Check(ParseState(expired_json.c_str(), expired), "ParseState(expired)");
    Check(!IsSessionUsable(expired), "expired não é utilizável");
    CheckRoute(DecideRoute(expired, lesson), PvRoute::Preparation, "expired -> preparação");

    // Failsafe não escapa de sessão inutilizável.
    std::string expired_adult =
        WithReplacement(expired_json, "\"adult_intervention_required\": false",
                        "\"adult_intervention_required\": true");
    PvSessionState expired_with_adult;
    Check(ParseState(expired_adult.c_str(), expired_with_adult), "ParseState(expired + adulto)");
    CheckRoute(DecideRoute(expired_with_adult, lesson), PvRoute::Preparation,
               "expired vence adult_intervention_required");
}

void TestLesson() {
    std::printf("caso: lição completa (§7.3) e no_lesson\n");
    PvLesson lesson;
    Check(ParseLesson(kLessonFull, lesson), "ParseLesson(lição completa)");
    // Sucesso da lição NÃO tem campo "status": detecção pela presença de lesson_id.
    Check(std::string(kLessonFull).find("\"status\"") == std::string::npos,
          "fixture da lição não tem campo status");
    Check(lesson.has_lesson, "has_lesson true por presença de lesson_id");
    CheckEq(lesson.lesson_id, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee", "lesson_id");
    CheckEq(lesson.created_at, "2026-08-03T10:00:00Z", "created_at");
    CheckEqInt(static_cast<int>(lesson.source_pages.size()), 2, "duas source_pages");
    CheckEq(lesson.source_pages[0], "data/images/page_1.jpg", "primeira source_page");
    CheckEqInt(static_cast<int>(lesson.items.size()), 2, "dois itens");
    CheckEq(lesson.items[0].first, "item_1", "ordem dos itens preservada");
    CheckEq(lesson.items[1].first, "item_2", "ordem dos itens preservada (2)");

    const PvLessonItem* item1 = lesson.FindItem("item_1");
    Check(item1 != nullptr, "FindItem(item_1) da lição");
    if (item1 != nullptr) {
        CheckEq(item1->titulo, "Exercício 1", "titulo do item");
        CheckEq(item1->enunciado, "texto do item", "enunciado do item");
        CheckEqInt(static_cast<int>(item1->tarefas.size()), 1, "uma tarefa no item_1");
        const PvLessonTask* task = item1->FindTarefa("item_1_task_1");
        Check(task != nullptr, "FindTarefa(item_1_task_1) da lição");
        if (task != nullptr) {
            CheckEq(task->enunciado, "texto da pergunta", "enunciado da tarefa");
            CheckEq(task->origem, "livro X", "origem");
            CheckEq(task->disciplina, "matemática", "disciplina com acento");
            Check(task->has_pagina, "pagina presente");
            CheckEqInt(task->pagina, 12, "pagina");
        }
    }
    const PvLessonItem* item2 = lesson.FindItem("item_2");
    Check(item2 != nullptr, "FindItem(item_2) da lição");
    if (item2 != nullptr) {
        CheckEqInt(static_cast<int>(item2->tarefas.size()), 2, "duas tarefas no item_2");
        CheckEq(item2->tarefas[0].first, "item_2_task_1", "ordem das tarefas da lição");
        const PvLessonTask* task = item2->FindTarefa("item_2_task_1");
        Check(task != nullptr, "FindTarefa(item_2_task_1) da lição");
        if (task != nullptr) {
            CheckEq(task->origem, "", "origem null vira vazio");
            CheckEq(task->disciplina, "", "disciplina null vira vazio");
        }
    }

    PvLesson none;
    Check(ParseLesson(R"({"status": "no_lesson"})", none), "ParseLesson(no_lesson)");
    Check(!none.has_lesson, "no_lesson -> has_lesson false");
    CheckEq(none.lesson_id, "", "no_lesson sem lesson_id");

    // Sessão ativa sem lição ainda vai para a preparação.
    PvSessionState state;
    Check(ParseState(kStateActive, state), "ParseState para o caso sem lição");
    CheckRoute(DecideRoute(state, none), PvRoute::Preparation,
               "sessão ativa sem lição -> preparação");
}

void TestMalformed() {
    std::printf("caso: corpos inválidos\n");
    PvSessionState state;
    Check(!ParseState("{ isso não é json", state), "state: JSON malformado -> false");
    Check(!ParseState("", state), "state: corpo vazio -> false");
    Check(!ParseState(nullptr, state), "state: ponteiro nulo -> false");
    Check(!ParseState("[1, 2, 3]", state), "state: raiz não-objeto -> false");
    Check(!ParseState(R"({"foo": "bar"})", state),
          "state: objeto sem status nem session_id -> false");
    Check(state.kind == PvStateKind::NoSession, "state fica limpo após falha");
    CheckRoute(DecideRoute(state, PvLesson()), PvRoute::Preparation,
               "estado limpo roteia para preparação");

    PvLesson lesson;
    Check(!ParseLesson("{ isso não é json", lesson), "lesson: JSON malformado -> false");
    Check(!ParseLesson("", lesson), "lesson: corpo vazio -> false");
    Check(!ParseLesson(nullptr, lesson), "lesson: ponteiro nulo -> false");
    Check(!ParseLesson(R"(["item_1"])", lesson), "lesson: raiz não-objeto -> false");
    Check(!ParseLesson(R"({"created_at": "2026-08-03T10:00:00Z"})", lesson),
          "lesson: objeto sem lesson_id nem status -> false");
    Check(!lesson.has_lesson, "lesson fica limpa após falha");

    // Robustez: campos ausentes/tipos errados dentro de um formato reconhecido
    // não invalidam o parse (cliente robusto), viram defaults.
    PvSessionState partial;
    Check(ParseState(R"({"session_id": "s1", "waiting_for_photo": "sim", "item_progress": 7})",
                     partial),
          "state mínimo com tipos errados ainda parseia");
    Check(partial.kind == PvStateKind::Session, "state mínimo é sessão");
    Check(!partial.waiting_for_photo, "tipo errado vira default");
    CheckEqInt(static_cast<int>(partial.item_progress.size()), 0,
               "item_progress inválido ignorado");
    PvPosition unknown = partial.CurrentPosition();
    CheckEqInt(unknown.item_index, -1, "posição desconhecida com item_progress vazio");
    Check(partial.IsUntouched(), "sessão sem contadores é intocada");
}

// Textos das telas de rota (pv_route_text): a tradução "espelho hidratado ->
// texto na tela" é justamente a prova de hidratação real que a tela de tutoria
// exibe, então ela é testada aqui junto com os parsers.
void TestRouteText() {
    std::printf("caso: textos das telas de rota (§9.1)\n");

    Check(std::string(PvRouteTitle(PvRoute::Preparation)) != PvRouteTitle(PvRoute::Tutoring),
          "preparação e tutoria têm títulos distintos");
    Check(std::string(PvRouteTitle(PvRoute::Celebration)) != PvRouteTitle(PvRoute::Failsafe),
          "celebração e failsafe têm títulos distintos");
    for (PvRoute route :
         {PvRoute::Preparation, PvRoute::Celebration, PvRoute::Failsafe, PvRoute::Tutoring}) {
        Check(PvRouteTitle(route) != nullptr && PvRouteTitle(route)[0] != '\0',
              "toda rota tem título");
        Check(PvRouteSubtitle(route) != nullptr && PvRouteSubtitle(route)[0] != '\0',
              "toda rota tem subtítulo");
    }

    PvSessionState state;
    PvLesson lesson;
    Check(ParseState(kStateActive, state), "ParseState para o detalhe da tutoria");
    Check(ParseLesson(kLessonFull, lesson), "ParseLesson para o detalhe da tutoria");

    std::string detail = PvBuildTutoringDetail(state, lesson);
    Check(detail.find("item_2") != std::string::npos, "detalhe mostra o current_item");
    Check(detail.find("item_2_task_1") != std::string::npos, "detalhe mostra o current_tarefa");
    Check(detail.find("Exercício 2") != std::string::npos, "detalhe mostra o título do item");
    Check(detail.find("primeira pergunta") != std::string::npos,
          "detalhe mostra o enunciado da tarefa corrente");
    Check(detail.find("segunda pergunta") == std::string::npos, "detalhe não mistura outra tarefa");

    // Espelho sem posição corrente: nada de string vazia solta na tela.
    PvSessionState empty;
    std::string placeholder = PvBuildTutoringDetail(empty, lesson);
    Check(placeholder.find(PvStrings::kRouteUnknownValue) != std::string::npos,
          "sem current_item o detalhe usa o marcador pt-BR");

    // Lição ausente (rota de preparação, mas a função não pode quebrar).
    std::string no_lesson = PvBuildTutoringDetail(state, PvLesson());
    Check(no_lesson.find("item_2") != std::string::npos,
          "sem lição o detalhe ainda mostra a posição");
    Check(no_lesson.find("Exercício") == std::string::npos, "sem lição não há título de item");

    // Corte do enunciado: nunca parte um caractere UTF-8 pelo meio.
    PvLesson accented;
    Check(ParseLesson(R"({"lesson_id": "l1", "items": {"item_1": {"titulo": "Título",
          "tarefas": {"t1": {"enunciado": "ãããããããããã"}}}}})",
                      accented),
          "ParseLesson do enunciado acentuado");
    PvSessionState at_item;
    Check(ParseState(R"({"session_id": "s1", "current_item": "item_1", "current_tarefa": "t1"})",
                     at_item),
          "ParseState apontando para o enunciado acentuado");
    // 10 caracteres "ã" = 20 bytes; cortar em 5 bytes cairia no meio do 3º.
    std::string cut = PvBuildTutoringDetail(at_item, accented, 5);
    Check(cut.find("ãã...") != std::string::npos, "corte recua para a fronteira UTF-8");
    Check(cut.find("ãããããã") == std::string::npos, "corte realmente encurtou o enunciado");
    std::string uncut = PvBuildTutoringDetail(at_item, accented, 0);
    Check(uncut.find("ãããããããããã") != std::string::npos, "max 0 desliga o corte");
}

}  // namespace

int main() {
    std::printf("pv_session_mirror — teste de host\n");
    TestStateActive();
    TestStateNoSession();
    TestRouteCompleted();
    TestRouteFailsafe();
    TestRouteClosedExpired();
    TestLesson();
    TestMalformed();
    TestRouteText();

    std::printf("\n%d verificações, %d falha(s)\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("FALHOU\n");
    return 1;
}
