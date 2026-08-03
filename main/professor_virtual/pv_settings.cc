#include "pv_settings.h"

#include "settings.h"

namespace {
constexpr const char* kNamespace = "pv";
constexpr const char* kKeyBackendUrl = "backend_url";
constexpr const char* kKeyApiToken = "api_token";

// Remove espaços e barras finais para que o pv_backend_client possa
// concatenar caminhos "/api/..." e URLs relativas de mídia sem duplicar "/".
std::string NormalizeUrl(const std::string& url) {
    size_t begin = url.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    size_t end = url.find_last_not_of(" \t\r\n");
    std::string trimmed = url.substr(begin, end - begin + 1);
    while (!trimmed.empty() && trimmed.back() == '/') {
        trimmed.pop_back();
    }
    return trimmed;
}
}  // namespace

namespace PvSettings {

std::string GetBackendUrl() {
    Settings settings(kNamespace, false);
    return settings.GetString(kKeyBackendUrl);
}

void SetBackendUrl(const std::string& url) {
    Settings settings(kNamespace, true);
    settings.SetString(kKeyBackendUrl, NormalizeUrl(url));
}

bool HasApiToken() {
    Settings settings(kNamespace, false);
    return !settings.GetString(kKeyApiToken).empty();
}

std::string GetApiToken() {
    Settings settings(kNamespace, false);
    return settings.GetString(kKeyApiToken);
}

void SetApiToken(const std::string& token) {
    Settings settings(kNamespace, true);
    settings.SetString(kKeyApiToken, token);
}

bool IsConfigured() {
    Settings settings(kNamespace, false);
    return !settings.GetString(kKeyBackendUrl).empty() &&
           !settings.GetString(kKeyApiToken).empty();
}

}  // namespace PvSettings
