#include "pv_backend_client.h"

#include <esp_log.h>

#include <cJSON.h>

#include <memory>

#include "board.h"
#include "pv_settings.h"

#define TAG "PvBackend"

namespace {

constexpr const char* kPathHealth = "/api/health";
constexpr const char* kPathState = "/api/state";
constexpr const char* kPathLesson = "/api/lesson";

bool StartsWithIgnoreCase(const std::string& text, const char* prefix) {
    size_t i = 0;
    for (; prefix[i] != '\0'; i++) {
        if (i >= text.size()) {
            return false;
        }
        char a = text[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

// Traduz o status HTTP na taxonomia da F1. O wrapper devolve -1 quando os
// cabeçalhos não chegam (timeout/erro de rede): qualquer valor abaixo de 100
// não é uma resposta HTTP e vira NetworkError (revisão F1, P1).
PvBackendStatus ClassifyHttpStatus(int http_status) {
    if (http_status < 100) {
        return PvBackendStatus::NetworkError;
    }
    switch (http_status) {
        case 200:
            return PvBackendStatus::Ok;
        case 401:
            return PvBackendStatus::Unauthorized;
        case 503:
            return PvBackendStatus::ServiceUnavailable;
        default:
            return PvBackendStatus::HttpError;
    }
}

// Teto de segurança para corpos de resposta: uma lição de 20 páginas cabe com
// folga; algo maior que isso é anomalia e não deve estourar a RAM.
constexpr size_t kMaxBodyBytes = 256 * 1024;

// Lê o corpo em blocos. O wrapper pausa a recepção quando a fila interna
// atinge 8 KiB e ReadAll() só drena depois do EOF — com corpos maiores que
// 8 KiB isso trava até o timeout (revisão F1, P1). A leitura incremental
// drena a fila e destrava o produtor.
bool ReadBody(Http& http, std::string& body) {
    char buffer[1024];
    while (true) {
        int n = http.Read(buffer, sizeof(buffer));
        if (n < 0) {
            return false;
        }
        if (n == 0) {
            return true;
        }
        if (body.size() + static_cast<size_t>(n) > kMaxBodyBytes) {
            return false;
        }
        body.append(buffer, static_cast<size_t>(n));
    }
}

// GET bloqueante com token em cabeçalho. Um objeto Http novo por requisição:
// nunca reutilizar entre chamadas, e Close() sempre — inclusive em erro e
// timeout (ressalva da decisão F1-HttpTimeout).
PvBackendResult Get(const char* path, int timeout_ms, std::string& body) {
    PvBackendResult result;
    body.clear();

    std::string url = PvBackendClient::ResolveUrl(path);
    if (url.empty()) {
        ESP_LOGW(TAG, "GET %s ignorado: backend_url não configurado", path);
        result.status = PvBackendStatus::NetworkError;
        return result;
    }

    auto* network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        ESP_LOGE(TAG, "GET %s sem interface de rede", path);
        result.status = PvBackendStatus::NetworkError;
        return result;
    }

    auto http = network->CreateHttp(3);
    if (http == nullptr) {
        ESP_LOGE(TAG, "GET %s: não foi possível criar o cliente HTTP", path);
        result.status = PvBackendStatus::NetworkError;
        return result;
    }

    http->SetTimeout(timeout_ms);
    http->SetHeader("Accept", "application/json");
    {
        // Escopo curto para o token: monta o cabeçalho e some. NUNCA logar.
        std::string token = PvSettings::GetApiToken();
        if (!token.empty()) {
            http->SetHeader("Authorization", "Bearer " + token);
        }
        // Token vazio: a chamada segue sem cabeçalho e o backend responde
        // 401/503; a UI encaminha para a tela de configuração.
    }

    ESP_LOGI(TAG, "GET %s (timeout %d ms)", url.c_str(), timeout_ms);
    if (!http->Open("GET", url)) {
        ESP_LOGE(TAG, "Falha ao abrir %s (erro 0x%x)", path, http->GetLastError());
        http->Close();
        result.status = PvBackendStatus::NetworkError;
        return result;
    }

    result.http_status = http->GetStatusCode();
    result.status = ClassifyHttpStatus(result.http_status);
    if (result.status != PvBackendStatus::Ok) {
        // Corpo de erro não é lido nem logado: pode conter detalhe do servidor
        // e não muda o tratamento do cliente.
        http->Close();
        ESP_LOGW(TAG, "GET %s respondeu HTTP %d (%s)", path, result.http_status,
                 PvBackendClient::StatusName(result.status));
        return result;
    }

    bool read_ok = ReadBody(*http, body);
    http->Close();

    if (!read_ok) {
        ESP_LOGW(TAG, "GET %s: corpo interrompido ou acima de %u bytes", path,
                 static_cast<unsigned>(kMaxBodyBytes));
        body.clear();
        result.status = PvBackendStatus::NetworkError;
        return result;
    }

    if (body.empty()) {
        // 200 sem corpo = resposta truncada/perdida; tratado como falha de rede
        // (recuperável por nova tentativa), não como contrato quebrado.
        ESP_LOGW(TAG, "GET %s respondeu 200 sem corpo", path);
        result.status = PvBackendStatus::NetworkError;
        return result;
    }

    ESP_LOGD(TAG, "GET %s: %u bytes de corpo", path, static_cast<unsigned>(body.size()));
    return result;
}

}  // namespace

namespace PvBackendClient {

std::string ResolveUrl(const std::string& relative_or_absolute) {
    if (StartsWithIgnoreCase(relative_or_absolute, "http://") ||
        StartsWithIgnoreCase(relative_or_absolute, "https://")) {
        return relative_or_absolute;
    }

    std::string base = PvSettings::GetBackendUrl();
    while (!base.empty() && base.back() == '/') {  // defensivo: já vem normalizada
        base.pop_back();
    }
    if (base.empty()) {
        return "";
    }
    if (relative_or_absolute.empty()) {
        return base;
    }
    if (relative_or_absolute.front() == '/') {
        return base + relative_or_absolute;
    }
    return base + "/" + relative_or_absolute;
}

PvBackendResult CheckHealth() {
    std::string body;
    PvBackendResult result = Get(kPathHealth, kHealthTimeoutMs, body);
    if (!result.ok()) {
        return result;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        ESP_LOGW(TAG, "health: corpo não é JSON válido");
        result.status = PvBackendStatus::ParseError;
        return result;
    }
    const cJSON* status = cJSON_GetObjectItem(root, "status");
    bool healthy = cJSON_IsString(status) && status->valuestring != nullptr &&
                   std::string(status->valuestring) == "ok";
    cJSON_Delete(root);

    if (!healthy) {
        ESP_LOGW(TAG, "health: resposta sem status \"ok\"");
        result.status = PvBackendStatus::ParseError;
    }
    return result;
}

PvBackendResult FetchState(PvSessionState& out) {
    std::string body;
    PvBackendResult result = Get(kPathState, kHydrationTimeoutMs, body);
    if (!result.ok()) {
        return result;
    }
    if (!ParseState(body.c_str(), out)) {
        ESP_LOGW(TAG, "state: corpo fora do contrato §7.2");
        result.status = PvBackendStatus::ParseError;
        return result;
    }
    ESP_LOGI(TAG, "state: kind=%d session_status=%s", static_cast<int>(out.kind),
             out.session_status.c_str());
    return result;
}

PvBackendResult FetchLesson(PvLesson& out) {
    std::string body;
    PvBackendResult result = Get(kPathLesson, kHydrationTimeoutMs, body);
    if (!result.ok()) {
        return result;
    }
    if (!ParseLesson(body.c_str(), out)) {
        ESP_LOGW(TAG, "lesson: corpo fora do contrato §7.3");
        result.status = PvBackendStatus::ParseError;
        return result;
    }
    ESP_LOGI(TAG, "lesson: %s (%u itens)", out.has_lesson ? "presente" : "ausente",
             static_cast<unsigned>(out.items.size()));
    return result;
}

const char* StatusName(PvBackendStatus status) {
    switch (status) {
        case PvBackendStatus::Ok:
            return "ok";
        case PvBackendStatus::NetworkError:
            return "erro de rede";
        case PvBackendStatus::Unauthorized:
            return "nao autorizado";
        case PvBackendStatus::ServiceUnavailable:
            return "servico indisponivel";
        case PvBackendStatus::HttpError:
            return "erro http";
        case PvBackendStatus::ParseError:
            return "resposta invalida";
    }
    return "desconhecido";
}

}  // namespace PvBackendClient
