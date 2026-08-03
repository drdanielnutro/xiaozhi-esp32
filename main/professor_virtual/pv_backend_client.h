#ifndef PV_BACKEND_CLIENT_H
#define PV_BACKEND_CLIENT_H

#include <string>

#include "pv_session_mirror.h"

// Cliente HTTP do contrato de dispositivo v1.1 (docs/professor-virtual/
// contrato-dispositivo.md) para os endpoints de leitura da hidratação:
// GET /api/health (§7.1), GET /api/state (§7.2) e GET /api/lesson (§7.3).
//
// ATENÇÃO — todas as chamadas são BLOQUEANTES (abrem conexão, esperam resposta
// e leem o corpo). Quem chama deve rodar fora do loop LVGL e fora das tasks de
// áudio: use a task própria do Professor Virtual e só depois toque a UI sob
// DisplayLockGuard.
//
// Credencial: o token vem de PvSettings::GetApiToken() e vai em
// "Authorization: Bearer <token>" em TODA chamada. O token nunca é logado,
// exibido nem embutido em mensagem de erro/estado — nem em fragmento.

// Taxonomia de resultado exigida pela F1.
enum class PvBackendStatus {
    Ok,                  // 200 com corpo válido
    NetworkError,        // falha ao abrir a conexão, timeout ou resposta ausente
    Unauthorized,        // HTTP 401 — token ausente/incorreto
    ServiceUnavailable,  // HTTP 503 — backend sem DEVICE_API_TOKEN configurado
    HttpError,           // qualquer outro status != 200
    ParseError,          // 200 com corpo inválido para o contrato
};

struct PvBackendResult {
    PvBackendStatus status = PvBackendStatus::NetworkError;
    int http_status = 0;  // 0 quando a resposta nem chegou

    bool ok() const { return status == PvBackendStatus::Ok; }
    // 401/503 interrompem o fluxo normal e vão para configuração/erro,
    // nunca para a resposta pedagógica.
    bool needs_credentials() const {
        return status == PvBackendStatus::Unauthorized ||
               status == PvBackendStatus::ServiceUnavailable;
    }
};

namespace PvBackendClient {

// Timeouts por requisição (decisão F1-HttpTimeout).
constexpr int kHealthTimeoutMs = 5000;
constexpr int kHydrationTimeoutMs = 15000;

// Resolve um caminho relativo do backend ("/api/media/x.wav", vindo de
// audio_url/image_url do perfil v1.1) contra PvSettings::GetBackendUrl().
// URLs absolutas (http:// ou https://) são devolvidas sem alteração. Devolve
// "" quando não há base configurada e o caminho é relativo.
std::string ResolveUrl(const std::string& relative_or_absolute);

// GET /api/health — Ok apenas com 200 e {"status": "ok", ...}.
PvBackendResult CheckHealth();

// GET /api/state — preenche `out` apenas quando o resultado é Ok.
PvBackendResult FetchState(PvSessionState& out);

// GET /api/lesson — preenche `out` apenas quando o resultado é Ok.
PvBackendResult FetchLesson(PvLesson& out);

// Nome estável do resultado, para log/diagnóstico (nunca inclui o token).
const char* StatusName(PvBackendStatus status);

}  // namespace PvBackendClient

#endif  // PV_BACKEND_CLIENT_H
