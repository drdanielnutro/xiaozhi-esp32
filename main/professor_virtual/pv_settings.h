#ifndef PV_SETTINGS_H
#define PV_SETTINGS_H

#include <string>

// Configuração persistente do Professor Virtual no namespace NVS exato "pv"
// (contrato de dispositivo v1.1). O token de dispositivo NUNCA aparece em UI,
// log ou mensagem de erro; GetApiToken() existe apenas para montar o
// cabeçalho de autenticação no pv_backend_client.
namespace PvSettings {

std::string GetBackendUrl();
void SetBackendUrl(const std::string& url);

bool HasApiToken();
std::string GetApiToken();
void SetApiToken(const std::string& token);

// true quando backend_url e api_token estão provisionados.
bool IsConfigured();

}  // namespace PvSettings

#endif  // PV_SETTINGS_H
