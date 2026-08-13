// FERRAMENTA DE BANCADA DA F2B — ver o cabeçalho pv_uvc_direct.h.
#include "pv_uvc_direct.h"

#include "sdkconfig.h"

#if CONFIG_PV_UVC_DIRECT_SPIKE

#if CONFIG_PV_UVC_SPIKE
#error "CONFIG_PV_UVC_DIRECT_SPIKE e CONFIG_PV_UVC_SPIKE sao mutuamente exclusivos"
#endif

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include "usb/usb_host.h"
#include "usb/uvc_host.h"

#include "pv_photo_dump.h"

#define TAG "PvUvcDirect"

namespace {

// ---------------------------------------------------------------------------
// Parâmetros (decisões D1-D8 da rodada 16, decision-log F2B-T5-Rodada16-*)
// ---------------------------------------------------------------------------

// Mesma escada da rodada 15 (comparabilidade 1:1). O último degrau é OPCIONAL:
// reprovar nele não muda a decisão da fase, só informa o teto real do conjunto.
struct Rung {
    uint32_t width;
    uint32_t height;
    bool optional;
};

constexpr Rung kLadder[] = {
    {800, 600, false},   {1920, 1080, false}, {2592, 1944, false},
    {3264, 2448, false}, {4000, 3000, true},
};
constexpr size_t kRungCount = sizeof(kLadder) / sizeof(kLadder[0]);

// Transporte — EXPERIMENTO E1 da rodada 17 (Codex, thread 019ffc22): URBs de
// UM pacote ISOC (urb_size = 3072 = MPS high-bandwidth 3x1024), reproduzindo
// dentro da pilha 2.5.1 a geometria do mundo que TRANSMITE. Evidência A/B do
// mesmo dia, mesma placa/câmera/conector: usb_host_uvc 2.4.2 (URBs de 1
// pacote, "bug" oficial) = canal vivo (81.798 frame errors no controle
// t5-run17); 2.5.1 com URBs de 4 pacotes (urb_size=0 => 4x MPS) = silêncio
// absoluto (runs 15/16/16b). Se isto transmitir, a causa raiz é a URB
// multi-pacote high-bandwidth no DWC2 do P4 — workaround adotável e issue
// para a Espressif. 8 URBs mantidas; 12/16 só se a bancada pedir.
// EXPERIMENTO E6 (rodada 20): 8 -> 4 URBs. Descoberta do A/B com o controle
// vivo: o glue V4L2 aloca "4 USB transfers ... 1 ISOC packets" e transmite;
// TODAS as nossas execucoes diretas usaram 8 URBs e ficaram mudas — e o
// numero de URBs era a UNICA diferenca de transporte restante entre os dois
// mundos. Hipotese: >4 URBs ISOC em voo estouram silenciosamente o agendador
// periodico do DWC2 (consistente com 100% do historico de bancada).
constexpr int kNumberOfUrbs = 4;
constexpr size_t kUrbSizeOnePacket = 3072;

// Frame buffers: alocados UMA vez no stream_open (uvc_host.c:853-856 — o
// format_select NÃO realoca), então o tamanho precisa caber o PIOR degrau:
// 4000x3000 a w*h/2 = 6 MB (JPEG real fica muito abaixo de 4 bits/px; mesmo
// teto deliberado do spike V4L2). 4 buffers x 6 MB = 24 MB de PSRAM — cabe no
// app do spike, que não tem board/UI/LVGL.
constexpr int kFrameBufferCount = 4;
constexpr size_t kFrameBufferBytes = 4000u * 3000u / 2u;

// Warmup por degrau: o autofoco/exposição são on-camera. Teto DUPLO — 10
// frames OU 3 s, o que vier primeiro (mesmos números de todas as rodadas).
constexpr uint32_t kWarmupFrames = 10;
constexpr int64_t kWarmupBudgetUs = 3 * 1000 * 1000;

// Janela de captura por degrau (rodada 11): a câmera intercala rajadas de
// frames com ERR e, ocasionalmente, um limpo — o driver só entrega os limpos.
constexpr int64_t kCaptureWindowUs = 30 * 1000 * 1000;

// Espera pela enumeração/abertura do stream por ciclo do laço.
constexpr uint32_t kDeviceWaitMs = 10000;
constexpr uint32_t kCycleRetryMs = 15000;

// Câmeras costumam acolchoar o fim do JPEG; aceitar o EOI dentro desta cauda
// evita reprovar frame íntegro por causa do padding.
constexpr size_t kEoiTailBytes = 64;

constexpr uint32_t kTaskStack = 12 * 1024;
// Média-baixa: abaixo da task do driver uvc_host (5) e da task da lib USB
// host (6). O spike passa a vida bloqueado em fila ou vTaskDelay.
constexpr UBaseType_t kTaskPriority = 3;

// ---------------------------------------------------------------------------
// Estado (uma execução por boot; nada disto é reentrante)
// ---------------------------------------------------------------------------

struct RungResult {
    uint32_t applied_width = 0;
    uint32_t applied_height = 0;
    bool applied = false;
    uint32_t bytesused = 0;
    bool pass = false;
    // Sempre um literal estático: o sumário é impresso muito depois e o spike
    // não pode alocar nada só para explicar uma falha.
    const char* reason = "nao-executado";
};

RungResult g_results[kRungCount];

// Tamanhos MJPEG anunciados (uvc_host_get_frame_list). É esta lista — e não um
// chute — que decide se um degrau existe.
struct AdvertisedSize {
    uint32_t width;
    uint32_t height;
};
constexpr size_t kMaxAdvertisedSizes = 32;
AdvertisedSize g_mjpeg_sizes[kMaxAdvertisedSizes];
size_t g_mjpeg_size_count = 0;

// Conexão vista pelo callback do driver (contexto da task do driver).
std::atomic<bool> g_device_seen{false};
std::atomic<uint8_t> g_dev_addr{0};
std::atomic<uint8_t> g_uvc_stream_index{0};
std::atomic<uint32_t> g_frame_info_num{0};

// Telemetria agregada do degrau corrente. Callbacks SÓ tocam nestes atômicos
// (e na fila do frame retido) — nenhum printf, nenhum trabalho pesado.
std::atomic<uint32_t> g_cb_frames{0};
std::atomic<uint32_t> g_cb_bytes{0};
std::atomic<uint32_t> g_cb_empty{0};
std::atomic<uint32_t> g_ev_overflow{0};
std::atomic<uint32_t> g_ev_underflow{0};
std::atomic<uint32_t> g_ev_xfer_err{0};
std::atomic<bool> g_disconnected{false};

// Captura: quando armado, o PRÓXIMO frame com bytes é retido (callback devolve
// false) e o ponteiro segue pela fila para a task validar/dumpar/devolver.
std::atomic<bool> g_capture_armed{false};
QueueHandle_t g_frame_queue = nullptr;

void ResetRungCounters() {
    g_cb_frames.store(0, std::memory_order_relaxed);
    g_cb_bytes.store(0, std::memory_order_relaxed);
    g_cb_empty.store(0, std::memory_order_relaxed);
    g_ev_overflow.store(0, std::memory_order_relaxed);
    g_ev_underflow.store(0, std::memory_order_relaxed);
    g_ev_xfer_err.store(0, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Callbacks (contexto das tasks do driver — mínimos por decisão do decisor)
// ---------------------------------------------------------------------------

void OnDriverEvent(const uvc_host_driver_event_data_t* event, void* user_ctx) {
    (void)user_ctx;
    if (event->type == UVC_HOST_DRIVER_EVENT_DEVICE_CONNECTED) {
        g_dev_addr.store(event->device_connected.dev_addr, std::memory_order_relaxed);
        g_uvc_stream_index.store(event->device_connected.uvc_stream_index,
                                 std::memory_order_relaxed);
        g_frame_info_num.store((uint32_t)event->device_connected.frame_info_num,
                               std::memory_order_relaxed);
        g_device_seen.store(true, std::memory_order_release);
    }
}

bool OnFrame(const uvc_host_frame_t* frame, void* user_ctx) {
    (void)user_ctx;
    if (frame->data_len == 0) {
        g_cb_empty.fetch_add(1, std::memory_order_relaxed);
        return true;  // devolve a posse imediatamente
    }
    g_cb_frames.fetch_add(1, std::memory_order_relaxed);
    g_cb_bytes.fetch_add((uint32_t)frame->data_len, std::memory_order_relaxed);

    bool expected = true;
    if (g_capture_armed.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        const uvc_host_frame_t* handoff = frame;
        if (xQueueSend(g_frame_queue, &handoff, 0) == pdTRUE) {
            return false;  // frame RETIDO; a task devolve com uvc_host_frame_return
        }
        // Fila cheia não deveria acontecer (profundidade 1, um frame em voo);
        // rearma e segue sem reter para não vazar a posse.
        g_capture_armed.store(true, std::memory_order_release);
    }
    return true;
}

void OnStreamEvent(const uvc_host_stream_event_data_t* event, void* user_ctx) {
    (void)user_ctx;
    switch (event->type) {
        case UVC_HOST_TRANSFER_ERROR:
            g_ev_xfer_err.fetch_add(1, std::memory_order_relaxed);
            break;
        case UVC_HOST_FRAME_BUFFER_OVERFLOW:
            g_ev_overflow.fetch_add(1, std::memory_order_relaxed);
            break;
        case UVC_HOST_FRAME_BUFFER_UNDERFLOW:
            g_ev_underflow.fetch_add(1, std::memory_order_relaxed);
            break;
        case UVC_HOST_DEVICE_DISCONNECTED:
            g_disconnected.store(true, std::memory_order_release);
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------------

void PrintMemory(const char* etapa) {
    printf("PV-UVC-MEM etapa=%s psram_livre=%u psram_maior_bloco=%u interna_livre=%u\n", etapa,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    fflush(stdout);
}

void PrintStats(unsigned t_s) {
    printf("PV-UVC-STATS t=%us cb=%u bytes=%u vazios=%u overflow=%u underflow=%u xfer_err=%u\n",
           t_s, (unsigned)g_cb_frames.load(std::memory_order_relaxed),
           (unsigned)g_cb_bytes.load(std::memory_order_relaxed),
           (unsigned)g_cb_empty.load(std::memory_order_relaxed),
           (unsigned)g_ev_overflow.load(std::memory_order_relaxed),
           (unsigned)g_ev_underflow.load(std::memory_order_relaxed),
           (unsigned)g_ev_xfer_err.load(std::memory_order_relaxed));
    fflush(stdout);
}

// SOI nos dois primeiros bytes e EOI terminando o frame (aceito dentro dos
// últimos kEoiTailBytes, porque câmeras acolchoam o fim).
bool ValidateJpeg(const uint8_t* data, size_t len, const char** reason) {
    if (len < 4) {
        *reason = "frame-curto";
        return false;
    }
    if (data[0] != 0xFF || data[1] != 0xD8) {
        *reason = "sem-soi";
        return false;
    }
    const size_t tail = len < kEoiTailBytes ? len : kEoiTailBytes;
    for (size_t back = 2; back <= tail; ++back) {
        const size_t offset = len - back;
        if (data[offset] == 0xFF && data[offset + 1] == 0xD9) {
            printf("PV-UVC-JPEG soi=ok eoi_offset=%u len=%u padding=%u\n", (unsigned)offset,
                   (unsigned)len, (unsigned)(len - offset - 2));
            return true;
        }
    }
    *reason = "sem-eoi";
    return false;
}

// ---------------------------------------------------------------------------
// USB host com ciclo de VBUS (rodada 6 — mantido: inócuo e é o protocolo)
// ---------------------------------------------------------------------------

// ACHADO DA BANCADA (rodada 16): esta task NUNCA pode sair do laço. O padrão
// dos exemplos oficiais (break em NO_CLIENTS/ALL_FREE) serve para SHUTDOWN —
// mas o usbh dispara ALL_FREE incondicionalmente quando o ÚNICO device é
// desplugado e liberado (usbh.c:680-683, num_device==0 em qualquer free).
// Com o break, o unplug da câmera matava o daemon silenciosamente e o replug
// nunca mais enumerava ("sem device" eterno — observado ao vivo; e é uma
// explicação candidata para o "slot-fantasma" das rodadas 7-14). O firmware
// de bancada não tem teardown: só loga os flags e continua.
void UsbLibTask(void* arg) {
    (void)arg;
    while (true) {
        uint32_t event_flags = 0;
        if (usb_host_lib_handle_events(portMAX_DELAY, &event_flags) == ESP_OK) {
            if (event_flags != 0) {
                printf("PV-UVC-USBLIB flags=0x%x (daemon segue vivo)\n", (unsigned)event_flags);
                fflush(stdout);
            }
        }
    }
}

// EXPERIMENTO E2 da rodada 18 (ordem ratificada pelo Codex, thread 019ffc22):
// host instalado JÁ ENERGIZADO, sem o ciclo de VBUS da rodada 6 — igual ao
// exemplo oficial basic_uvc_stream e ao binário de controle b8ed27e (o mundo
// que TRANSMITE). Motivo: com o driver UVC praticamente idêntico entre 2.4.2
// e 2.5.1 (diff trivial), a geometria de URB igualada (E1, run18, mudo) e o
// stack usb 1.4.1 nos dois mundos, o bring-up desenergizado é a diferença
// estrutural restante entre o mundo vivo e o mudo. O ciclo da rodada 6 nunca
// destravou nada (o EN do TPS2051C não é fiado ao P4 — fato da rodada 6);
// removê-lo não perde função, só elimina a variável.
bool InstallUsbHost() {
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .root_port_unpowered = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .enum_filter_cb = nullptr,
        .fifo_settings_custom = {},
        .peripheral_map = 0,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        printf("PV-UVC-VBUS result=FAIL reason=usb_host_install err=0x%x\n", (unsigned)err);
        return false;
    }
    if (xTaskCreate(UsbLibTask, "usb_lib", 4096, nullptr, 6, nullptr) != pdPASS) {
        printf("PV-UVC-VBUS result=FAIL reason=usb_lib_task\n");
        return false;
    }
    printf("PV-UVC-VBUS estado=on-no-boot (E2: sem ciclo; host energizado na instalacao)\n");
    fflush(stdout);
    return true;
}

bool InstallUvcDriver() {
    const uvc_host_driver_config_t driver_config = {
        .driver_task_stack_size = 5 * 1024,
        .driver_task_priority = 5,
        .xCoreID = tskNO_AFFINITY,
        .create_background_task = true,
        .event_cb = OnDriverEvent,
        .user_ctx = nullptr,
    };
    const esp_err_t err = uvc_host_install(&driver_config);
    if (err != ESP_OK) {
        printf("PV-UVC-INIT result=FAIL reason=uvc_host_install err=0x%x\n", (unsigned)err);
        return false;
    }
    printf(
        "PV-UVC-INIT result=OK driver=usb_host_uvc-direto urbs=%d urb_size=4xMPS fb=%d "
        "fb_bytes=%u\n",
        kNumberOfUrbs, kFrameBufferCount, (unsigned)kFrameBufferBytes);
    fflush(stdout);
    return true;
}

// ---------------------------------------------------------------------------
// Enumeração (uvc_host_get_frame_list, sem V4L2)
// ---------------------------------------------------------------------------

const char* FormatName(enum uvc_host_stream_format format) {
    switch (format) {
        case UVC_VS_FORMAT_MJPEG:
            return "MJPEG";
        case UVC_VS_FORMAT_YUY2:
            return "YUY2";
        case UVC_VS_FORMAT_H264:
            return "H264";
        case UVC_VS_FORMAT_H265:
            return "H265";
        // NV12 só existe a partir do usb_host_uvc 2.5.x; o default cobre
        // qualquer formato desconhecido e mantém o código compilável no
        // 2.4.2 (experimento E5-lite da rodada 19).
        default:
            return "DEFAULT";
    }
}

void RememberMjpegSize(uint32_t width, uint32_t height) {
    for (size_t i = 0; i < g_mjpeg_size_count; ++i) {
        if (g_mjpeg_sizes[i].width == width && g_mjpeg_sizes[i].height == height) {
            return;
        }
    }
    if (g_mjpeg_size_count >= kMaxAdvertisedSizes) {
        ESP_LOGW(TAG, "Lista de tamanhos MJPEG cheia (%u); ignorando %ux%u",
                 (unsigned)kMaxAdvertisedSizes, (unsigned)width, (unsigned)height);
        return;
    }
    g_mjpeg_sizes[g_mjpeg_size_count].width = width;
    g_mjpeg_sizes[g_mjpeg_size_count].height = height;
    ++g_mjpeg_size_count;
}

bool IsMjpegSizeAdvertised(uint32_t width, uint32_t height) {
    for (size_t i = 0; i < g_mjpeg_size_count; ++i) {
        if (g_mjpeg_sizes[i].width == width && g_mjpeg_sizes[i].height == height) {
            return true;
        }
    }
    return false;
}

bool EnumerateFrameList() {
    g_mjpeg_size_count = 0;
    size_t list_size = (size_t)g_frame_info_num.load(std::memory_order_relaxed);
    if (list_size == 0 || list_size > 256) {
        printf("PV-UVC-ENUM result=FAIL reason=frame_info_num=%u\n", (unsigned)list_size);
        return false;
    }
    uvc_host_frame_info_t* list = static_cast<uvc_host_frame_info_t*>(
        heap_caps_calloc(list_size, sizeof(uvc_host_frame_info_t), MALLOC_CAP_DEFAULT));
    if (list == nullptr) {
        printf("PV-UVC-ENUM result=FAIL reason=sem-memoria n=%u\n", (unsigned)list_size);
        return false;
    }
    const esp_err_t err =
        uvc_host_get_frame_list(g_dev_addr.load(std::memory_order_relaxed),
                                g_uvc_stream_index.load(std::memory_order_relaxed),
                                reinterpret_cast<uvc_host_frame_info_t(*)[]>(list), &list_size);
    if (err != ESP_OK) {
        printf("PV-UVC-ENUM result=FAIL reason=get_frame_list err=0x%x\n", (unsigned)err);
        heap_caps_free(list);
        return false;
    }
    for (size_t i = 0; i < list_size; ++i) {
        const uvc_host_frame_info_t& info = list[i];
        printf("PV-UVC-ENUM frame index=%u format=%s %ux%u default_interval=%u tipos=%u\n",
               (unsigned)i, FormatName(info.format), (unsigned)info.h_res, (unsigned)info.v_res,
               (unsigned)info.default_interval, (unsigned)info.interval_type);
        if (info.interval_type == 0) {
            printf("PV-UVC-ENUM interval index=%u continuo min=%u max=%u step=%u\n", (unsigned)i,
                   (unsigned)info.interval_min, (unsigned)info.interval_max,
                   (unsigned)info.interval_step);
        } else {
            const unsigned count = info.interval_type < CONFIG_UVC_INTERVAL_ARRAY_SIZE
                                       ? info.interval_type
                                       : CONFIG_UVC_INTERVAL_ARRAY_SIZE;
            for (unsigned k = 0; k < count; ++k) {
                // intervalo em unidades de 100 ns => fps = 1e7/intervalo
                const unsigned fps_x1000 =
                    info.interval[k] > 0 ? (unsigned)(10000000000ULL / (uint64_t)info.interval[k])
                                         : 0;
                printf("PV-UVC-ENUM interval index=%u k=%u valor=%u fps_x1000=%u\n", (unsigned)i, k,
                       (unsigned)info.interval[k], fps_x1000);
            }
        }
        if (info.format == UVC_VS_FORMAT_MJPEG) {
            RememberMjpegSize(info.h_res, info.v_res);
        }
    }
    heap_caps_free(list);
    printf("PV-UVC-ENUM result=OK entradas=%u tamanhos_mjpeg=%u\n", (unsigned)list_size,
           (unsigned)g_mjpeg_size_count);
    fflush(stdout);
    return g_mjpeg_size_count > 0;
}

// ---------------------------------------------------------------------------
// Um degrau da escada
// ---------------------------------------------------------------------------

// Devolve à força qualquer frame que tenha ficado na fila (janela estourada
// entre o CAS do callback e o receive da task): sem isto o close() falharia
// com ESP_ERR_INVALID_STATE ("some frames were not returned").
void DrainRetainedFrames(uvc_host_stream_hdl_t stream) {
    const uvc_host_frame_t* frame = nullptr;
    while (xQueueReceive(g_frame_queue, &frame, 0) == pdTRUE) {
        uvc_host_frame_return(stream, const_cast<uvc_host_frame_t*>(frame));
    }
}

// Executa o degrau. Em PASS devolve, por *out_jpeg, uma cópia do frame em
// PSRAM cuja POSSE passa a ser do chamador (heap_caps_free). O stream chega
// PARADO e sai PARADO; o handle pertence à escada.
bool RunRung(uvc_host_stream_hdl_t stream, const Rung& rung, RungResult* result, uint8_t** out_jpeg,
             size_t* out_len) {
    *out_jpeg = nullptr;
    *out_len = 0;

    uvc_host_stream_format_t format = {
        .h_res = rung.width,
        .v_res = rung.height,
        .fps = 0.0f,  // default do device: S_PARM fora do default emudece a NE-HD362
        .format = UVC_VS_FORMAT_MJPEG,
    };
    esp_err_t err = uvc_host_stream_format_select(stream, &format);
    if (err != ESP_OK) {
        printf("PV-UVC-FMT requested=%ux%u result=FAIL err=0x%x\n", (unsigned)rung.width,
               (unsigned)rung.height, (unsigned)err);
        result->reason = "format-select";
        return false;
    }

    uvc_host_stream_format_t applied = {};
    err = uvc_host_stream_format_get(stream, &applied);
    if (err != ESP_OK) {
        result->reason = "format-get";
        return false;
    }
    result->applied_width = applied.h_res;
    result->applied_height = applied.v_res;
    result->applied = true;
    // fps sai como inteiro x1000: %f depende do newlib completo e não vale
    // arriscar o formato da evidência por causa disso.
    printf("PV-UVC-FMT requested=%ux%u applied=%ux%u fourcc=%s fps_x1000=%u\n",
           (unsigned)rung.width, (unsigned)rung.height, (unsigned)applied.h_res,
           (unsigned)applied.v_res, FormatName(applied.format), (unsigned)(applied.fps * 1000.0f));
    fflush(stdout);

    if (applied.h_res != rung.width || applied.v_res != rung.height) {
        result->reason = "applied-diferente";
        return false;
    }
    if (applied.format != UVC_VS_FORMAT_MJPEG) {
        result->reason = "formato-inesperado";
        return false;
    }

    ResetRungCounters();
    g_capture_armed.store(false, std::memory_order_release);

    err = uvc_host_stream_start(stream);
    if (err != ESP_OK) {
        // Linha própria (não PV-UVC-RUNG): a linha canônica do degrau sai UMA
        // vez, em PrintRungLine — duplicá-la quebraria a contagem do monitor.
        printf("PV-UVC-START requested=%ux%u result=FAIL err=0x%x\n", (unsigned)rung.width,
               (unsigned)rung.height, (unsigned)err);
        result->reason = "stream-start";
        return false;
    }

    // Warmup: descarta os primeiros frames (o callback devolve tudo enquanto
    // desarmado) até 10 frames OU 3 s. Telemetria a cada segundo.
    const int64_t warmup_start_us = esp_timer_get_time();
    unsigned t_s = 0;
    while (esp_timer_get_time() - warmup_start_us < kWarmupBudgetUs &&
           g_cb_frames.load(std::memory_order_relaxed) < kWarmupFrames &&
           !g_disconnected.load(std::memory_order_acquire)) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        PrintStats(++t_s);
    }
    printf("PV-UVC-WARMUP requested=%ux%u descartados=%u\n", (unsigned)rung.width,
           (unsigned)rung.height, (unsigned)g_cb_frames.load(std::memory_order_relaxed));
    fflush(stdout);

    // Janela de captura: arma o callback e espera o frame retido chegar pela
    // fila; frame inválido é devolvido e a captura re-armada até a janela
    // fechar. Stats a cada segundo POR TEMPO (não por iteração: uma rajada de
    // frames inválidos não pode calar a telemetria — achado da revisão).
    bool pass = false;
    const int64_t window_start_us = esp_timer_get_time();
    int64_t last_stats_us = esp_timer_get_time();
    g_capture_armed.store(true, std::memory_order_release);
    while (esp_timer_get_time() - window_start_us < kCaptureWindowUs &&
           !g_disconnected.load(std::memory_order_acquire)) {
        const uvc_host_frame_t* frame = nullptr;
        const bool got = xQueueReceive(g_frame_queue, &frame, pdMS_TO_TICKS(1000)) == pdTRUE;
        if (esp_timer_get_time() - last_stats_us >= 1000000) {
            PrintStats(++t_s);
            last_stats_us = esp_timer_get_time();
        }
        if (!got) {
            continue;
        }
        // uvc_host_frame_return ZERA frame->data_len (uvc_frame_reset, driver
        // 2.5.1): tudo que descreve o frame é copiado ANTES de devolver a
        // posse — achado P0 da revisão independente.
        const size_t frame_len = frame->data_len;
        result->bytesused = (uint32_t)frame_len;
        const char* reason = "ok";
        if (!ValidateJpeg(frame->data, frame_len, &reason)) {
            uvc_host_frame_return(stream, const_cast<uvc_host_frame_t*>(frame));
            result->reason = reason;
            g_capture_armed.store(true, std::memory_order_release);
            continue;
        }
        // Cópia para PSRAM ANTES de devolver a posse ao driver: o dump só
        // roda depois do stop, e o buffer do driver volta ao pool agora.
        uint8_t* copy = static_cast<uint8_t*>(heap_caps_malloc(frame_len, MALLOC_CAP_SPIRAM));
        if (copy == nullptr) {
            ESP_LOGE(TAG, "Sem PSRAM para copiar %u bytes", (unsigned)frame_len);
            uvc_host_frame_return(stream, const_cast<uvc_host_frame_t*>(frame));
            result->reason = "sem-psram";
            break;
        }
        memcpy(copy, frame->data, frame_len);
        uvc_host_frame_return(stream, const_cast<uvc_host_frame_t*>(frame));
        *out_jpeg = copy;
        *out_len = frame_len;
        result->reason = "ok";
        pass = true;
        break;
    }
    g_capture_armed.store(false, std::memory_order_release);

    if (!pass && strcmp(result->reason, "nao-executado") == 0) {
        result->reason = "sem-frame";
    }
    if (!pass && g_disconnected.load(std::memory_order_acquire)) {
        result->reason = "desconectado";
    }

    err = uvc_host_stream_stop(stream);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "stream_stop falhou: err=0x%x", (unsigned)err);
    }
    DrainRetainedFrames(stream);
    PrintStats(++t_s);
    return pass;
}

// ---------------------------------------------------------------------------
// Escada, dump e sumário (mesmos formatos de linha da rodada 15)
// ---------------------------------------------------------------------------

void DumpJpeg(const uint8_t* jpeg, size_t len, uint32_t width, uint32_t height) {
    if (!PvPhotoDump::Request(jpeg, len, static_cast<uint16_t>(width),
                              static_cast<uint16_t>(height))) {
        printf("PV-UVC-DUMP result=FAIL requested=%ux%u (pedido recusado)\n", (unsigned)width,
               (unsigned)height);
        return;
    }
    while (PvPhotoDump::busy()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    printf("PV-UVC-DUMP result=OK requested=%ux%u bytes=%u\n", (unsigned)width, (unsigned)height,
           (unsigned)len);
    fflush(stdout);
}

void PrintRungLine(const Rung& rung, const RungResult& result) {
    const char* fourcc = result.applied ? "JPEG" : "----";
    if (result.pass) {
        printf("PV-UVC-RUNG requested=%ux%u applied=%ux%u fourcc=%s bytesused=%u result=PASS\n",
               (unsigned)rung.width, (unsigned)rung.height, (unsigned)result.applied_width,
               (unsigned)result.applied_height, fourcc, (unsigned)result.bytesused);
    } else {
        printf(
            "PV-UVC-RUNG requested=%ux%u applied=%ux%u fourcc=%s bytesused=%u result=FAIL "
            "reason=%s\n",
            (unsigned)rung.width, (unsigned)rung.height, (unsigned)result.applied_width,
            (unsigned)result.applied_height, fourcc, (unsigned)result.bytesused, result.reason);
    }
    fflush(stdout);
}

void RunLadder(uvc_host_stream_hdl_t stream) {
    for (size_t i = 0; i < kRungCount; ++i) {
        const Rung& rung = kLadder[i];
        RungResult& result = g_results[i];

        if (g_disconnected.load(std::memory_order_acquire)) {
            result.reason = "desconectado";
            PrintRungLine(rung, result);
            continue;
        }
        if (!IsMjpegSizeAdvertised(rung.width, rung.height)) {
            result.pass = false;
            result.reason = "not-advertised";
            PrintRungLine(rung, result);
            continue;
        }

        uint8_t* jpeg = nullptr;
        size_t jpeg_len = 0;
        result.pass = RunRung(stream, rung, &result, &jpeg, &jpeg_len);
        PrintRungLine(rung, result);
        PrintMemory("pos-degrau");

        if (jpeg != nullptr) {
            DumpJpeg(jpeg, jpeg_len, rung.width, rung.height);
            heap_caps_free(jpeg);
        }
    }
}

void PrintSummary() {
    unsigned pass = 0;
    unsigned fail = 0;
    unsigned fail_obrigatorios = 0;

    printf("PV-UVC-SUMARIO inicio\n");
    for (size_t i = 0; i < kRungCount; ++i) {
        const Rung& rung = kLadder[i];
        const RungResult& result = g_results[i];
        const char* fourcc = result.applied ? "JPEG" : "----";
        printf(
            "PV-UVC-SUMARIO degrau=%u requested=%ux%u applied=%ux%u fourcc=%s bytesused=%u "
            "result=%s reason=%s obrigatorio=%s\n",
            (unsigned)(i + 1), (unsigned)rung.width, (unsigned)rung.height,
            (unsigned)result.applied_width, (unsigned)result.applied_height, fourcc,
            (unsigned)result.bytesused, result.pass ? "PASS" : "FAIL", result.reason,
            rung.optional ? "nao" : "sim");
        if (result.pass) {
            ++pass;
        } else {
            ++fail;
            if (!rung.optional) {
                ++fail_obrigatorios;
            }
        }
    }
    printf("PV-UVC-SUMARIO fim pass=%u fail=%u total=%u fail_obrigatorios=%u\n", pass, fail,
           (unsigned)kRungCount, fail_obrigatorios);
    fflush(stdout);
}

[[noreturn]] void IdleForever() {
    printf("PV-UVC-DIRECT fim (a placa fica ociosa; power-cycle para repetir)\n");
    fflush(stdout);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void SpikeTask(void* arg) {
    (void)arg;

    printf("\nPV-UVC-DIRECT inicio (F2B rodada 16, usb_host_uvc sem esp_video) degraus=%u\n",
           (unsigned)kRungCount);
    PrintMemory("boot");

    // Bancada enxerga o driver por dentro (efetivo só com
    // CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y, variante própria). EXCEÇÃO deliberada:
    // "uvc-isoc" fica em INFO — o LOGD por callback ISOC (milhares/s) satura o
    // console a 115200 e perturba o timing do próprio host (lição das rodadas
    // 4-14; o diagnóstico independente apontou o flood como variável nunca
    // controlada). A telemetria desta rodada é agregada: PV-UVC-STATS, 1 l/s.
    esp_log_level_set("uvc", ESP_LOG_DEBUG);
    // E7 (rodada 21): uvc-isoc em DEBUG deliberadamente. Ponto cego achado no
    // A/B: com INFO, completions ISOC de zero bytes sao INVISIVEIS — "canal
    // mudo" e "URBs completando vazias" parecem identicos. O controle vivo
    // enxergava a diferenca porque tinha o flood ligado. O custo do flood e
    // aceito para responder UMA pergunta: ha completions no nosso binario?
    esp_log_level_set("uvc-isoc", ESP_LOG_DEBUG);
    esp_log_level_set("uvc-bulk", ESP_LOG_DEBUG);
    esp_log_level_set("uvc-frame", ESP_LOG_DEBUG);

    g_frame_queue = xQueueCreate(1, sizeof(const uvc_host_frame_t*));
    if (g_frame_queue == nullptr) {
        printf("PV-UVC-INIT result=FAIL reason=fila\n");
        IdleForever();
    }

    if (!InstallUsbHost()) {
        PrintSummary();
        IdleForever();
    }
    if (!InstallUvcDriver()) {
        PrintSummary();
        IdleForever();
    }

    // Escada em LAÇO (protocolo das rodadas 10-15): o firmware NUNCA reinicia;
    // cada replug da câmera dispara enumeração nova e uma escada nova. Com a
    // câmera parada e conectada, os ciclos REPETEM a escada de propósito — é o
    // comportamento observado da rodada 15 (t5-run15-stack251-silencio.log:
    // 8 escadas completas num único boot, sem replug) e mantê-lo preserva a
    // comparabilidade 1:1; a tentativa da bancada é definida pela partida
    // fria, não pela contagem de ciclos.
    for (unsigned ciclo = 1;; ++ciclo) {
        printf("\nPV-UVC-CICLO %u inicio\n", ciclo);
        for (size_t i = 0; i < kRungCount; ++i) {
            g_results[i] = RungResult{};
        }
        g_disconnected.store(false, std::memory_order_release);

        // Espera o driver anunciar um device (replug ou já presente do boot).
        const int64_t wait_start_us = esp_timer_get_time();
        while (!g_device_seen.load(std::memory_order_acquire) &&
               esp_timer_get_time() - wait_start_us < (int64_t)kDeviceWaitMs * 1000) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (!g_device_seen.load(std::memory_order_acquire)) {
            for (size_t i = 0; i < kRungCount; ++i) {
                g_results[i].reason = "sem-device";
            }
            printf("PV-UVC-CICLO %u sem device (replug a camera p/ nova escada; retry %u s)\n",
                   ciclo, (unsigned)(kCycleRetryMs / 1000));
            fflush(stdout);
            vTaskDelay(pdMS_TO_TICKS(kCycleRetryMs));
            continue;
        }
        printf("PV-UVC-ENUM device dev_addr=%u stream_index=%u frames_anunciados=%u\n",
               (unsigned)g_dev_addr.load(std::memory_order_relaxed),
               (unsigned)g_uvc_stream_index.load(std::memory_order_relaxed),
               (unsigned)g_frame_info_num.load(std::memory_order_relaxed));

        if (!EnumerateFrameList()) {
            // Slot-fantasma ou descritor inacessível: exige replug.
            g_device_seen.store(false, std::memory_order_release);
            vTaskDelay(pdMS_TO_TICKS(kCycleRetryMs));
            continue;
        }

        // UM stream_open para a escada inteira: frame buffers de 6 MB cabem o
        // pior degrau (o format_select por degrau não realoca — fato do
        // driver, uvc_host.c:853-856).
        const uvc_host_stream_config_t stream_config = {
            .event_cb = OnStreamEvent,
            .frame_cb = OnFrame,
            .user_ctx = nullptr,
            .usb =
                {
                    .dev_addr = UVC_HOST_ANY_DEV_ADDR,
                    .vid = UVC_HOST_ANY_VID,
                    .pid = UVC_HOST_ANY_PID,
                    .uvc_stream_index = g_uvc_stream_index.load(std::memory_order_relaxed),
                },
            .vs_format =
                {
                    .h_res = kLadder[0].width,
                    .v_res = kLadder[0].height,
                    .fps = 0.0f,
                    .format = UVC_VS_FORMAT_MJPEG,
                },
            .advanced =
                {
                    .number_of_frame_buffers = kFrameBufferCount,
                    .frame_size = kFrameBufferBytes,
                    .frame_heap_caps = MALLOC_CAP_SPIRAM,
                    .number_of_urbs = kNumberOfUrbs,
                    .urb_size = kUrbSizeOnePacket,
                    .user_frame_buffers = nullptr,
                },
        };
        uvc_host_stream_hdl_t stream = nullptr;
        const esp_err_t err =
            uvc_host_stream_open(&stream_config, pdMS_TO_TICKS(kDeviceWaitMs), &stream);
        if (err != ESP_OK) {
            for (size_t i = 0; i < kRungCount; ++i) {
                g_results[i].reason = "stream-open";
            }
            printf(
                "PV-UVC-CICLO %u stream_open falhou err=0x%x (replug p/ nova escada; retry "
                "%u s)\n",
                ciclo, (unsigned)err, (unsigned)(kCycleRetryMs / 1000));
            fflush(stdout);
            // Sem device utilizável: força esperar novo evento de conexão.
            g_device_seen.store(false, std::memory_order_release);
            vTaskDelay(pdMS_TO_TICKS(kCycleRetryMs));
            continue;
        }
        PrintMemory("pos-open");
        // Evidência: descritor completo (device + configuração) no console.
        uvc_host_desc_print(stream);

        RunLadder(stream);

        const esp_err_t close_err = uvc_host_stream_close(stream);
        if (close_err != ESP_OK) {
            ESP_LOGW(TAG, "stream_close falhou: err=0x%x", (unsigned)close_err);
        }
        if (g_disconnected.load(std::memory_order_acquire)) {
            g_device_seen.store(false, std::memory_order_release);
        }
        PrintSummary();
        printf("PV-UVC-CICLO %u fim — REPLUG a camera para disparar nova escada (retry %u s)\n",
               ciclo, (unsigned)(kCycleRetryMs / 1000));
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(kCycleRetryMs));
    }
}

}  // namespace

bool PvUvcDirect::Start() {
    if (xTaskCreate(SpikeTask, "pv_uvc_direct", kTaskStack, nullptr, kTaskPriority, nullptr) !=
        pdPASS) {
        ESP_LOGE(TAG, "Falha ao criar a task do spike direto UVC");
        return false;
    }
    return true;
}

#endif  // CONFIG_PV_UVC_DIRECT_SPIKE
