// FERRAMENTA DE BANCADA DA F2B — ver o cabeçalho pv_uvc_spike.h.
#include "pv_uvc_spike.h"

#include "sdkconfig.h"

#if CONFIG_PV_UVC_SPIKE

#if !CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE
#error "CONFIG_PV_UVC_SPIKE exige CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE=y"
#endif

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cassert>
#include <cstdio>
#include <cstring>

#include "esp_video_device.h"
#include "esp_video_init.h"
#include "esp_video_ioctl.h"
#include "linux/videodev2.h"

#include "pv_photo_dump.h"

#define TAG "PvUvcSpike"

// O nome do device UVC ("/dev/video40") vem de um array que o esp_video exporta
// em C, e o macro ESP_VIDEO_USB_UVC_DEVICE_NAME() injeta o `extern` desse array
// NO ESCOPO EM QUE É USADO. Dentro de um namespace anônimo esse `extern`
// declararia um símbolo de linkage INTERNA (o link quebra com "undefined
// reference to (anonymous namespace)::esp_video_usb_uvc_device_name"), então a
// única chamada ao macro mora aqui, no escopo global, onde a declaração casa
// com a definição do componente. `static` mantém a função restrita a este .cc.
static const char* PvUvcSpikeDeviceName() { return ESP_VIDEO_USB_UVC_DEVICE_NAME(0); }

namespace {

// ---------------------------------------------------------------------------
// Parâmetros do spike (decisões da T2 registradas no decision-log F2B-Spike*)
// ---------------------------------------------------------------------------

// Escada FIXA: o spike NÃO varre tudo o que a câmera anuncia, mede os degraus
// que interessam à extração do manuscrito. O último é OPCIONAL — reprovar nele
// não muda a decisão da fase, só informa o teto real do conjunto.
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

// Dois buffers V4L2 por degrau: o mínimo que mantém o stream vivo enquanto um
// frame está fora. No pior degrau (4000x3000) são 12 MB cada, 24 MB de PSRAM —
// cabe porque esta build não tem UI, LVGL nem rede.
constexpr uint32_t kBufferCount = 2;

// Descarte antes do frame que vale: o autofoco é on-camera (o caminho UVC não
// expõe V4L2_CID_FOCUS_AUTO) e a exposição também precisa convergir. Teto
// DUPLO — 10 frames OU 3 s, o que vier primeiro — para que uma câmera lenta
// não coma o orçamento da escada inteira.
constexpr int kWarmupFrames = 10;
constexpr int64_t kWarmupBudgetUs = 3 * 1000 * 1000;

// Deadline de cada DQBUF. Sem ele o default é portMAX_DELAY e um degrau que
// não entrega frame trava o spike para sempre, em vez de virar um FAIL.
constexpr int64_t kDqbufTimeoutUs = 5 * 1000 * 1000;

// Câmeras costumam acolchoar o fim do JPEG com alguns bytes; aceitar o EOI
// dentro desta cauda evita reprovar frame íntegro por causa do padding.
constexpr size_t kEoiTailBytes = 64;

// A task só faz I/O de arquivo e printf, mas as structs V4L2 e o caminho de
// ioctl do VFS não são baratos em pilha; 12 KB dá folga confortável.
constexpr uint32_t kTaskStack = 12 * 1024;

// Média-baixa: abaixo da task do device UVC (5), acima da task da lib USB host
// (2). O spike passa a vida bloqueado em DQBUF ou em vTaskDelay, então não
// disputa CPU com o transporte.
constexpr UBaseType_t kTaskPriority = 3;

// ---------------------------------------------------------------------------
// Estado do spike (uma execução por boot; nada disto é reentrante)
// ---------------------------------------------------------------------------

struct RungResult {
    uint32_t applied_width = 0;
    uint32_t applied_height = 0;
    uint32_t fourcc = 0;
    uint32_t bytesused = 0;
    bool pass = false;
    // Sempre um literal estático: o sumário é impresso muito depois e o spike
    // não pode alocar nada só para explicar uma falha.
    const char* reason = "nao-executado";
};

RungResult g_results[kRungCount];

// Tamanhos do formato JPEG que a câmera anunciou na enumeração. É esta lista —
// e não um chute — que decide se um degrau existe: S_FMT exige match exato e
// pedir um modo inexistente só produziria ruído no log.
struct AdvertisedSize {
    uint32_t width;
    uint32_t height;
};

constexpr size_t kMaxAdvertisedSizes = 32;
AdvertisedSize g_jpeg_sizes[kMaxAdvertisedSizes];
size_t g_jpeg_size_count = 0;

// ---------------------------------------------------------------------------
// Utilidades de log
// ---------------------------------------------------------------------------

// FOURCC legível ("JPEG"); "----" enquanto não houve formato aplicado.
void FourccToText(uint32_t fourcc, char out[5]) {
    if (fourcc == 0) {
        memcpy(out, "----", 5);
        return;
    }
    out[0] = static_cast<char>((fourcc >> 0) & 0xFF);
    out[1] = static_cast<char>((fourcc >> 8) & 0xFF);
    out[2] = static_cast<char>((fourcc >> 16) & 0xFF);
    out[3] = static_cast<char>((fourcc >> 24) & 0xFF);
    out[4] = '\0';
}

// PSRAM é o recurso que decide se os degraus grandes cabem (24 MB de buffers em
// 4000x3000): medir antes e depois de cada degrau é evidência da bancada.
void PrintMemory(const char* etapa) {
    printf("PV-UVC-MEM etapa=%s psram_livre=%u psram_maior_bloco=%u interna_livre=%u\n", etapa,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// Inicialização do vídeo
// ---------------------------------------------------------------------------

// SÓ `.usb_uvc`: com `.csi` nulo o laço de detecção de sensores do esp_video
// não roda, o CSI/ISP nunca é tocado e o board continua inexistente.
bool InitVideo() {
    const esp_video_init_usb_uvc_config_t uvc_config = {
        .uvc =
            {
                .uvc_dev_num = 1,
                .task_stack = 4096,
                .task_priority = 5,
                .task_affinity = -1,
            },
        .usb =
            {
                .init_usb_host_lib = true,
                // 0 = periférico USB default do P4 (o High-Speed), que é onde
                // fica a porta USB-A OTG da 7B.
                .peripheral_map = 0,
                .task_stack = 4096,
                .task_priority = 2,
                .task_affinity = -1,
            },
    };

    esp_video_init_config_t config = {};
    config.usb_uvc = &uvc_config;

    const esp_err_t err = esp_video_init(&config);
    if (err != ESP_OK) {
        printf("PV-UVC-INIT result=FAIL reason=esp_video_init err=0x%x\n", (unsigned)err);
        return false;
    }
    printf("PV-UVC-INIT result=OK usb_host=on uvc_dev_num=1\n");
    return true;
}

// ---------------------------------------------------------------------------
// Enumeração prévia (uma vez, device aberto e fechado em seguida)
// ---------------------------------------------------------------------------

void RememberJpegSize(uint32_t width, uint32_t height) {
    for (size_t i = 0; i < g_jpeg_size_count; ++i) {
        if (g_jpeg_sizes[i].width == width && g_jpeg_sizes[i].height == height) {
            return;
        }
    }
    if (g_jpeg_size_count >= kMaxAdvertisedSizes) {
        ESP_LOGW(TAG, "Lista de tamanhos JPEG cheia (%u); ignorando %ux%u",
                 (unsigned)kMaxAdvertisedSizes, (unsigned)width, (unsigned)height);
        return;
    }
    g_jpeg_sizes[g_jpeg_size_count].width = width;
    g_jpeg_sizes[g_jpeg_size_count].height = height;
    ++g_jpeg_size_count;
}

bool IsJpegSizeAdvertised(uint32_t width, uint32_t height) {
    for (size_t i = 0; i < g_jpeg_size_count; ++i) {
        if (g_jpeg_sizes[i].width == width && g_jpeg_sizes[i].height == height) {
            return true;
        }
    }
    return false;
}

void EnumerateIntervals(int fd, uint32_t pixelformat, const char* fourcc, uint32_t width,
                        uint32_t height) {
    for (uint32_t index = 0;; ++index) {
        struct v4l2_frmivalenum frmival = {};
        frmival.index = index;
        frmival.pixel_format = pixelformat;
        frmival.width = width;
        frmival.height = height;
        // O componente usa este campo como buf type na ENTRADA e o sobrescreve
        // com o tipo do intervalo na SAÍDA (ambos valem 1 — coincidência que a
        // API do V4L2 do esp_video carrega).
        frmival.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) != 0) {
            break;
        }
        if (frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
            // fps sai como inteiro x1000: %f depende do newlib completo e não
            // vale arriscar o formato da evidência por causa disso.
            const unsigned num = (unsigned)frmival.discrete.numerator;
            const unsigned den = (unsigned)frmival.discrete.denominator;
            const unsigned fps_x1000 = num > 0 ? (unsigned)((uint64_t)den * 1000 / num) : 0;
            printf(
                "PV-UVC-ENUM interval fourcc=%s %ux%u index=%u tipo=discrete num=%u den=%u "
                "fps_x1000=%u\n",
                fourcc, (unsigned)width, (unsigned)height, (unsigned)index, num, den, fps_x1000);
        } else {
            printf(
                "PV-UVC-ENUM interval fourcc=%s %ux%u index=%u tipo=%u min=%u/%u max=%u/%u "
                "step=%u/%u\n",
                fourcc, (unsigned)width, (unsigned)height, (unsigned)index, (unsigned)frmival.type,
                (unsigned)frmival.stepwise.min.numerator,
                (unsigned)frmival.stepwise.min.denominator,
                (unsigned)frmival.stepwise.max.numerator,
                (unsigned)frmival.stepwise.max.denominator,
                (unsigned)frmival.stepwise.step.numerator,
                (unsigned)frmival.stepwise.step.denominator);
        }
    }
}

void EnumerateSizes(int fd, uint32_t pixelformat, const char* fourcc) {
    for (uint32_t index = 0;; ++index) {
        struct v4l2_frmsizeenum frmsize = {};
        frmsize.index = index;
        frmsize.pixel_format = pixelformat;
        frmsize.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  // ver a nota em EnumerateIntervals
        if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) != 0) {
            break;
        }
        if (frmsize.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
            printf("PV-UVC-ENUM size fourcc=%s index=%u tipo=%u (nao-discreto, ignorado)\n", fourcc,
                   (unsigned)index, (unsigned)frmsize.type);
            continue;
        }
        const uint32_t width = frmsize.discrete.width;
        const uint32_t height = frmsize.discrete.height;
        printf("PV-UVC-ENUM size fourcc=%s index=%u %ux%u\n", fourcc, (unsigned)index,
               (unsigned)width, (unsigned)height);
        if (pixelformat == V4L2_PIX_FMT_JPEG) {
            RememberJpegSize(width, height);
        }
        EnumerateIntervals(fd, pixelformat, fourcc, width, height);
    }
}

void EnumerateFormats(int fd) {
    for (uint32_t index = 0;; ++index) {
        struct v4l2_fmtdesc fmtdesc = {};
        fmtdesc.index = index;
        fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) != 0) {
            break;
        }
        char fourcc[5];
        FourccToText(fmtdesc.pixelformat, fourcc);
        printf("PV-UVC-ENUM fmt index=%u fourcc=%s flags=0x%x desc=\"%s\"\n", (unsigned)index,
               fourcc, (unsigned)fmtdesc.flags, (const char*)fmtdesc.description);
        EnumerateSizes(fd, fmtdesc.pixelformat, fourcc);
    }
}

// Abre o device UMA vez só para descrever a câmera e fecha em seguida: a escada
// faz o seu próprio ciclo open->close por degrau.
bool EnumerateOnce() {
    const char* device = PvUvcSpikeDeviceName();
    printf("PV-UVC-ENUM aguardando enumeracao USB em %s (ate %u ms)\n", device,
           (unsigned)CONFIG_USB_UVC_INIT_TIMEOUT_MS);
    fflush(stdout);

    const int64_t started_us = esp_timer_get_time();
    const int fd = open(device, O_RDWR);
    if (fd < 0) {
        printf("PV-UVC-ENUM result=FAIL reason=open errno=%d(%s)\n", errno, strerror(errno));
        return false;
    }
    printf("PV-UVC-ENUM device aberto em %u ms\n",
           (unsigned)((esp_timer_get_time() - started_us) / 1000));

    struct v4l2_capability cap = {};
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
        printf("PV-UVC-ENUM cap driver=%s card=%s bus=%s caps=0x%08x device_caps=0x%08x\n",
               (const char*)cap.driver, (const char*)cap.card, (const char*)cap.bus_info,
               (unsigned)cap.capabilities, (unsigned)cap.device_caps);
    } else {
        printf("PV-UVC-ENUM querycap falhou errno=%d(%s)\n", errno, strerror(errno));
    }

    EnumerateFormats(fd);
    close(fd);

    printf("PV-UVC-ENUM result=OK tamanhos_jpeg=%u\n", (unsigned)g_jpeg_size_count);
    fflush(stdout);
    return true;
}

// ---------------------------------------------------------------------------
// Um degrau da escada
// ---------------------------------------------------------------------------

// Limpeza defensiva: QUALQUER saída do degrau (inclusive erro no meio) desfaz
// STREAMON, mmap e open na ordem inversa. Sem isto um degrau que falha deixa o
// device ocupado e derruba todos os seguintes.
struct RungSession {
    int fd = -1;
    void* mapped[kBufferCount] = {};
    size_t mapped_len[kBufferCount] = {};
    bool streaming = false;

    ~RungSession() {
        if (streaming) {
            int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (ioctl(fd, VIDIOC_STREAMOFF, &type) != 0) {
                ESP_LOGW(TAG, "STREAMOFF falhou: errno=%d(%s)", errno, strerror(errno));
            }
        }
        for (size_t i = 0; i < kBufferCount; ++i) {
            if (mapped[i] != nullptr) {
                munmap(mapped[i], mapped_len[i]);
            }
        }
        if (fd >= 0) {
            close(fd);
        }
    }
};

bool SetDqbufTimeout(int fd, int64_t timeout_us) {
    struct timeval tv = {};
    tv.tv_sec = static_cast<time_t>(timeout_us / 1000000);
    tv.tv_usec = static_cast<suseconds_t>(timeout_us % 1000000);
    if (ioctl(fd, VIDIOC_S_DQBUF_TIMEOUT, &tv) != 0) {
        ESP_LOGE(TAG, "VIDIOC_S_DQBUF_TIMEOUT falhou: errno=%d(%s)", errno, strerror(errno));
        return false;
    }
    return true;
}

// SOI nos dois primeiros bytes e EOI terminando o frame (aceito dentro dos
// últimos kEoiTailBytes, porque câmeras acolchoam o fim). O offset do EOI vai
// para o log: é ele que explica um JPEG que abre mas fecha truncado.
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

// Executa o degrau inteiro. Em PASS devolve, por *out_jpeg, uma cópia do frame
// em PSRAM cuja POSSE passa a ser do chamador (heap_caps_free). Em FAIL nada é
// devolvido e o degrau seguinte segue normalmente.
bool RunRung(const Rung& rung, RungResult* result, uint8_t** out_jpeg, size_t* out_len) {
    *out_jpeg = nullptr;
    *out_len = 0;

    RungSession session;
    session.fd = open(PvUvcSpikeDeviceName(), O_RDWR);
    if (session.fd < 0) {
        ESP_LOGE(TAG, "open falhou: errno=%d(%s)", errno, strerror(errno));
        result->reason = "open";
        return false;
    }

    struct v4l2_format format = {};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = rung.width;
    format.fmt.pix.height = rung.height;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
    // sizeimage=0 de propósito: o componente então dimensiona o buffer como
    // w*h bytes, teto seguro para qualquer JPEG deste degrau (um frame maior
    // que o buffer viraria evento de OVERFLOW e descarte limpo, não corrupção).
    format.fmt.pix.sizeimage = 0;
    if (ioctl(session.fd, VIDIOC_S_FMT, &format) != 0) {
        ESP_LOGE(TAG, "VIDIOC_S_FMT falhou: errno=%d(%s)", errno, strerror(errno));
        result->reason = "s-fmt";
        return false;
    }

    struct v4l2_format applied = {};
    applied.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(session.fd, VIDIOC_G_FMT, &applied) != 0) {
        ESP_LOGE(TAG, "VIDIOC_G_FMT falhou: errno=%d(%s)", errno, strerror(errno));
        result->reason = "g-fmt";
        return false;
    }
    result->applied_width = applied.fmt.pix.width;
    result->applied_height = applied.fmt.pix.height;
    result->fourcc = applied.fmt.pix.pixelformat;

    char fourcc[5];
    FourccToText(result->fourcc, fourcc);
    printf("PV-UVC-FMT requested=%ux%u applied=%ux%u fourcc=%s sizeimage=%u\n",
           (unsigned)rung.width, (unsigned)rung.height, (unsigned)result->applied_width,
           (unsigned)result->applied_height, fourcc, (unsigned)applied.fmt.pix.sizeimage);

    if (result->applied_width != rung.width || result->applied_height != rung.height) {
        result->reason = "applied-diferente";
        return false;
    }
    if (result->fourcc != V4L2_PIX_FMT_JPEG) {
        result->reason = "fourcc-inesperado";
        return false;
    }

    struct v4l2_requestbuffers req = {};
    req.count = kBufferCount;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(session.fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(TAG, "VIDIOC_REQBUFS falhou: errno=%d(%s)", errno, strerror(errno));
        result->reason = "reqbufs";
        return false;
    }

    for (uint32_t i = 0; i < kBufferCount; ++i) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(session.fd, VIDIOC_QUERYBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QUERYBUF[%u] falhou: errno=%d(%s)", (unsigned)i, errno,
                     strerror(errno));
            result->reason = "querybuf";
            return false;
        }
        void* start =
            mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, session.fd, buf.m.offset);
        // MAP_FAILED é NULL neste componente; a comparação cobre os dois nomes.
        if (start == MAP_FAILED) {
            ESP_LOGE(TAG, "mmap[%u] falhou (%u bytes)", (unsigned)i, (unsigned)buf.length);
            result->reason = "mmap";
            return false;
        }
        session.mapped[i] = start;
        session.mapped_len[i] = buf.length;
        if (ioctl(session.fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF[%u] falhou: errno=%d(%s)", (unsigned)i, errno,
                     strerror(errno));
            result->reason = "qbuf";
            return false;
        }
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(session.fd, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGE(TAG, "VIDIOC_STREAMON falhou: errno=%d(%s)", errno, strerror(errno));
        result->reason = "streamon";
        return false;
    }
    session.streaming = true;

    // Descarte para o AF/exposição convergirem. Um DQBUF que falha aqui não
    // reprova o degrau sozinho: quem decide é o DQBUF final, logo abaixo.
    const int64_t warmup_deadline_us = esp_timer_get_time() + kWarmupBudgetUs;
    int discarded = 0;
    for (int i = 0; i < kWarmupFrames; ++i) {
        const int64_t remaining_us = warmup_deadline_us - esp_timer_get_time();
        if (remaining_us <= 0) {
            break;
        }
        const int64_t timeout_us = remaining_us < kDqbufTimeoutUs ? remaining_us : kDqbufTimeoutUs;
        if (!SetDqbufTimeout(session.fd, timeout_us)) {
            result->reason = "dqbuf-timeout";
            return false;
        }
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(session.fd, VIDIOC_DQBUF, &buf) != 0) {
            ESP_LOGW(TAG, "DQBUF de descarte %d falhou: errno=%d(%s)", i, errno, strerror(errno));
            break;
        }
        ++discarded;
        if (ioctl(session.fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "QBUF de descarte %d falhou: errno=%d(%s)", i, errno, strerror(errno));
            result->reason = "qbuf-descarte";
            return false;
        }
    }
    printf("PV-UVC-WARMUP requested=%ux%u descartados=%d\n", (unsigned)rung.width,
           (unsigned)rung.height, discarded);

    if (!SetDqbufTimeout(session.fd, kDqbufTimeoutUs)) {
        result->reason = "dqbuf-timeout";
        return false;
    }
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(session.fd, VIDIOC_DQBUF, &buf) != 0) {
        ESP_LOGE(TAG, "VIDIOC_DQBUF falhou: errno=%d(%s)", errno, strerror(errno));
        result->reason = "dqbuf";
        return false;
    }
    result->bytesused = buf.bytesused;

    if (buf.index >= kBufferCount || session.mapped[buf.index] == nullptr) {
        ESP_LOGE(TAG, "DQBUF devolveu índice inválido: %u", (unsigned)buf.index);
        result->reason = "indice-invalido";
        return false;
    }
    if (buf.bytesused == 0) {
        result->reason = "bytesused-zero";
        return false;
    }

    // Cópia para PSRAM ANTES de devolver o buffer ao driver: o frame precisa
    // sobreviver ao STREAMOFF/munmap/close, porque o dump só roda depois.
    uint8_t* copy = static_cast<uint8_t*>(heap_caps_malloc(buf.bytesused, MALLOC_CAP_SPIRAM));
    if (copy == nullptr) {
        ESP_LOGE(TAG, "Sem PSRAM para copiar %u bytes", (unsigned)buf.bytesused);
        result->reason = "sem-psram";
        return false;
    }
    memcpy(copy, session.mapped[buf.index], buf.bytesused);

    if (ioctl(session.fd, VIDIOC_QBUF, &buf) != 0) {
        // Não é fatal: o degrau já tem o frame e a sessão vai ser desmontada.
        ESP_LOGW(TAG, "QBUF final falhou: errno=%d(%s)", errno, strerror(errno));
    }

    const char* reason = "ok";
    if (!ValidateJpeg(copy, buf.bytesused, &reason)) {
        heap_caps_free(copy);
        result->reason = reason;
        return false;
    }

    *out_jpeg = copy;
    *out_len = buf.bytesused;
    result->reason = "ok";
    return true;
}

// ---------------------------------------------------------------------------
// Escada, dump e sumário
// ---------------------------------------------------------------------------

// O dump lê o buffer por EMPRÉSTIMO (protocolo em pv_photo_dump.h) e o dono
// continua sendo esta task: por isso esperamos !busy() antes de liberar. Não há
// TryHandOff aqui porque a task do spike sobrevive ao dump inteiro — e um dump
// de 12 MP a 115200 leva minutos, o que é esperado.
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
    char fourcc[5];
    FourccToText(result.fourcc, fourcc);
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

void RunLadder() {
    for (size_t i = 0; i < kRungCount; ++i) {
        const Rung& rung = kLadder[i];
        RungResult& result = g_results[i];

        // Degrau que a câmera não anuncia não vira S_FMT: o componente exige
        // match exato e devolveria ESP_ERR_INVALID_ARG de qualquer jeito.
        if (!IsJpegSizeAdvertised(rung.width, rung.height)) {
            result.pass = false;
            result.reason = "not-advertised";
            PrintRungLine(rung, result);
            continue;
        }

        uint8_t* jpeg = nullptr;
        size_t jpeg_len = 0;
        result.pass = RunRung(rung, &result, &jpeg, &jpeg_len);
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
        char fourcc[5];
        FourccToText(result.fourcc, fourcc);
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
    printf("PV-UVC-SPIKE fim (a placa fica ociosa; power-cycle para repetir a escada)\n");
    fflush(stdout);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void SpikeTask(void* arg) {
    (void)arg;

    printf("\nPV-UVC-SPIKE inicio (F2B) degraus=%u\n", (unsigned)kRungCount);
    PrintMemory("boot");

    if (!InitVideo()) {
        PrintSummary();
        IdleForever();
    }
    if (!EnumerateOnce()) {
        // Sem device não há o que medir: os degraus ficam com o motivo de
        // fábrica e o sumário diz por que a escada nem começou.
        for (size_t i = 0; i < kRungCount; ++i) {
            g_results[i].reason = "sem-device";
        }
        PrintSummary();
        IdleForever();
    }

    RunLadder();
    PrintSummary();
    IdleForever();
}

}  // namespace

bool PvUvcSpike::Start() {
    if (xTaskCreate(SpikeTask, "pv_uvc_spike", kTaskStack, nullptr, kTaskPriority, nullptr) !=
        pdPASS) {
        ESP_LOGE(TAG, "Falha ao criar a task do spike UVC");
        return false;
    }
    return true;
}

#endif  // CONFIG_PV_UVC_SPIKE
