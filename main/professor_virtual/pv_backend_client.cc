#include "pv_backend_client.h"

#include <esp_log.h>
#include <esp_random.h>

#include <cJSON.h>

#include <memory>

#include "board.h"
#include "pv_settings.h"

#define TAG "PvBackend"

namespace {

constexpr const char* kPathHealth = "/api/health";
constexpr const char* kPathState = "/api/state";
constexpr const char* kPathLesson = "/api/lesson";
constexpr const char* kPathTurn = "/api/turn";

// Fatia de envio do JPEG. Cada Write() vira um chunk HTTP com cabeçalho
// próprio: fatias muito pequenas desperdiçam banda, e fatias grandes não
// ajudam porque a pilha TCP fragmenta assim mesmo. 8 KiB é o mesmo tamanho da
// fila interna do wrapper.
constexpr size_t kUploadChunkBytes = 8 * 1024;

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

// Sufixo do CAMINHO da URL, ignorando maiúsculas (o backend gera nomes em
// minúsculo, mas um proxy pode reescrever).
bool EndsWithIgnoreCase(const std::string& text, const char* suffix) {
    size_t len = 0;
    while (suffix[len] != '\0') {
        len++;
    }
    if (text.size() < len) {
        return false;
    }
    size_t offset = text.size() - len;
    for (size_t i = 0; i < len; i++) {
        char a = text[offset + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = static_cast<char>(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

// Parte da URL antes de query/fragmento: a extensão precisa ser checada no
// caminho, não em "?t=123" nem em "#x".
std::string UrlPath(const std::string& url) {
    size_t cut = url.find_first_of("?#");
    return cut == std::string::npos ? url : url.substr(0, cut);
}

// Content-Type casa com o esperado quando o valor COMEÇA com o mime e o que
// vem depois é fim de string ou separador de parâmetro ("; charset=..."):
// "audio/wavx" não é audio/wav.
bool ContentTypeMatches(const std::string& header, const char* expected_mime) {
    if (!StartsWithIgnoreCase(header, expected_mime)) {
        return false;
    }
    size_t len = 0;
    while (expected_mime[len] != '\0') {
        len++;
    }
    if (header.size() == len) {
        return true;
    }
    char next = header[len];
    return next == ';' || next == ' ' || next == '\t';
}

// Segunda barreira, independente do cabeçalho: os primeiros bytes do arquivo.
// Um 200 de página de erro (ou um proxy que devolve HTML) não passa daqui.
bool MagicBytesMatch(const char* expected_mime, const std::vector<uint8_t>& body) {
    std::string mime = expected_mime;
    if (mime == PvBackendClient::kAudioMime) {
        return body.size() >= 4 && body[0] == 'R' && body[1] == 'I' && body[2] == 'F' &&
               body[3] == 'F';
    }
    if (mime == PvBackendClient::kImageMime) {
        return body.size() >= 2 && body[0] == 0xFF && body[1] == 0xD8;
    }
    return false;
}

// Cabeçalho de credencial. Escopo curto para o token: monta e some. NUNCA
// logar, nem em fragmento. Token vazio segue sem cabeçalho e o backend
// responde 401/503, que a UI encaminha para a configuração.
void ApplyAuthHeader(Http& http) {
    std::string token = PvSettings::GetApiToken();
    if (!token.empty()) {
        http.SetHeader("Authorization", "Bearer " + token);
    }
}

// Escreve um bloco no corpo chunked. O wrapper devolve os bytes REALMENTE
// enviados no socket, que incluem o cabeçalho do chunk — por isso o teste é
// "não negativo / não zero", nunca "== tamanho pedido".
bool WriteChunk(Http& http, const char* data, size_t size) {
    if (size == 0) {
        return true;
    }
    int written = http.Write(data, size);
    return written > 0;
}

bool WriteChunk(Http& http, const std::string& data) {
    return WriteChunk(http, data.c_str(), data.size());
}

// Um campo de texto do multipart.
std::string FormField(const std::string& boundary, const char* name, const std::string& value) {
    std::string part;
    part += "--" + boundary + "\r\n";
    part += "Content-Disposition: form-data; name=\"" + std::string(name) + "\"\r\n\r\n";
    part += value + "\r\n";
    return part;
}

// Hex aleatório do gerador de hardware, para o boundary do multipart: um
// boundary fixo poderia (em teoria) aparecer dentro do JPEG e cortar o corpo.
std::string RandomHex(size_t byte_count) {
    static const char kHex[] = "0123456789abcdef";
    std::vector<uint8_t> bytes(byte_count);
    esp_fill_random(bytes.data(), bytes.size());
    std::string out;
    out.reserve(byte_count * 2);
    for (uint8_t byte : bytes) {
        out.push_back(kHex[(byte >> 4) & 0x0F]);
        out.push_back(kHex[byte & 0x0F]);
    }
    return out;
}

// Lê um corpo BINÁRIO em blocos, com teto próprio. Mesma razão do ReadBody:
// o wrapper pausa a recepção com a fila cheia e ReadAll() só drena no EOF.
bool ReadBinaryBody(Http& http, std::vector<uint8_t>& out, size_t max_bytes) {
    char buffer[1024];
    while (true) {
        int n = http.Read(buffer, sizeof(buffer));
        if (n < 0) {
            return false;
        }
        if (n == 0) {
            return true;
        }
        if (out.size() + static_cast<size_t>(n) > max_bytes) {
            return false;
        }
        out.insert(out.end(), reinterpret_cast<const uint8_t*>(buffer),
                   reinterpret_cast<const uint8_t*>(buffer) + n);
    }
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

std::string NewRequestId() {
    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof(bytes));
    // RFC 4122: versão 4 no nibble alto do byte 6 e variante "10" nos dois bits
    // altos do byte 8. O resto continua aleatório.
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3F) | 0x80);

    static const char kHex[] = "0123456789abcdef";
    std::string uuid;
    uuid.reserve(36);
    for (size_t i = 0; i < sizeof(bytes); i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            uuid.push_back('-');
        }
        uuid.push_back(kHex[(bytes[i] >> 4) & 0x0F]);
        uuid.push_back(kHex[bytes[i] & 0x0F]);
    }
    return uuid;
}

PvBackendResult PostTurnPhoto(const std::string& session_id, const std::string& request_id,
                              const uint8_t* jpeg, size_t jpeg_len, PvTurnResponse& out) {
    PvBackendResult result;
    out.Clear();

    // Argumentos impossíveis: a requisição nem sai. NetworkError é o balde de
    // "não houve resposta do servidor" — nada foi aplicado do outro lado, que é
    // exatamente a garantia que o chamador precisa aqui.
    if (session_id.empty() || request_id.empty() || jpeg == nullptr || jpeg_len == 0) {
        ESP_LOGE(TAG, "turn: argumentos inválidos (sessão/request_id/imagem)");
        result.status = PvBackendStatus::NetworkError;
        return result;
    }

    std::string url = ResolveUrl(kPathTurn);
    if (url.empty()) {
        ESP_LOGW(TAG, "turn ignorado: backend_url não configurado");
        result.status = PvBackendStatus::NetworkError;
        return result;
    }

    auto* network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        ESP_LOGE(TAG, "turn sem interface de rede");
        result.status = PvBackendStatus::NetworkError;
        return result;
    }
    auto http = network->CreateHttp(3);
    if (http == nullptr) {
        ESP_LOGE(TAG, "turn: não foi possível criar o cliente HTTP");
        result.status = PvBackendStatus::NetworkError;
        return result;
    }

    std::string boundary = "----ProfessorVirtual" + RandomHex(12);

    http->SetTimeout(kTurnTimeoutMs);
    http->SetHeader("Accept", "application/json");
    ApplyAuthHeader(*http);
    http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    // Chunked: o tamanho total do corpo não precisa ser conhecido de antemão e
    // o JPEG nunca é copiado inteiro para uma string de montagem.
    http->SetHeader("Transfer-Encoding", "chunked");

    ESP_LOGI(TAG, "POST %s (%u bytes de imagem, timeout %d ms)", kPathTurn,
             static_cast<unsigned>(jpeg_len), kTurnTimeoutMs);
    if (!http->Open("POST", url)) {
        ESP_LOGE(TAG, "Falha ao abrir %s (erro 0x%x)", kPathTurn, http->GetLastError());
        http->Close();
        result.status = PvBackendStatus::NetworkError;
        return result;
    }

    bool sent = true;
    sent = sent && WriteChunk(*http, FormField(boundary, "session_id", session_id));
    sent = sent && WriteChunk(*http, FormField(boundary, "request_id", request_id));
    sent = sent && WriteChunk(*http, FormField(boundary, "media", kTurnMediaMode));
    sent = sent && WriteChunk(*http, FormField(boundary, "audio_format", kTurnAudioFormat));
    sent = sent &&
           WriteChunk(*http, FormField(boundary, "image_max_px", std::to_string(kTurnImageMaxPx)));

    if (sent) {
        std::string file_header;
        file_header += "--" + boundary + "\r\n";
        file_header += "Content-Disposition: form-data; name=\"image\"; filename=\"photo.jpg\"\r\n";
        file_header += "Content-Type: image/jpeg\r\n\r\n";
        sent = WriteChunk(*http, file_header);
    }

    // A imagem sai em fatias: o buffer do chamador é a única cópia dos bytes.
    for (size_t offset = 0; sent && offset < jpeg_len; offset += kUploadChunkBytes) {
        size_t slice = jpeg_len - offset;
        if (slice > kUploadChunkBytes) {
            slice = kUploadChunkBytes;
        }
        sent = WriteChunk(*http, reinterpret_cast<const char*>(jpeg + offset), slice);
    }

    if (sent) {
        sent = WriteChunk(*http, "\r\n--" + boundary + "--\r\n");
    }
    if (sent) {
        // Chunk final (tamanho zero): fecha o corpo e libera a resposta.
        sent = http->Write("", 0) >= 0;
    }

    if (!sent) {
        ESP_LOGE(TAG, "turn: envio interrompido (erro 0x%x)", http->GetLastError());
        http->Close();
        result.status = PvBackendStatus::NetworkError;
        return result;
    }

    result.http_status = http->GetStatusCode();
    result.status = ClassifyHttpStatus(result.http_status);
    if (result.status != PvBackendStatus::Ok) {
        // Corpo de erro não é lido nem logado. 409 e 502 TÊM efeito no
        // servidor; quem decide o que fazer com o status é a camada de fluxo.
        http->Close();
        ESP_LOGW(TAG, "POST %s respondeu HTTP %d (%s)", kPathTurn, result.http_status,
                 StatusName(result.status));
        return result;
    }

    std::string body;
    bool read_ok = ReadBody(*http, body);
    http->Close();

    if (!read_ok || body.empty()) {
        ESP_LOGW(TAG, "turn: corpo interrompido, vazio ou acima de %u bytes",
                 static_cast<unsigned>(kMaxBodyBytes));
        result.status = PvBackendStatus::NetworkError;
        return result;
    }

    if (!ParseTurnResponse(body.c_str(), out)) {
        // Sem eco do corpo no log: ele carrega texto do aluno/tutor.
        ESP_LOGW(TAG, "turn: corpo fora do perfil v1.1 do §7.5");
        result.status = PvBackendStatus::ParseError;
        return result;
    }

    // Eco da idempotência: uma resposta com outro request_id é de outro turno
    // (cache divergente do backend) e não pode ser exibida como se fosse desta
    // pergunta.
    if (out.request_id != request_id) {
        ESP_LOGE(TAG, "turn: request_id ecoado diverge do enviado");
        out.Clear();
        result.status = PvBackendStatus::ParseError;
        return result;
    }

    ESP_LOGI(TAG, "turn: veredicto=%s session_status=%s", PvVerdictName(out.veredicto),
             out.session_status.c_str());
    return result;
}

PvBackendResult DownloadMedia(const std::string& url_or_path, const char* expected_mime,
                              const char* expected_ext, std::vector<uint8_t>& out) {
    PvBackendResult result;
    out.clear();

    if (expected_mime == nullptr || expected_ext == nullptr || url_or_path.empty()) {
        ESP_LOGE(TAG, "mídia: argumentos inválidos");
        result.status = PvBackendStatus::NetworkError;
        return result;
    }

    // Extensão ANTES de abrir: uma URL fora do par esperado nunca chega a virar
    // requisição, e o log fica sem o caminho completo do arquivo.
    if (!EndsWithIgnoreCase(UrlPath(url_or_path), expected_ext)) {
        ESP_LOGE(TAG, "mídia: caminho não termina em %s", expected_ext);
        result.status = PvBackendStatus::ParseError;
        return result;
    }

    std::string url = ResolveUrl(url_or_path);
    if (url.empty()) {
        ESP_LOGW(TAG, "mídia ignorada: backend_url não configurado");
        result.status = PvBackendStatus::NetworkError;
        return result;
    }

    auto* network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        ESP_LOGE(TAG, "mídia sem interface de rede");
        result.status = PvBackendStatus::NetworkError;
        return result;
    }
    auto http = network->CreateHttp(3);
    if (http == nullptr) {
        ESP_LOGE(TAG, "mídia: não foi possível criar o cliente HTTP");
        result.status = PvBackendStatus::NetworkError;
        return result;
    }

    http->SetTimeout(kMediaTimeoutMs);
    http->SetHeader("Accept", expected_mime);
    ApplyAuthHeader(*http);

    ESP_LOGI(TAG, "GET mídia %s (timeout %d ms)", expected_ext, kMediaTimeoutMs);
    if (!http->Open("GET", url)) {
        ESP_LOGE(TAG, "Falha ao abrir mídia %s (erro 0x%x)", expected_ext, http->GetLastError());
        http->Close();
        result.status = PvBackendStatus::NetworkError;
        return result;
    }

    result.http_status = http->GetStatusCode();
    result.status = ClassifyHttpStatus(result.http_status);
    if (result.status != PvBackendStatus::Ok) {
        http->Close();
        ESP_LOGW(TAG, "mídia %s respondeu HTTP %d (%s)", expected_ext, result.http_status,
                 StatusName(result.status));
        return result;
    }

    // Cabeçalho REAL da resposta: o wrapper Http expõe GetResponseHeader() e
    // GetStatusCode() já esperou os cabeçalhos, então a tabela está preenchida.
    // O Content-Type é OBRIGATÓRIO (revisão F3, P2b): o contrato promete a rota
    // de mídia declarando o tipo, e "ausente" é indistinguível de uma resposta
    // intermediária (proxy, portal cativo) que devolveu outra coisa com 200.
    // Os bytes mágicos, mais abaixo, continuam como SEGUNDA barreira — esta
    // rejeita antes de baixar megabytes.
    std::string content_type = http->GetResponseHeader("Content-Type");
    if (content_type.empty() || !ContentTypeMatches(content_type, expected_mime)) {
        http->Close();
        ESP_LOGE(TAG, "mídia: Content-Type ausente ou inesperado (esperado %s)", expected_mime);
        result.status = PvBackendStatus::ParseError;
        return result;
    }

    // Content-Length conhecido e acima do teto: nem começa a baixar.
    size_t declared = http->GetBodyLength();
    if (declared > kMaxMediaBytes) {
        http->Close();
        ESP_LOGE(TAG, "mídia: %u bytes declarados acima do teto de %u",
                 static_cast<unsigned>(declared), static_cast<unsigned>(kMaxMediaBytes));
        result.status = PvBackendStatus::NetworkError;
        return result;
    }
    if (declared > 0) {
        out.reserve(declared);
    }

    bool read_ok = ReadBinaryBody(*http, out, kMaxMediaBytes);
    http->Close();

    if (!read_ok) {
        ESP_LOGW(TAG, "mídia: corpo interrompido ou acima de %u bytes",
                 static_cast<unsigned>(kMaxMediaBytes));
        out.clear();
        result.status = PvBackendStatus::NetworkError;
        return result;
    }
    if (out.empty()) {
        // 200 sem corpo = transferência perdida; falha de rede, não contrato.
        ESP_LOGW(TAG, "mídia %s respondeu 200 sem corpo", expected_ext);
        result.status = PvBackendStatus::NetworkError;
        return result;
    }
    if (!MagicBytesMatch(expected_mime, out)) {
        ESP_LOGE(TAG, "mídia: conteúdo não parece %s", expected_mime);
        out.clear();
        result.status = PvBackendStatus::ParseError;
        return result;
    }

    ESP_LOGI(TAG, "mídia %s: %u bytes", expected_ext, static_cast<unsigned>(out.size()));
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
