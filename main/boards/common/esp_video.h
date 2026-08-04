#pragma once
#include "sdkconfig.h"

#include <lvgl.h>
#include <thread>
#include <memory>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "camera.h"
#include "jpg/image_to_jpeg.h"
#include "esp_video_init.h"

struct JpegChunk {
    uint8_t* data;
    size_t len;
};

class EspVideo : public Camera {
private:
    struct FrameBuffer {
        uint8_t *data = nullptr;
        size_t len = 0;
        uint16_t width = 0;
        uint16_t height = 0;
        v4l2_pix_fmt_t format = 0;
    } frame_;
    v4l2_pix_fmt_t sensor_format_ = 0;
#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    uint16_t sensor_width_ = 0;
    uint16_t sensor_height_ = 0;
#endif  // CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    // Resolução real entregue pelo driver, gravada sempre (frame_.width/height
    // ficam trocados quando a rotação opcional está ligada, e as extensões
    // CaptureRaw/AcquirePreviewFrame não rotacionam).
    uint16_t capture_width_ = 0;
    uint16_t capture_height_ = 0;
    int video_fd_ = -1;
    bool streaming_on_ = false;
    struct MmapBuffer { void *start = nullptr; size_t length = 0; };
    std::vector<MmapBuffer> mmap_buffers_;
    std::string explain_url_;
    std::string explain_token_;
    std::thread encoder_thread_;

    // Recursos do caminho de preview (AcquirePreviewFrame). São membros, e não
    // locais, exatamente para que NÃO haja alocação por frame: a 5 fps, abrir e
    // fechar o conversor ou realocar 2,4 MB a cada frame é proibitivo.
    // `preview_convert_handle_` é um esp_imgfx_color_convert_handle_t; fica
    // como void* para manter este header livre dos headers do esp_imgfx.
    void* preview_convert_handle_ = nullptr;
    uint32_t preview_convert_format_ = 0;
    // Só é alocado quando o sensor exige troca de endianness E o formato
    // precisa de conversão de cor: aí o byte-swap não cabe no buffer de
    // destino (que é RGB565) e precisa de um intermediário reutilizável.
    uint8_t* preview_swap_buffer_ = nullptr;
    size_t preview_swap_size_ = 0;

    // Converte um frame do driver para RGB565 LE dentro do buffer do chamador.
    bool ConvertPreviewToRgb565(const uint8_t* src, size_t src_len, uint32_t src_format,
                                uint8_t* dst, size_t dst_len);

public:
    EspVideo(const esp_video_init_config_t& config);
    ~EspVideo();

    virtual void SetExplainUrl(const std::string& url, const std::string& token);
    virtual bool Capture();
    // 翻转控制函数
    virtual bool SetHMirror(bool enabled) override;
    virtual bool SetVFlip(bool enabled) override;
    virtual std::string Explain(const std::string& question);

    // Extensões opcionais da interface Camera (ver camera.h). Não alteram o
    // comportamento de Capture()/Explain(), que seguem sendo o caminho do
    // assistente XiaoZhi.
    virtual bool GetSensorResolution(uint16_t& width, uint16_t& height) override;
    virtual bool CaptureRaw(CameraRawFrame& frame) override;
    virtual bool AcquirePreviewFrame(CameraPreviewFrame& frame) override;
};
