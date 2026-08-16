#ifndef PV_BACKEND_CLIENT_H
#define PV_BACKEND_CLIENT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pv_session_mirror.h"

// Cliente HTTP do contrato de dispositivo v1.1 (docs/professor-virtual/
// contrato-dispositivo.md): endpoints de leitura da hidratação —
// GET /api/health (§7.1), GET /api/state (§7.2), GET /api/lesson (§7.3) — e o
// turno com foto — POST /api/turn (§7.5) + GET /api/media/... .
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
// O turno faz avaliação por LLM + TTS + geração de imagem no backend: dois
// minutos é o piso pedido pela F3, não uma folga generosa.
constexpr int kTurnTimeoutMs = 120000;
// Download de mídia é só transferência de arquivo já pronto.
constexpr int kMediaTimeoutMs = 60000;

// Campos fixos do perfil v1.1 enviados em todo turno.
constexpr const char* kTurnMediaMode = "url";
constexpr const char* kTurnAudioFormat = "wav";
// Tela do dispositivo é 1280x800; pedir mais que 1280 no lado maior só gastaria
// banda e RAM (contrato, "POST /api/turn — campos opcionais novos").
constexpr int kTurnImageMaxPx = 1280;

// Pares MIME/extensão aceitos em DownloadMedia (perfil v1.1: WAV e JPEG).
constexpr const char* kAudioMime = "audio/wav";
constexpr const char* kAudioExt = ".wav";
constexpr const char* kImageMime = "image/jpeg";
constexpr const char* kImageExt = ".jpg";

// Teto de um arquivo de mídia. O WAV do tutor passa de 1 MB com facilidade
// (PCM s16le 16 kHz), então o teto de 256 KiB dos corpos JSON não serve aqui;
// 8 MiB continua sendo anomalia e protege a PSRAM.
constexpr size_t kMaxMediaBytes = 8 * 1024 * 1024;

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

// UUID v4 canônico (36 caracteres, hex minúsculo com hífens) a partir do
// gerador de hardware (esp_fill_random). Serve de `request_id`: o chamador
// gera UM id por TURNO LÓGICO e nunca o reaproveita para outro conteúdo —
// reaproveitar id é o que ativa o 409 fail-safe do backend.
std::string NewRequestId();

// POST /api/turn (§7.5) com a foto da tarefa, no perfil v1.1
// (media=url, audio_format=wav, image_max_px=1280).
//
// O corpo multipart é enviado com Transfer-Encoding: chunked e a imagem vai em
// fatias: um JPEG de 1280 px tem centenas de KB e não pode ser duplicado em
// uma std::string só para montar o corpo. POSSE: `jpeg` continua do chamador,
// não é copiado nem liberado aqui, e precisa continuar válido até o retorno
// (a chamada é bloqueante e pode levar os 120 s do timeout).
//
// `out` só é preenchido quando o resultado é Ok: exige 200, corpo válido pelo
// ParseTurnResponse estrito E o eco `request_id` idêntico ao enviado (eco
// divergente é ParseError — a resposta pode ser de outro turno).
//
// Erros: 401/503 viram Unauthorized/ServiceUnavailable; QUALQUER outro status
// (inclusive 409 e 502, que têm efeito colateral no servidor) vira HttpError
// com `http_status` preservado — a decisão de fluxo é do chamador (F3-T5),
// nunca deste módulo. Nada de retry automático aqui.
PvBackendResult PostTurnPhoto(const std::string& session_id, const std::string& request_id,
                              const uint8_t* jpeg, size_t jpeg_len, PvTurnResponse& out);

// GET de um arquivo de mídia do turno (audio_url/image_url, relativos).
// `expected_mime`/`expected_ext` formam um par fechado: use
// kAudioMime/kAudioExt ou kImageMime/kImageExt. A extensão do CAMINHO é
// verificada ANTES de abrir a conexão; depois, o Content-Type da resposta é
// EXIGIDO (ausente também reprova) e os bytes mágicos são conferidos, para que
// um redirecionamento ou um 200 de página de erro não vire "áudio".
// `out` é substituído; só tem conteúdo quando o resultado é Ok.
PvBackendResult DownloadMedia(const std::string& url_or_path, const char* expected_mime,
                              const char* expected_ext, std::vector<uint8_t>& out);

// Nome estável do resultado, para log/diagnóstico (nunca inclui o token).
const char* StatusName(PvBackendStatus status);

}  // namespace PvBackendClient

#endif  // PV_BACKEND_CLIENT_H
