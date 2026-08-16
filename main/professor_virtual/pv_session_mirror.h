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

// Veredicto pedagógico do turno (§7.5). Lista FECHADA: o backend só emite
// estes quatro valores e cada um tem tratamento próprio na tela (som de
// acerto, explicação, pedido de nova foto). Um valor desconhecido não pode
// virar "algo parecido" — vira rejeição da resposta inteira.
enum class PvVerdict {
    Correct,
    Wrong,
    Teach,
    Unidentifiable,
};

// Espelho da resposta 200 de POST /api/turn no perfil v1.1 (§7.5 + "Campos
// novos na resposta"). Só é preenchido quando ParseTurnResponse devolve true.
struct PvTurnResponse {
    // Default conservador: um objeto ainda não preenchido não pode parecer
    // "acertou". "unidentifiable" é o veredicto que pede nova foto.
    PvVerdict veredicto = PvVerdict::Unidentifiable;
    std::string texto_explicacao;  // fala do tutor; pode ser vazia
    std::string session_status;    // "active" | "completed" (lista fechada)
    std::string current_item;      // posição APÓS o turno (§9.4)
    std::string current_tarefa;    // idem
    int wrong_answer_count = 0;
    bool adult_intervention_required = false;
    // Caminhos RELATIVOS à base do backend, sempre sob "/api/media/";
    // resolvidos por PvBackendClient::ResolveUrl. Nunca absolutos: o parser
    // rejeita a resposta inteira se forem (ver ParseTurnResponse).
    std::string audio_url;
    std::string image_url;
    std::string request_id;  // eco do id enviado; a COMPARAÇÃO é do chamador

    void Clear();
};

// Parser ESTRITO da resposta do turno (decisão F3-D1). Ao contrário dos
// parsers tolerantes da hidratação, aqui QUALQUER desvio do contrato v1.1
// devolve false e deixa `out` limpo — nenhum campo desta resposta é
// decorativo: veredicto e posição movem a máquina de estados, as URLs são a
// única fonte da mídia e o eco do request_id é a prova de idempotência.
// Exigências: veredicto e session_status nas listas fechadas; audio_url e
// image_url começando EXATAMENTE por "/api/media/" e com nome de arquivo
// depois do prefixo — URL absoluta (http/https), relativa a protocolo
// ("//host/...") ou fora dessa rota derruba a resposta, porque o download leva
// o token do dispositivo no cabeçalho e ele só pode ir para a origem que o
// adulto configurou; audio_format == "wav"; image_format == "jpg";
// audio_base64 e image_base64 presentes e EXATAMENTE ""; request_id presente e
// não vazio; current_item/current_tarefa, texto_explicacao presentes como
// string (vazia é aceita); wrong_answer_count número INTEIRO dentro da faixa
// de int (1.5 e 1e12 são rejeitados); adult_intervention_required booleano.
// Campos extras desconhecidos são tolerados (o contrato é aditivo).
bool ParseTurnResponse(const char* json, PvTurnResponse& out);

// Nome estável do veredicto, só para log/diagnóstico.
const char* PvVerdictName(PvVerdict verdict);

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
