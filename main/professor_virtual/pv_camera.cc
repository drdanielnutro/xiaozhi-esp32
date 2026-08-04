#include "pv_camera.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/task.h>

#include <utility>

#include "board.h"
#include "jpg/image_to_jpeg.h"

#define TAG "PvCamera"

namespace {

// Três tipos de pedido e coalescência ativa na captura: a fila nunca cresce.
constexpr UBaseType_t kQueueLength = 4;

// 6 KB de pilha (RAM interna). O caminho mais fundo é
// RunCaptureJpeg() -> image_to_jpeg() -> encoder de hardware (ou o fallback
// esp_new_jpeg), que trabalham com buffers de heap, não de pilha; o preview é
// só ioctl + esp_imgfx. 4 KB atenderiam, 6 KB deixam folga para os logs de
// erro sem custar RAM interna relevante.
constexpr uint32_t kStackSize = 6144;

// Acima da task de rede (2) e abaixo das tasks de UI/áudio: a câmera precisa
// acordar no ritmo certo, mas nunca à frente do desenho da tela ou do áudio.
constexpr UBaseType_t kPriority = 3;

// 5 fps (decisão F2-Preview): ritmo controlado AQUI, no chamador, e não dentro
// do EspVideo. O intervalo é o alvo entre o início de dois frames; atraso
// acumulado é descartado em vez de virar rajada.
constexpr TickType_t kPreviewIntervalTicks = pdMS_TO_TICKS(200);

// Decisão F2-SensorFormat: qualidade 85 para caderno/A4 legível.
constexpr uint8_t kJpegQuality = 85;

// O preview falha em série durante o warm-up de ~5 s do ISP (25 tentativas a
// 5 fps). Loga uma vez a cada 25 falhas para não afogar o console.
constexpr uint32_t kPreviewFailureLogInterval = 25;

}  // namespace

bool PvCamera::Start(EventHandler handler) {
    if (task_ != nullptr) {
        return available_;
    }
    handler_ = std::move(handler);

    camera_ = Board::GetInstance().GetCamera();
    if (camera_ == nullptr) {
        ESP_LOGW(TAG, "Placa sem câmera; preview e captura ficam desligados");
        return false;
    }
    // Placa com câmera que não implementa as extensões opcionais de camera.h:
    // degrada para no-op em vez de arriscar um dynamic_cast ou um Capture()
    // legado que mexeria no display do assistente.
    if (!camera_->GetSensorResolution(frame_width_, frame_height_) || frame_width_ == 0 ||
        frame_height_ == 0) {
        ESP_LOGW(TAG, "Câmera sem suporte a captura direta; preview e captura ficam desligados");
        return false;
    }
    ESP_LOGI(TAG, "Câmera disponível: %ux%u", (unsigned)frame_width_, (unsigned)frame_height_);

    queue_ = xQueueCreate(kQueueLength, sizeof(Job));
    if (queue_ == nullptr) {
        ESP_LOGE(TAG, "Falha ao criar a fila de pedidos da câmera");
        return false;
    }

    auto entry = [](void* arg) {
        static_cast<PvCamera*>(arg)->Loop();
        vTaskDelete(nullptr);
    };
    if (xTaskCreate(entry, "pv_camera", kStackSize, this, kPriority, &task_) != pdPASS) {
        ESP_LOGE(TAG, "Falha ao criar a task da câmera");
        vQueueDelete(queue_);
        queue_ = nullptr;
        task_ = nullptr;
        return false;
    }

    available_ = true;
    return true;
}

bool PvCamera::Enqueue(Job job) {
    if (!available_ || queue_ == nullptr) {
        return false;
    }
    if (xQueueSend(queue_, &job, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Fila da câmera cheia; pedido %d descartado", static_cast<int>(job));
        return false;
    }
    return true;
}

void PvCamera::StartPreview() {
    if (!Enqueue(Job::PreviewStart)) {
        ESP_LOGD(TAG, "StartPreview ignorado (câmera indisponível)");
    }
}

void PvCamera::StopPreview() { Enqueue(Job::PreviewStop); }

bool PvCamera::RequestCaptureJpeg() {
    if (!available_) {
        ESP_LOGW(TAG, "Captura pedida sem câmera disponível");
        return false;
    }
    bool expected = false;
    if (!capture_in_flight_.compare_exchange_strong(expected, true)) {
        // Coalescido de propósito: um toque repetido no botão não pode
        // empilhar codificações de vários megabytes.
        return false;
    }
    if (!Enqueue(Job::CaptureJpeg)) {
        capture_in_flight_.store(false);
        return false;
    }
    return true;
}

void PvCamera::Loop() {
    TickType_t next_frame = xTaskGetTickCount();
    while (true) {
        // Sem preview, a task dorme indefinidamente na fila; com preview, ela
        // acorda no instante do próximo frame ou antes, se chegar um pedido.
        TickType_t wait = portMAX_DELAY;
        if (preview_active_) {
            const TickType_t now = xTaskGetTickCount();
            const int32_t remaining = static_cast<int32_t>(next_frame - now);
            wait = remaining > 0 ? static_cast<TickType_t>(remaining) : 0;
        }

        Job job = Job::PreviewStop;
        if (xQueueReceive(queue_, &job, wait) == pdTRUE) {
            switch (job) {
                case Job::PreviewStart:
                    if (!preview_active_) {
                        if (EnsurePreviewBuffers()) {
                            preview_active_ = true;
                            next_frame = xTaskGetTickCount();
                            ESP_LOGI(TAG, "Preview ligado");
                        }
                    }
                    break;
                case Job::PreviewStop:
                    if (preview_active_) {
                        preview_active_ = false;
                        ESP_LOGI(TAG, "Preview desligado");
                    }
                    break;
                case Job::CaptureJpeg:
                    // Exclusão mútua com o preview por construção: enquanto
                    // esta chamada não retorna, nenhum frame de preview é
                    // adquirido.
                    RunCaptureJpeg();
                    capture_in_flight_.store(false);
                    next_frame = xTaskGetTickCount();
                    break;
            }
            continue;
        }

        if (preview_active_) {
            GrabPreviewFrame();
            next_frame += kPreviewIntervalTicks;
            const TickType_t now = xTaskGetTickCount();
            if (static_cast<int32_t>(next_frame - now) < 0) {
                next_frame = now;  // atraso acumulado é descartado
            }
        }
    }
}

bool PvCamera::EnsurePreviewBuffers() {
    if (buffers_[0] != nullptr && buffers_[1] != nullptr) {
        return true;
    }
    const size_t size = static_cast<size_t>(frame_width_) * static_cast<size_t>(frame_height_) * 2;
    if (size == 0) {
        return false;
    }
    for (int i = 0; i < 2; i++) {
        if (buffers_[i] != nullptr) {
            continue;
        }
        // Alinhado à linha de cache: o conversor do esp_imgfx pede isso para o
        // caminho otimizado do P4. Alocados uma única vez e nunca liberados —
        // liberar enquanto a tela ainda referencia o frame emprestado seria
        // uso-após-liberação.
        buffers_[i] = static_cast<uint8_t*>(
            heap_caps_aligned_alloc(64, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (buffers_[i] == nullptr) {
            ESP_LOGE(TAG, "Falha ao alocar %u bytes para o buffer de preview %d", (unsigned)size,
                     i);
            return false;
        }
    }
    buffer_size_ = size;
    ESP_LOGI(TAG, "Double-buffer de preview: 2 x %u bytes em PSRAM", (unsigned)size);
    return true;
}

void PvCamera::GrabPreviewFrame() {
    int index = 0;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (display_index_ >= 0) {
            // O consumidor segura um buffer: escreve obrigatoriamente no outro.
            index = 1 - display_index_;
        } else {
            index = ready_index_ == 0 ? 1 : 0;
        }
        writing_index_ = index;
        if (ready_index_ == index) {
            // Vamos sobrescrever o frame que estava pronto: invalida ANTES de
            // escrever, senão o consumidor poderia pegá-lo pela metade.
            ready_index_ = -1;
        }
    }

    CameraPreviewFrame request;
    request.buffer = buffers_[index];
    request.buffer_size = buffer_size_;
    const bool ok = camera_->AcquirePreviewFrame(request);

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        writing_index_ = -1;
        if (ok) {
            ready_index_ = index;
            ready_len_ = request.len;
        }
    }

    if (!ok) {
        if ((preview_failures_++ % kPreviewFailureLogInterval) == 0) {
            ESP_LOGW(TAG, "Frame de preview indisponível (falha %u)", (unsigned)preview_failures_);
        }
        return;
    }
    preview_failures_ = 0;
    Notify(Event::PreviewFrame);
}

void PvCamera::RunCaptureJpeg() {
    CameraRawFrame raw;
    if (!camera_->CaptureRaw(raw)) {
        ESP_LOGE(TAG, "Captura falhou: a câmera não entregou frame");
        Notify(Event::JpegFailed);
        return;
    }

    uint8_t* jpeg_data = nullptr;
    size_t jpeg_len = 0;
    // Medições da decisão F2-SensorFormat: tempo de codificação e pressão de
    // PSRAM a CADA captura. São o insumo da medição física da T6 (tamanho
    // típico por página) e o alarme precoce de fragmentação: o maior bloco
    // livre importa mais que o total, porque a foto e o decodificado da tela
    // de revisão são alocações contíguas de centenas de KB / megabytes.
    const int64_t encode_started_us = esp_timer_get_time();
    const bool ok = image_to_jpeg(raw.data, raw.len, raw.width, raw.height, raw.format,
                                  kJpegQuality, &jpeg_data, &jpeg_len);
    const int64_t encode_ms = (esp_timer_get_time() - encode_started_us) / 1000;
    const uint16_t width = raw.width;
    const uint16_t height = raw.height;
    // Posse do frame bruto era nossa desde o CaptureRaw; o JPEG já foi gerado.
    heap_caps_free(raw.data);
    raw.data = nullptr;

    if (!ok || jpeg_data == nullptr || jpeg_len == 0) {
        ESP_LOGE(TAG, "Codificação JPEG falhou");
        if (jpeg_data != nullptr) {
            heap_caps_free(jpeg_data);
        }
        Notify(Event::JpegFailed);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(jpeg_mutex_);
        if (jpeg_.data != nullptr) {
            // Último-vence: a foto anterior nunca foi retirada. Liberar aqui é
            // o único jeito de não vazar alguns megabytes de PSRAM.
            ESP_LOGW(TAG, "Foto anterior não retirada; descartada");
            heap_caps_free(jpeg_.data);
        }
        jpeg_.data = jpeg_data;
        jpeg_.len = jpeg_len;
        jpeg_.width = width;
        jpeg_.height = height;
    }
    ESP_LOGI(TAG,
             "Foto pronta: %ux%u, %u bytes de JPEG (qualidade %u), codificação %u ms; "
             "PSRAM livre %u B, maior bloco %u B",
             (unsigned)width, (unsigned)height, (unsigned)jpeg_len, (unsigned)kJpegQuality,
             (unsigned)encode_ms, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    Notify(Event::JpegReady);
}

void PvCamera::Notify(Event event) {
    if (handler_) {
        handler_(event);
    }
}

bool PvCamera::AcquireDisplayFrame(PvCameraPreviewFrame& frame) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (display_index_ >= 0) {
        ESP_LOGW(TAG, "AcquireDisplayFrame sem ReleaseDisplayFrame anterior");
        return false;
    }
    if (ready_index_ < 0 || ready_index_ == writing_index_) {
        return false;
    }
    display_index_ = ready_index_;
    frame.data = buffers_[display_index_];
    frame.len = ready_len_;
    frame.width = frame_width_;
    frame.height = frame_height_;
    frame.stride = static_cast<size_t>(frame_width_) * 2;
    return true;
}

void PvCamera::ReleaseDisplayFrame() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    display_index_ = -1;
}

bool PvCamera::TakeJpeg(PvCameraJpeg& jpeg) {
    std::lock_guard<std::mutex> lock(jpeg_mutex_);
    if (jpeg_.data == nullptr) {
        return false;
    }
    jpeg = jpeg_;  // posse transferida ao chamador (heap_caps_free)
    jpeg_ = PvCameraJpeg{};
    return true;
}
