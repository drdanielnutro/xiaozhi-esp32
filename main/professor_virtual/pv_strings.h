#ifndef PV_STRINGS_H
#define PV_STRINGS_H

// Textos pt-BR do Professor Virtual (DOCUMENTACAO-APP.md, Apêndice A).
// Mantido fora do sistema de locales do assistente por decisão estrutural
// (decision-log 2026-08-03): header próprio, sem tocar main/assets/locales.
namespace PvStrings {

inline constexpr const char* kAppName = "Professor Virtual";
inline constexpr const char* kBootStatus = "Iniciando...";
inline constexpr const char* kNetScanning = "Procurando redes Wi-Fi...";
inline constexpr const char* kNetConnecting = "Conectando a ";
inline constexpr const char* kNetConnected = "Conectado a ";
inline constexpr const char* kNetDisconnected = "Sem conexão com a rede";
inline constexpr const char* kNetConfigMode = "Modo de configuração de rede";

}  // namespace PvStrings

#endif  // PV_STRINGS_H
