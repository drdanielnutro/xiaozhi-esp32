#ifndef PV_STRINGS_H
#define PV_STRINGS_H

// Textos pt-BR do Professor Virtual (DOCUMENTACAO-APP.md, Apêndice A).
// Mantido fora do sistema de locales do assistente por decisão estrutural
// (decision-log 2026-08-03): header próprio, sem tocar main/assets/locales.
namespace PvStrings {

inline constexpr const char* kAppName = "Professor Virtual";
inline constexpr const char* kBootStatus = "Iniciando...";

}  // namespace PvStrings

#endif  // PV_STRINGS_H
