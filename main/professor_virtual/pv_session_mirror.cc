#include "pv_session_mirror.h"

#include <cJSON.h>

namespace {

// Leitores tolerantes: campo ausente ou de tipo inesperado vira default.
// Nada vindo da rede é assumido presente (validação de entrada obrigatória).
std::string GetString(const cJSON* object, const char* key) {
    const cJSON* item = cJSON_GetObjectItem(object, key);
    if (cJSON_IsString(item) && item->valuestring != nullptr) {
        return item->valuestring;
    }
    return "";
}

bool GetBool(const cJSON* object, const char* key, bool fallback = false) {
    const cJSON* item = cJSON_GetObjectItem(object, key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item) != 0;
    }
    return fallback;
}

int GetInt(const cJSON* object, const char* key, int fallback = 0) {
    const cJSON* item = cJSON_GetObjectItem(object, key);
    if (cJSON_IsNumber(item)) {
        return item->valueint;
    }
    return fallback;
}

// Nome da chave de um filho de objeto; protege contra `string` nulo.
std::string ChildKey(const cJSON* child) { return child->string != nullptr ? child->string : ""; }

void ParseTaskProgress(const cJSON* json, PvTaskProgress& out) {
    out.status = GetString(json, "status");
    out.image_uri = GetString(json, "image_uri");
    out.completion_source = GetString(json, "completion_source");
    out.wrong_answer_count = GetInt(json, "wrong_answer_count");
    out.technical_failure_count = GetInt(json, "technical_failure_count");
    out.clarification_count = GetInt(json, "clarification_count");
}

void ParseItemProgress(const cJSON* json, PvItemProgress& out) {
    out.status = GetString(json, "status");
    const cJSON* tarefas = cJSON_GetObjectItem(json, "tarefas");
    if (!cJSON_IsObject(tarefas)) {
        return;
    }
    const cJSON* tarefa = nullptr;
    cJSON_ArrayForEach (tarefa, tarefas) {
        if (!cJSON_IsObject(tarefa)) {
            continue;
        }
        PvTaskProgress parsed;
        ParseTaskProgress(tarefa, parsed);
        out.tarefas.emplace_back(ChildKey(tarefa), std::move(parsed));
    }
}

void ParseLessonTask(const cJSON* json, PvLessonTask& out) {
    out.enunciado = GetString(json, "enunciado");
    out.origem = GetString(json, "origem");
    out.disciplina = GetString(json, "disciplina");
    const cJSON* pagina = cJSON_GetObjectItem(json, "pagina");
    if (cJSON_IsNumber(pagina)) {
        out.pagina = pagina->valueint;
        out.has_pagina = true;
    }
}

void ParseLessonItem(const cJSON* json, PvLessonItem& out) {
    out.titulo = GetString(json, "titulo");
    out.enunciado = GetString(json, "enunciado");
    const cJSON* tarefas = cJSON_GetObjectItem(json, "tarefas");
    if (!cJSON_IsObject(tarefas)) {
        return;
    }
    const cJSON* tarefa = nullptr;
    cJSON_ArrayForEach (tarefa, tarefas) {
        if (!cJSON_IsObject(tarefa)) {
            continue;
        }
        PvLessonTask parsed;
        ParseLessonTask(tarefa, parsed);
        out.tarefas.emplace_back(ChildKey(tarefa), std::move(parsed));
    }
}

}  // namespace

int PvItemProgress::IndexOfTarefa(const std::string& tarefa_id) const {
    for (size_t i = 0; i < tarefas.size(); i++) {
        if (tarefas[i].first == tarefa_id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const PvTaskProgress* PvItemProgress::FindTarefa(const std::string& tarefa_id) const {
    int index = IndexOfTarefa(tarefa_id);
    return index < 0 ? nullptr : &tarefas[static_cast<size_t>(index)].second;
}

bool PvUsageCounters::IsZero() const {
    return llm_calls == 0 && image_generation_calls == 0 && tts_chars_used == 0;
}

bool PvPosition::operator==(const PvPosition& other) const {
    return item_index == other.item_index && tarefa_index == other.tarefa_index;
}

bool PvPosition::operator!=(const PvPosition& other) const { return !(*this == other); }

bool PvPosition::operator<(const PvPosition& other) const {
    if (item_index != other.item_index) {
        return item_index < other.item_index;
    }
    return tarefa_index < other.tarefa_index;
}

void PvSessionState::Clear() { *this = PvSessionState(); }

int PvSessionState::IndexOfItem(const std::string& item_id) const {
    for (size_t i = 0; i < item_progress.size(); i++) {
        if (item_progress[i].first == item_id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const PvItemProgress* PvSessionState::FindItem(const std::string& item_id) const {
    int index = IndexOfItem(item_id);
    return index < 0 ? nullptr : &item_progress[static_cast<size_t>(index)].second;
}

PvPosition PvSessionState::CurrentPosition() const {
    PvPosition position;
    position.item_index = IndexOfItem(current_item);
    if (position.item_index >= 0) {
        position.tarefa_index =
            item_progress[static_cast<size_t>(position.item_index)].second.IndexOfTarefa(
                current_tarefa);
    }
    return position;
}

bool PvSessionState::IsUntouched() const {
    if (!usage_counters.IsZero()) {
        return false;
    }
    for (const auto& item : item_progress) {
        if (item.second.status != "pending") {
            return false;
        }
    }
    return true;
}

int PvLessonItem::IndexOfTarefa(const std::string& tarefa_id) const {
    for (size_t i = 0; i < tarefas.size(); i++) {
        if (tarefas[i].first == tarefa_id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const PvLessonTask* PvLessonItem::FindTarefa(const std::string& tarefa_id) const {
    int index = IndexOfTarefa(tarefa_id);
    return index < 0 ? nullptr : &tarefas[static_cast<size_t>(index)].second;
}

void PvLesson::Clear() { *this = PvLesson(); }

int PvLesson::IndexOfItem(const std::string& item_id) const {
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].first == item_id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const PvLessonItem* PvLesson::FindItem(const std::string& item_id) const {
    int index = IndexOfItem(item_id);
    return index < 0 ? nullptr : &items[static_cast<size_t>(index)].second;
}

bool ParseState(const char* json, PvSessionState& out) {
    out.Clear();
    if (json == nullptr) {
        return false;
    }
    cJSON* root = cJSON_Parse(json);
    if (root == nullptr) {
        return false;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    // Discriminação dos três formatos do §7.2. O objeto de sessão tem
    // "session_status", nunca "status" de topo — então um "status" string
    // significa que NÃO é sessão.
    const cJSON* status = cJSON_GetObjectItem(root, "status");
    if (cJSON_IsString(status) && status->valuestring != nullptr) {
        std::string value = status->valuestring;
        if (value == "no_session") {
            out.kind = PvStateKind::NoSession;
        } else {
            // "invalid_state" e qualquer status desconhecido caem aqui: o
            // remédio documentado é refazer a preparação, e DecideRoute já
            // manda qualquer kind != Session para a tela de preparação.
            out.kind = PvStateKind::InvalidState;
            out.error = GetString(root, "error");
        }
        cJSON_Delete(root);
        return true;
    }

    // Sessão: identificada pela presença de "session_id" (equivalente ao
    // "lesson_id" da lição). Sem ele o corpo não é nenhum dos três formatos.
    const cJSON* session_id = cJSON_GetObjectItem(root, "session_id");
    if (!cJSON_IsString(session_id) || session_id->valuestring == nullptr) {
        cJSON_Delete(root);
        out.Clear();
        return false;
    }

    out.kind = PvStateKind::Session;
    out.session_id = session_id->valuestring;
    out.student_id = GetString(root, "student_id");
    out.student_name = GetString(root, "student_name");
    out.lesson_id = GetString(root, "lesson_id");
    out.session_status = GetString(root, "session_status");
    out.waiting_for_photo = GetBool(root, "waiting_for_photo");
    out.adult_intervention_required = GetBool(root, "adult_intervention_required");
    out.created_at = GetString(root, "created_at");
    out.updated_at = GetString(root, "updated_at");
    out.expires_at = GetString(root, "expires_at");
    out.current_item = GetString(root, "current_item");
    out.current_tarefa = GetString(root, "current_tarefa");

    const cJSON* item_progress = cJSON_GetObjectItem(root, "item_progress");
    if (cJSON_IsObject(item_progress)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach (item, item_progress) {
            if (!cJSON_IsObject(item)) {
                continue;
            }
            PvItemProgress parsed;
            ParseItemProgress(item, parsed);
            out.item_progress.emplace_back(ChildKey(item), std::move(parsed));
        }
    }

    const cJSON* usage = cJSON_GetObjectItem(root, "usage_counters");
    if (cJSON_IsObject(usage)) {
        out.usage_counters.llm_calls = GetInt(usage, "llm_calls");
        out.usage_counters.image_generation_calls = GetInt(usage, "image_generation_calls");
        out.usage_counters.tts_chars_used = GetInt(usage, "tts_chars_used");
    }

    cJSON_Delete(root);
    return true;
}

bool ParseLesson(const char* json, PvLesson& out) {
    out.Clear();
    if (json == nullptr) {
        return false;
    }
    cJSON* root = cJSON_Parse(json);
    if (root == nullptr) {
        return false;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    // A resposta de sucesso do §7.3 não tem campo "status": a lição é
    // detectada pela PRESENÇA de "lesson_id".
    const cJSON* lesson_id = cJSON_GetObjectItem(root, "lesson_id");
    if (!cJSON_IsString(lesson_id) || lesson_id->valuestring == nullptr) {
        const cJSON* status = cJSON_GetObjectItem(root, "status");
        if (!cJSON_IsString(status) || status->valuestring == nullptr) {
            // Nem lição nem {"status": ...}: corpo irreconhecível.
            cJSON_Delete(root);
            out.Clear();
            return false;
        }
        // "no_lesson" (ou qualquer status sem lesson_id) = sem lição.
        cJSON_Delete(root);
        out.has_lesson = false;
        return true;
    }

    out.has_lesson = true;
    out.lesson_id = lesson_id->valuestring;
    out.created_at = GetString(root, "created_at");

    const cJSON* source_pages = cJSON_GetObjectItem(root, "source_pages");
    if (cJSON_IsArray(source_pages)) {
        const cJSON* page = nullptr;
        cJSON_ArrayForEach (page, source_pages) {
            if (cJSON_IsString(page) && page->valuestring != nullptr) {
                out.source_pages.emplace_back(page->valuestring);
            }
        }
    }

    const cJSON* items = cJSON_GetObjectItem(root, "items");
    if (cJSON_IsObject(items)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach (item, items) {
            if (!cJSON_IsObject(item)) {
                continue;
            }
            PvLessonItem parsed;
            ParseLessonItem(item, parsed);
            out.items.emplace_back(ChildKey(item), std::move(parsed));
        }
    }

    cJSON_Delete(root);
    return true;
}

bool IsSessionUsable(const PvSessionState& state) {
    if (state.kind != PvStateKind::Session) {
        return false;
    }
    return state.session_status != "closed" && state.session_status != "expired";
}

PvRoute DecideRoute(const PvSessionState& state, const PvLesson& lesson) {
    // Ordem exata do §9.1 — não reordenar.
    if (!IsSessionUsable(state) || !lesson.has_lesson) {
        return PvRoute::Preparation;
    }
    if (state.session_status == "completed") {
        return PvRoute::Celebration;
    }
    if (state.adult_intervention_required) {
        return PvRoute::Failsafe;
    }
    return PvRoute::Tutoring;
}

const char* PvRouteName(PvRoute route) {
    switch (route) {
        case PvRoute::Preparation:
            return "preparation";
        case PvRoute::Celebration:
            return "celebration";
        case PvRoute::Failsafe:
            return "failsafe";
        case PvRoute::Tutoring:
            return "tutoring";
    }
    return "unknown";
}
