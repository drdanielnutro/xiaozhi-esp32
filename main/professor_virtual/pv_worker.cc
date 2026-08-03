#include "pv_worker.h"

#include <esp_log.h>
#include <freertos/task.h>

#include <utility>

#define TAG "PvWorker"

namespace {

// Dois jobs distintos e coalescência ativa: a fila nunca precisa crescer.
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

    queue_ = xQueueCreate(kQueueLength, sizeof(Job));
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

bool PvWorker::Enqueue(Job job, std::atomic<bool>& in_flight) {
    if (queue_ == nullptr) {
        return false;
    }
    bool expected = false;
    if (!in_flight.compare_exchange_strong(expected, true)) {
        // Já existe um pedido igual em voo: coalescido de propósito, para o
        // tick de 10 s nunca empilhar chamadas de 15 s.
        return false;
    }
    if (xQueueSend(queue_, &job, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Fila do worker cheia; pedido %d descartado", static_cast<int>(job));
        in_flight.store(false);
        return false;
    }
    return true;
}

bool PvWorker::RequestHydrate() { return Enqueue(Job::Hydrate, hydrate_in_flight_); }

bool PvWorker::RequestHealth() { return Enqueue(Job::HealthCheck, health_in_flight_); }

void PvWorker::Loop() {
    while (true) {
        Job job = Job::HealthCheck;
        if (xQueueReceive(queue_, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (job) {
            case Job::Hydrate:
                RunHydrate();
                hydrate_in_flight_.store(false);
                break;
            case Job::HealthCheck:
                RunHealth();
                health_in_flight_.store(false);
                break;
        }
        if (done_handler_) {
            done_handler_(job);
        }
    }
}

void PvWorker::RunHydrate() {
    // §9.1 manda health, depois state + lesson. O cliente de referência faz as
    // duas leituras em paralelo; aqui elas são sequenciais na mesma task, o que
    // custa uma ida e volta a mais e economiza uma segunda task de rede.
    HydrationResult result;
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

void PvWorker::RunHealth() {
    PvBackendResult result = PvBackendClient::CheckHealth();
    std::lock_guard<std::mutex> lock(result_mutex_);
    health_result_ = result;
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

bool PvWorker::TakeHealth(PvBackendResult& result) {
    std::lock_guard<std::mutex> lock(result_mutex_);
    if (!health_ready_) {
        return false;
    }
    result = health_result_;
    health_ready_ = false;
    return true;
}
