#include "pv_worker.h"

#include <esp_log.h>
#include <freertos/task.h>

#include <utility>

#define TAG "PvWorker"

namespace {

// Três jobs distintos e coalescência ativa (um de cada tipo em voo, no
// máximo): a fila nunca precisa crescer.
constexpr UBaseType_t kQueueLength = 4;

// 8 KB de pilha, mesmo tamanho da task de ativação do assistente
// (application.cc), que também faz HTTP: o caminho mais fundo aqui é
// Open() -> esp_http_client (+ handshake TLS quando a base é https) e, na
// volta, o parse cJSON do corpo de /api/lesson. 6 KB atendem o caso HTTP puro,
// mas não deixam folga para o TLS; 8 KB custam ~2 KB de RAM interna e evitam
// um estouro que só apareceria em campo, com backend HTTPS.
constexpr uint32_t kStackSize = 8192;

// Abaixo das tasks de UI/áudio e igual à task de ativação do assistente: esta
// task passa a vida bloqueada em I/O de rede.
constexpr UBaseType_t kPriority = 2;

}  // namespace

bool PvWorker::Start(DoneHandler handler) {
    if (task_ != nullptr) {
        return true;
    }
    done_handler_ = std::move(handler);

    queue_ = xQueueCreate(kQueueLength, sizeof(JobItem));
    if (queue_ == nullptr) {
        ESP_LOGE(TAG, "Falha ao criar a fila de pedidos do worker");
        return false;
    }

    auto entry = [](void* arg) {
        static_cast<PvWorker*>(arg)->Loop();
        vTaskDelete(nullptr);
    };
    if (xTaskCreate(entry, "pv_worker", kStackSize, this, kPriority, &task_) != pdPASS) {
        ESP_LOGE(TAG, "Falha ao criar a task do worker");
        vQueueDelete(queue_);
        queue_ = nullptr;
        task_ = nullptr;
        return false;
    }
    return true;
}

bool PvWorker::Enqueue(Job job, uint32_t generation, std::atomic<bool>& in_flight) {
    if (queue_ == nullptr) {
        return false;
    }
    bool expected = false;
    if (!in_flight.compare_exchange_strong(expected, true)) {
        // Já existe um pedido igual em voo: coalescido de propósito, para o
        // tick de 10 s nunca empilhar chamadas de 15 s.
        return false;
    }
    JobItem item{job, generation};
    if (xQueueSend(queue_, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Fila do worker cheia; pedido %d descartado", static_cast<int>(job));
        in_flight.store(false);
        return false;
    }
    return true;
}

bool PvWorker::RequestHydrate(uint32_t generation) {
    return Enqueue(Job::Hydrate, generation, hydrate_in_flight_);
}

bool PvWorker::RequestHealth(uint32_t generation) {
    return Enqueue(Job::HealthCheck, generation, health_in_flight_);
}

bool PvWorker::RequestTurnPhoto(uint32_t generation, std::string session_id, std::string request_id,
                                std::vector<uint8_t>&& jpeg) {
    if (queue_ == nullptr || session_id.empty() || request_id.empty() || jpeg.empty()) {
        return false;
    }
    // A entrada precisa estar no slot ANTES do item entrar na fila (a task
    // pode retirá-lo imediatamente), então o Enqueue genérico não serve: o CAS
    // vem primeiro, o depósito depois, o envio por último.
    bool expected = false;
    if (!turn_in_flight_.compare_exchange_strong(expected, true)) {
        // Um turno por vez, por contrato: o cliente é único/serial. Recusar é
        // o que impede dois request_id vivos ao mesmo tempo.
        ESP_LOGW(TAG, "Turno recusado: já existe um em voo");
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(result_mutex_);
        turn_session_id_ = std::move(session_id);
        turn_request_id_ = std::move(request_id);
        turn_jpeg_ = std::move(jpeg);
        // Resultado antigo não retirado morre aqui: se sobrou um turn_ready_,
        // ele é de um pedido que o PvApp abandonou (mudança de geração).
        turn_ready_ = false;
    }
    JobItem item{Job::Turn, generation};
    if (xQueueSend(queue_, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Fila do worker cheia; turno descartado");
        std::lock_guard<std::mutex> lock(result_mutex_);
        turn_session_id_.clear();
        turn_request_id_.clear();
        std::vector<uint8_t>().swap(turn_jpeg_);
        turn_in_flight_.store(false);
        return false;
    }
    return true;
}

void PvWorker::Loop() {
    while (true) {
        JobItem item{Job::HealthCheck, 0};
        if (xQueueReceive(queue_, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (item.job) {
            case Job::Hydrate:
                RunHydrate(item.generation);
                hydrate_in_flight_.store(false);
                break;
            case Job::HealthCheck:
                RunHealth(item.generation);
                health_in_flight_.store(false);
                break;
            case Job::Turn:
                RunTurn(item.generation);
                turn_in_flight_.store(false);
                break;
        }
        if (done_handler_) {
            done_handler_(item.job);
        }
    }
}

void PvWorker::RunHydrate(uint32_t generation) {
    // §9.1 manda health, depois state + lesson. O cliente de referência faz as
    // duas leituras em paralelo; aqui elas são sequenciais na mesma task, o que
    // custa uma ida e volta a mais e economiza uma segunda task de rede.
    HydrationResult result;
    result.generation = generation;
    PvSessionState state;
    PvLesson lesson;

    result.stage = Stage::Health;
    result.backend = PvBackendClient::CheckHealth();
    if (result.backend.ok()) {
        result.stage = Stage::State;
        result.backend = PvBackendClient::FetchState(state);
    }
    if (result.backend.ok()) {
        result.stage = Stage::Lesson;
        result.backend = PvBackendClient::FetchLesson(lesson);
    }
    if (result.backend.ok()) {
        result.stage = Stage::Complete;
    } else {
        ESP_LOGW(TAG, "Hidratação parou na etapa %d: %s (HTTP %d)", static_cast<int>(result.stage),
                 PvBackendClient::StatusName(result.backend.status), result.backend.http_status);
    }

    std::lock_guard<std::mutex> lock(result_mutex_);
    hydration_result_ = result;
    hydration_state_ = std::move(state);
    hydration_lesson_ = std::move(lesson);
    hydration_ready_ = true;
}

void PvWorker::RunTurn(uint32_t generation) {
    // Retira a entrada movendo: o JPEG (centenas de KB) tem UMA cópia viva, e
    // ela acompanha o dono da vez — chamador, slot, esta task.
    std::string session_id;
    std::string request_id;
    std::vector<uint8_t> jpeg;
    {
        std::lock_guard<std::mutex> lock(result_mutex_);
        session_id = std::move(turn_session_id_);
        request_id = std::move(turn_request_id_);
        jpeg = std::move(turn_jpeg_);
        turn_session_id_.clear();
        turn_request_id_.clear();
        turn_jpeg_.clear();
    }

    // Decisão F3-D4: UMA tentativa por etapa, nenhum retry automático — nem em
    // 502, nem em timeout. O que cada status significa (409 idempotência, 502
    // efeito colateral, §7.5) é decidido pelo PvApp com o http_status que segue
    // no resultado; este job só executa a cadeia e para na primeira falha.
    TurnResult result;
    result.generation = generation;

    result.stage = TurnStage::Post;
    result.backend = PvBackendClient::PostTurnPhoto(session_id, request_id, jpeg.data(),
                                                    jpeg.size(), result.response);
    // O JPEG já cumpriu o papel; liberá-lo antes dos downloads reduz o pico de
    // PSRAM do turno (a foto e o WAV do tutor nunca coexistem por causa disto).
    std::vector<uint8_t>().swap(jpeg);

    if (result.backend.ok()) {
        result.stage = TurnStage::AudioDownload;
        result.backend =
            PvBackendClient::DownloadMedia(result.response.audio_url, PvBackendClient::kAudioMime,
                                           PvBackendClient::kAudioExt, result.wav);
    }
    if (result.backend.ok()) {
        result.stage = TurnStage::ImageDownload;
        result.backend =
            PvBackendClient::DownloadMedia(result.response.image_url, PvBackendClient::kImageMime,
                                           PvBackendClient::kImageExt, result.image);
    }
    if (result.backend.ok()) {
        result.stage = TurnStage::Complete;
    } else {
        ESP_LOGW(TAG, "Turno parou na etapa %d: %s (HTTP %d)", static_cast<int>(result.stage),
                 PvBackendClient::StatusName(result.backend.status), result.backend.http_status);
    }

    std::lock_guard<std::mutex> lock(result_mutex_);
    turn_result_ = std::move(result);
    turn_ready_ = true;
}

void PvWorker::RunHealth(uint32_t generation) {
    PvBackendResult result = PvBackendClient::CheckHealth();
    std::lock_guard<std::mutex> lock(result_mutex_);
    health_result_ = result;
    health_generation_ = generation;
    health_ready_ = true;
}

bool PvWorker::TakeHydration(HydrationResult& result, PvSessionState& state, PvLesson& lesson) {
    std::lock_guard<std::mutex> lock(result_mutex_);
    if (!hydration_ready_) {
        return false;
    }
    result = hydration_result_;
    state = std::move(hydration_state_);
    lesson = std::move(hydration_lesson_);
    // Move deixa os membros em estado válido porém indefinido: limpa
    // explicitamente para o worker sempre partir de um espelho vazio.
    hydration_state_.Clear();
    hydration_lesson_.Clear();
    hydration_ready_ = false;
    return true;
}

bool PvWorker::TakeTurn(TurnResult& result) {
    std::lock_guard<std::mutex> lock(result_mutex_);
    if (!turn_ready_) {
        return false;
    }
    result = std::move(turn_result_);
    // Move deixa o slot em estado válido porém indefinido: limpa para o
    // próximo turno partir do zero.
    turn_result_ = TurnResult{};
    turn_ready_ = false;
    return true;
}

bool PvWorker::TakeHealth(PvBackendResult& result, uint32_t& generation) {
    std::lock_guard<std::mutex> lock(result_mutex_);
    if (!health_ready_) {
        return false;
    }
    result = health_result_;
    generation = health_generation_;
    health_ready_ = false;
    return true;
}
