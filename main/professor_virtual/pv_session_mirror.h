#ifndef PV_SESSION_MIRROR_H
#define PV_SESSION_MIRROR_H

#include <string>
#include <utility>
#include <vector>

// Espelho em RAM do estado da sessão (§7.2) e da lição (§7.3) do backend, mais
// a decisão de rota de boot (§9.1).
//
// Este módulo é PURO: só C++ padrão no cabeçalho e apenas <cJSON.h> na
// implementação. Nenhum header do ESP-IDF entra aqui, para que os parsers e a
// rota possam ser compilados e testados no host (ver host_test/run.sh).
//
// Os mapas do contrato (item_progress, tarefas, items) viram vetores de pares
// para PRESERVAR A ORDEM de iteração do JSON: a detecção de avanço da F5
// compara posições (item, tarefa), e um std::map ordenaria por nome, o que
// quebraria a ordem pedagógica quando os identificadores não forem
// lexicográficos.

// Qual dos três formatos de GET /api/state chegou.
enum class PvStateKind {
    NoSession,     // {"status": "no_session"}
    InvalidState,  // {"status": "invalid_state", "error": "..."}
    Session,       // objeto de sessão completo (sem campo "status" de topo)
};

// Progresso de uma tarefa dentro de item_progress.
struct PvTaskProgress {
    std::string status;  // "pending" | "completed"
    // image_uri e completion_source são anuláveis no contrato; null vira "".
    std::string image_uri;
    std::string completion_source;
    int wrong_answer_count = 0;
    int technical_failure_count = 0;
    int clarification_count = 0;
};

// Progresso de um item dentro de item_progress.
struct PvItemProgress {
    std::string status;  // "pending" | "in_progress" | "completed"
    std::vector<std::pair<std::string, PvTaskProgress>> tarefas;

    // -1 quando a tarefa não existe no item.
    int IndexOfTarefa(const std::string& tarefa_id) const;
    const PvTaskProgress* FindTarefa(const std::string& tarefa_id) const;
};

// Contadores de uso do backend (usage_counters).
struct PvUsageCounters {
    int llm_calls = 0;
    int image_generation_calls = 0;
    int tts_chars_used = 0;

    // Sessão sem nenhum turno ainda (usado pela regra de revisão do §9.1).
    bool IsZero() const;
};

// Posição corrente (item, tarefa) já resolvida em índices; -1 significa
// "não encontrado no espelho". Serve à detecção de avanço da F5.
struct PvPosition {
    int item_index = -1;
    int tarefa_index = -1;

    bool operator==(const PvPosition& other) const;
    bool operator!=(const PvPosition& other) const;
    // Ordem linear (item, tarefa). Posições desconhecidas (-1) ficam antes.
    bool operator<(const PvPosition& other) const;
};

// Espelho de GET /api/state.
struct PvSessionState {
    PvStateKind kind = PvStateKind::NoSession;
    std::string error;  // preenchido só em InvalidState

    std::string session_id;
    std::string student_id;
    std::string student_name;
    std::string lesson_id;
    std::string session_status;  // "active" | "completed" | "closed" | "expired"
    bool waiting_for_photo = false;
    bool adult_intervention_required = false;
    std::string created_at;
    std::string updated_at;
    std::string expires_at;
    std::string current_item;
    std::string current_tarefa;
    std::vector<std::pair<std::string, PvItemProgress>> item_progress;
    PvUsageCounters usage_counters;

    void Clear();

    // -1 quando o item não existe no espelho.
    int IndexOfItem(const std::string& item_id) const;
    const PvItemProgress* FindItem(const std::string& item_id) const;
    // Posição de current_item/current_tarefa dentro de item_progress.
    PvPosition CurrentPosition() const;
    // Nenhum turno aplicado ainda: contadores zerados e todos os itens
    // "pending" (regra de reexibição da revisão, §9.1).
    bool IsUntouched() const;
};

// Uma tarefa da lição (§7.3).
struct PvLessonTask {
    std::string enunciado;
    std::string origem;      // anulável no contrato; null vira ""
    std::string disciplina;  // anulável no contrato; null vira ""
    int pagina = 0;
    bool has_pagina = false;
};

// Um item da lição (§7.3).
struct PvLessonItem {
    std::string titulo;
    std::string enunciado;
    std::vector<std::pair<std::string, PvLessonTask>> tarefas;

    int IndexOfTarefa(const std::string& tarefa_id) const;
    const PvLessonTask* FindTarefa(const std::string& tarefa_id) const;
};

// Espelho de GET /api/lesson.
struct PvLesson {
    // A resposta de sucesso NÃO tem campo "status"; a presença de "lesson_id"
    // é o que indica que existe lição.
    bool has_lesson = false;
    std::string lesson_id;
    std::string created_at;
    std::vector<std::string> source_pages;
    std::vector<std::pair<std::string, PvLessonItem>> items;

    void Clear();

    int IndexOfItem(const std::string& item_id) const;
    const PvLessonItem* FindItem(const std::string& item_id) const;
};

// Parsers. Retornam false para JSON inválido ou estruturalmente impossível
// (raiz que não é objeto, formato irreconhecível). Campos ausentes ou de tipo
// inesperado dentro de um formato reconhecido são tolerados e viram defaults —
// nada vindo da rede é assumido presente. Em caso de false, `out` fica limpo.
bool ParseState(const char* json, PvSessionState& out);
bool ParseLesson(const char* json, PvLesson& out);

// Destinos de boot do §9.1.
enum class PvRoute {
    Preparation,
    Celebration,
    Failsafe,
    Tutoring,
};

// "Sessão utilizável": kind == Session e session_status na lista FECHADA
// {"active", "completed"} — os dois únicos estados com rota própria no §9.1
// ("completed" é a condição da tela de celebração, avaliada logo depois).
// Qualquer status fora do contrato cai na preparação, nunca em tela
// pedagógica (revisão F1, P1).
bool IsSessionUsable(const PvSessionState& state);

// Rota de boot, na ORDEM EXATA do §9.1:
//   1. sem sessão utilizável OU sem lição -> Preparation;
//   2. session_status == "completed"      -> Celebration;
//   3. adult_intervention_required        -> Failsafe;
//   4. senão                              -> Tutoring.
PvRoute DecideRoute(const PvSessionState& state, const PvLesson& lesson);

// Nome estável da rota, só para log/diagnóstico.
const char* PvRouteName(PvRoute route);

#endif  // PV_SESSION_MIRROR_H
