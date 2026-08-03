#include "pv_route_text.h"

#include "pv_strings.h"

namespace {

// Corta `text` em no máximo `max_chars` BYTES sem partir uma sequência UTF-8:
// os enunciados vêm em pt-BR e um corte no meio de um "ç"/"ã" viraria glifo
// inválido no LVGL. Recua até o último byte inicial de caractere.
std::string TruncateUtf8(const std::string& text, size_t max_chars) {
    if (max_chars == 0 || text.size() <= max_chars) {
        return text;
    }
    size_t cut = max_chars;
    // Bytes de continuação UTF-8 têm os bits 10xxxxxx.
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
        cut--;
    }
    std::string result = text.substr(0, cut);
    result += "...";
    return result;
}

const std::string& ValueOrPlaceholder(const std::string& value) {
    static const std::string kUnknown = PvStrings::kRouteUnknownValue;
    return value.empty() ? kUnknown : value;
}

}  // namespace

const char* PvRouteTitle(PvRoute route) {
    switch (route) {
        case PvRoute::Preparation:
            return PvStrings::kRoutePreparationTitle;
        case PvRoute::Celebration:
            return PvStrings::kRouteCelebrationTitle;
        case PvRoute::Failsafe:
            return PvStrings::kRouteFailsafeTitle;
        case PvRoute::Tutoring:
            break;
    }
    return PvStrings::kRouteTutoringTitle;
}

const char* PvRouteSubtitle(PvRoute route) {
    switch (route) {
        case PvRoute::Preparation:
            return PvStrings::kRoutePreparationSubtitle;
        case PvRoute::Celebration:
            return PvStrings::kRouteCelebrationSubtitle;
        case PvRoute::Failsafe:
            return PvStrings::kRouteFailsafeSubtitle;
        case PvRoute::Tutoring:
            break;
    }
    return PvStrings::kRouteTutoringSubtitle;
}

std::string PvBuildTutoringDetail(const PvSessionState& state, const PvLesson& lesson,
                                  size_t max_statement_chars) {
    std::string detail = PvStrings::kRouteItemPrefix;
    detail += ValueOrPlaceholder(state.current_item);
    detail += "\n";
    detail += PvStrings::kRouteTaskPrefix;
    detail += ValueOrPlaceholder(state.current_tarefa);

    const PvLessonItem* item = lesson.FindItem(state.current_item);
    if (item == nullptr) {
        return detail;
    }
    if (!item->titulo.empty()) {
        detail += "\n";
        detail += item->titulo;
    }
    const PvLessonTask* task = item->FindTarefa(state.current_tarefa);
    if (task != nullptr && !task->enunciado.empty()) {
        detail += "\n";
        detail += TruncateUtf8(task->enunciado, max_statement_chars);
    }
    return detail;
}
