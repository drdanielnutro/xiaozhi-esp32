#include "pv_camera.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <errno.h>
#include <fcntl.h>
#include <freertos/task.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdio>
#include <utility>

#include "esp_video_device.h"
#include "linux/videodev2.h"

#include "board.h"
#include "esp_jpeg_common.h"
#include "esp_jpeg_enc.h"
#include "jpg/image_to_jpeg.h"
#include "jpg/jpeg_to_image.h"

#define TAG "PvCamera"

namespace {

// Três tipos de pedido e coalescência ativa na captura: a fila nunca cresce.
constexpr UBaseType_t kQueueLength = 4;

// 8 KB de pilha (RAM interna). O caminho mais fundo é
// RunCaptureJpeg() -> image_to_jpeg() -> encoder de hardware (ou o fallback
// esp_new_jpeg) e, desde a correção da revisão F2 (P1), também
// jpeg_to_image() -> decodificador. Os dois trabalham com buffers de HEAP, não
// de pilha; o preview é só ioctl + esp_imgfx. A folga extra em relação aos
// 6 KB originais cobre o decodificador que passou a rodar aqui, e custa RAM
// interna irrelevante frente aos megabytes de PSRAM em jogo.
constexpr uint32_t kStackSize = 8192;

// Acima da task de rede (2) e abaixo das tasks de UI/áudio: a câmera precisa
// acordar no ritmo certo, mas nunca à frente do desenho da tela ou do áudio.
constexpr UBaseType_t kPriority = 3;

// 5 fps (decisão F2-Preview): ritmo controlado AQUI, no chamador, e não dentro
// do EspVideo. O intervalo é o alvo entre o início de dois frames; atraso
// acumulado é descartado em vez de virar rajada.
constexpr TickType_t kPreviewIntervalTicks = pdMS_TO_TICKS(200);

// Ordem do proprietário (2026-08-15, supersede o q85 da decisão
// F2-SensorFormat): extrair o potencial MÁXIMO da câmera provisória para
// decidir mantê-la ou descartá-la — 100 no marco de avaliação (o ganho sobre
// 95 concentra-se no ringing em bordas de texto, que é o que a extração lê;
// o custo é só tamanho/tempo). Reduzir depois do veredito é uma linha. O
// outro limitador era a subamostragem de croma — ver EncodeUyvyJpeg422.
constexpr uint8_t kJpegQuality = 100;

// O preview falha em série durante o warm-up de ~5 s do ISP (25 tentativas a
// 5 fps). Loga uma vez a cada 25 falhas para não afogar o console.
constexpr uint32_t kPreviewFailureLogInterval = 25;

// Libera os DOIS buffers de uma captura e zera a estrutura. Ponto único: com a
// posse do JPEG e a do decodificado andando juntas, esquecer uma delas num
// caminho de erro é a forma mais fácil de vazar megabytes de PSRAM.
void FreeCapture(PvCameraCaptureResult& result) {
    if (result.jpeg.data != nullptr) {
        heap_caps_free(result.jpeg.data);
    }
    if (result.rgb != nullptr) {
        heap_caps_free(result.rgb);
    }
    result = PvCameraCaptureResult{};
}

// Codifica um frame UYVY em JPEG com croma 4:2:2, falando direto com o
// esp_new_jpeg. Por que não usar o image_to_jpeg() do core: aquele caminho
// fixa subamostragem 4:2:0, que descarta metade da resolução de cor — e o
// UYVY do ISP já É 4:2:2, então 4:2:2 no JPEG preserva a cor na resolução em
// que ela nasceu (o traço de caneta colorida é onde a diferença aparece;
// 4:4:4 não acrescentaria nada, a fonte não tem mais croma que isso). Vive
// aqui no módulo PV para não alterar o core compartilhado com o upstream.
// Sucesso: devolve o JPEG em buffer de PSRAM cuja posse é do chamador.
bool EncodeUyvyJpeg422(const uint8_t* src, uint16_t width, uint16_t height, uint8_t quality,
                       uint8_t** out, size_t* out_len) {
    const int size = static_cast<int>(width) * static_cast<int>(height) * 2;
    uint8_t* yuyv = static_cast<uint8_t*>(jpeg_calloc_align(size, 16));
    if (yuyv == nullptr) {
        return false;
    }
    // UYVY (Cb Y0 Cr Y1) -> YUYV (Y0 Cb Y1 Cr), o packed 4:2:2 do encoder.
    const uint8_t* s = src;
    uint8_t* d = yuyv;
    for (int i = 0; i < size; i += 4) {
        d[0] = s[1];
        d[1] = s[0];
        d[2] = s[3];
        d[3] = s[2];
        s += 4;
        d += 4;
    }

    jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
    cfg.width = width;
    cfg.height = height;
    cfg.src_type = JPEG_PIXEL_FORMAT_YCbYCr;
    cfg.subsampling = JPEG_SUBSAMPLE_422;
    cfg.quality = quality;
    cfg.rotate = JPEG_ROTATE_0D;
    cfg.task_enable = false;

    jpeg_enc_handle_t handle = nullptr;
    if (jpeg_enc_open(&cfg, &handle) != JPEG_ERR_OK) {
        jpeg_free_align(yuyv);
        return false;
    }

    const size_t out_cap = static_cast<size_t>(width) * height * 3 / 2 + 64 * 1024;
    uint8_t* outbuf =
        static_cast<uint8_t*>(heap_caps_malloc(out_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (outbuf == nullptr) {
        jpeg_enc_close(handle);
        jpeg_free_align(yuyv);
        return false;
    }

    int written = 0;
    const jpeg_error_t ret =
        jpeg_enc_process(handle, yuyv, size, outbuf, static_cast<int>(out_cap), &written);
    jpeg_enc_close(handle);
    jpeg_free_align(yuyv);
    if (ret != JPEG_ERR_OK || written <= 0) {
        heap_caps_free(outbuf);
        return false;
    }
    *out = outbuf;
    *out_len = static_cast<size_t>(written);
    return true;
}

// Linha de base numérica do tuning (pedido do proprietário, 2026-08-15): a
// cada captura, loga os valores EFETIVOS que o pipeline IPA deixou aplicados
// no ISP do P4 — ganhos R/B do AWB, brilho, contraste, saturação e matiz. É o
// insumo para o futuro A/B dessas variáveis: sem medir o ponto de partida,
// não dá para saber onde há ganho real. Leitura pura (G_EXT_CTRLS) no device
// do ISP; o esp_video conta referências no open, então abrir aqui não
// reinicializa nada. Falha vira log e nada mais — telemetria nunca derruba a
// captura.
void LogIspTuningBaseline() {
    static int isp_fd = -2;  // -2 = nunca tentou; -1 = tentou e falhou
    if (isp_fd == -2) {
        isp_fd = open(ESP_VIDEO_ISP1_DEVICE_NAME, O_RDONLY);
        if (isp_fd < 0) {
            ESP_LOGW(TAG, "Telemetria do ISP indisponível (open %s falhou, errno=%d)",
                     ESP_VIDEO_ISP1_DEVICE_NAME, errno);
            isp_fd = -1;
        }
    }
    if (isp_fd < 0) {
        return;
    }
    struct Item {
        uint32_t id;
        const char* name;
    };
    const Item items[] = {
        {V4L2_CID_RED_BALANCE, "red"}, {V4L2_CID_BLUE_BALANCE, "blue"},
        {V4L2_CID_BRIGHTNESS, "bri"},  {V4L2_CID_CONTRAST, "con"},
        {V4L2_CID_SATURATION, "sat"},  {V4L2_CID_HUE, "hue"},
    };
    char line[112];
    size_t used = 0;
    for (const Item& item : items) {
        struct v4l2_ext_control ctrl = {};
        struct v4l2_ext_controls ctrls = {};
        ctrl.id = item.id;
        ctrls.ctrl_class = V4L2_CTRL_CLASS_USER;
        ctrls.count = 1;
        ctrls.controls = &ctrl;
        const bool ok = ioctl(isp_fd, VIDIOC_G_EXT_CTRLS, &ctrls) == 0;
        const int wrote = ok ? std::snprintf(line + used, sizeof(line) - used, " %s=%ld", item.name,
                                             (long)ctrl.value)
                             : std::snprintf(line + used, sizeof(line) - used, " %s=?", item.name);
        if (wrote > 0) {
            used += (size_t)wrote;
        }
        if (used >= sizeof(line)) {
            break;
        }
    }
    // red/blue em milésimos (denominador 1000 do V4L2_CID_RED_BALANCE_DEN).
    ESP_LOGI(TAG, "PV-CAM-ISP%s", line);
}

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
    {
        // Slot limpo ANTES de começar: um resultado órfão de captura anterior
        // (tela fechada + aviso perdido) não pode sobreviver ao início de uma
        // captura nova — se ESTA falhar e o aviso de falha também se perder, o
        // resultado antigo seria confundido com o dela (revisão F2 rodada 2,
        // P1). A reconciliação do PvApp drena órfãos, mas este é o ponto que
        // garante a invariante mesmo se a drenagem ainda não tiver rodado.
        std::lock_guard<std::mutex> lock(capture_mutex_);
        if (capture_.jpeg.data != nullptr || capture_.rgb != nullptr) {
            ESP_LOGW(TAG, "Captura anterior não retirada; descartada antes da nova");
            FreeCapture(capture_);
            capture_ = PvCameraCaptureResult{};
        }
    }

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
    LogIspTuningBaseline();

    const int64_t encode_started_us = esp_timer_get_time();
    // UYVY (o formato real do ISP na 7B) vai pelo caminho 4:2:2 do PV; outro
    // formato qualquer cai no image_to_jpeg do core (4:2:0), que trata todos.
    bool ok;
    if (raw.format == V4L2_PIX_FMT_UYVY) {
        ok =
            EncodeUyvyJpeg422(raw.data, raw.width, raw.height, kJpegQuality, &jpeg_data, &jpeg_len);
    } else {
        ok = image_to_jpeg(raw.data, raw.len, raw.width, raw.height, raw.format, kJpegQuality,
                           &jpeg_data, &jpeg_len);
    }
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

    // DECODIFICAÇÃO AQUI, NA TASK DA CÂMERA (correção da revisão F2, P1). São
    // centenas de milissegundos de CPU e ~2,4 MB de saída: rodar isso na task
    // do PvApp travaria o laço de eventos (rede, health, toques) pelo mesmo
    // tempo. Esta task já é exclusiva com o preview por construção, então o
    // custo cai exatamente onde não atrapalha ninguém.
    PvCameraCaptureResult result;
    size_t rgb_len = 0;
    size_t rgb_width = 0;
    size_t rgb_height = 0;
    size_t rgb_stride = 0;
    const int64_t decode_started_us = esp_timer_get_time();
    const esp_err_t decode_err = jpeg_to_image(jpeg_data, jpeg_len, &result.rgb, &rgb_len,
                                               &rgb_width, &rgb_height, &rgb_stride);
    const int64_t decode_ms = (esp_timer_get_time() - decode_started_us) / 1000;
    if (decode_err != ESP_OK || result.rgb == nullptr || rgb_width == 0 || rgb_height == 0 ||
        rgb_stride == 0) {
        // Sem o decodificado não há o que mostrar à criança: a captura INTEIRA
        // falhou. Não adianta guardar o JPEG — a tela de revisão é o único
        // consumidor dele nesta fase.
        ESP_LOGE(TAG, "Falha ao decodificar a foto para exibição (err %d)", (int)decode_err);
        if (result.rgb != nullptr) {
            heap_caps_free(result.rgb);
            result.rgb = nullptr;
        }
        heap_caps_free(jpeg_data);
        Notify(Event::JpegFailed);
        return;
    }

    result.jpeg.data = jpeg_data;
    result.jpeg.len = jpeg_len;
    result.jpeg.width = width;
    result.jpeg.height = height;
    result.rgb_len = rgb_len;
    result.rgb_width = static_cast<uint16_t>(rgb_width);
    result.rgb_height = static_cast<uint16_t>(rgb_height);
    result.rgb_stride = rgb_stride;

    {
        std::lock_guard<std::mutex> lock(capture_mutex_);
        if (capture_.jpeg.data != nullptr || capture_.rgb != nullptr) {
            // Último-vence: a captura anterior nunca foi retirada. Liberar aqui
            // (os DOIS buffers) é o único jeito de não vazar megabytes de PSRAM.
            ESP_LOGW(TAG, "Captura anterior não retirada; descartada");
            FreeCapture(capture_);
        }
        capture_ = result;  // posse transferida ao slot
    }
    ESP_LOGI(TAG,
             "Foto pronta: %ux%u, %u bytes de JPEG (qualidade %u), codificação %u ms, "
             "decodificação %u ms (%u bytes de RGB565); PSRAM livre %u B, maior bloco %u B",
             (unsigned)width, (unsigned)height, (unsigned)jpeg_len, (unsigned)kJpegQuality,
             (unsigned)encode_ms, (unsigned)decode_ms, (unsigned)rgb_len,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
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

bool PvCamera::TakeCapture(PvCameraCaptureResult& result) {
    std::lock_guard<std::mutex> lock(capture_mutex_);
    if (capture_.jpeg.data == nullptr) {
        return false;
    }
    // Posse dos DOIS buffers transferida ao chamador (heap_caps_free em ambos).
    result = capture_;
    capture_ = PvCameraCaptureResult{};
    return true;
}
