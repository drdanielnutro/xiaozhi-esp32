#ifndef PV_CAMERA_H
#define PV_CAMERA_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>

#include "camera.h"

// Frame de preview EMPRESTADO ao consumidor por AcquireDisplayFrame().
//
// POSSE: o buffer continua sendo do PvCamera. Enquanto o empréstimo estiver
// aberto, a task da câmera não escreve nele; devolva com ReleaseDisplayFrame()
// assim que a tela parar de usá-lo (no LVGL, depois de lv_image_set_src() +
// invalidação, ou quando a imagem for trocada).
struct PvCameraPreviewFrame {
    const uint8_t* data = nullptr;  // RGB565 little endian
    size_t len = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    size_t stride = 0;  // bytes por linha (width * 2)
};

// Foto JPEG pronta.
//
// POSSE: transferida ao chamador — libere `data` com heap_caps_free().
struct PvCameraJpeg {
    uint8_t* data = nullptr;
    size_t len = 0;
    uint16_t width = 0;
    uint16_t height = 0;
};

// Resultado COMPLETO de uma captura, retirado com TakeCapture(): o JPEG (o que
// vai para o backend e para o dump de diagnóstico) e o MESMO quadro já
// decodificado em RGB565 LE (o que a tela de revisão desenha).
//
// Por que os dois vêm juntos: a decodificação por software custa centenas de
// milissegundos e produz ~2,4 MB. Fazê-la na task do PvApp travaria o laço de
// eventos (revisão F2, P1); ela roda na TASK DA CÂMERA, que já é exclusiva com
// o preview por construção, e o par chega pronto do outro lado.
//
// POSSE: os DOIS buffers são transferidos ao chamador — `jpeg.data` e `rgb`
// se liberam com heap_caps_free(). Quem recebe true de TakeCapture() é
// obrigado a liberar ambos em TODOS os caminhos, inclusive nos de erro.
struct PvCameraCaptureResult {
    PvCameraJpeg jpeg;
    uint8_t* rgb = nullptr;  // RGB565 little endian, decodificado do `jpeg`
    size_t rgb_len = 0;
    uint16_t rgb_width = 0;
    uint16_t rgb_height = 0;
    size_t rgb_stride = 0;  // bytes por linha (pode exceder rgb_width * 2)
};

// Motor de câmera do Professor Virtual.
//
// Por que existe: a captura V4L2 é bloqueante e a codificação JPEG de uma
// página inteira custa centenas de milissegundos. Nem a task principal do
// PvApp (que precisa continuar consumindo eventos) nem a task do LVGL podem
// pagar isso. Além disso, os métodos opcionais do `Camera` (CaptureRaw /
// AcquirePreviewFrame) NÃO são thread-safe entre si nem com o Capture()
// legado: esta classe existe também para garantir, por construção, que uma
// ÚNICA task fale com a câmera.
//
// Fronteiras (mesmas do PvWorker): o PvCamera nunca toca LVGL, nunca toca a
// DeviceStateMachine e nunca faz HTTP. Ele só produz pixels e avisa.
//
// Contrato de concorrência:
//  - StartPreview()/StopPreview()/RequestCaptureJpeg() podem ser chamados de
//    qualquer task; eles apenas enfileiram um pedido;
//  - preview e captura JPEG são mutuamente exclusivos porque rodam na mesma
//    task, em sequência: durante a codificação o preview simplesmente não
//    produz frames novos;
//  - no máximo UMA captura fica em voo; pedidos repetidos são coalescidos;
//  - o handler de eventos roda NA TASK DA CÂMERA e deve apenas sinalizar
//    (postar na fila do consumidor), como o DoneHandler do PvWorker.
//
// Ciclo de vida: o PvCamera é um membro do PvApp (singleton que vive enquanto
// o firmware roda). A task e os buffers de preview nunca são destruídos — por
// isso não há Stop(): destruir a task enquanto o consumidor segura um frame
// emprestado seria um uso-após-liberação, e o firmware não tem esse caso.
class PvCamera {
public:
    enum class Event : uint8_t {
        PreviewFrame,  // há frame novo estável; use AcquireDisplayFrame()
        JpegReady,     // há foto pronta; use TakeCapture()
        JpegFailed,    // a captura falhou; nada a retirar
    };

    using EventHandler = std::function<void(Event)>;

    PvCamera() = default;
    PvCamera(const PvCamera&) = delete;
    PvCamera& operator=(const PvCamera&) = delete;

    // Cria a fila e a task da câmera. Idempotente. Devolve false quando a placa
    // não tem câmera ou quando ela não implementa as extensões opcionais — daí
    // em diante todos os pedidos viram no-op seguro e registrado.
    bool Start(EventHandler handler);

    // true quando a câmera existe e expõe as extensões usadas aqui.
    bool available() const { return available_; }

    // Resolução nativa dos frames (0x0 quando indisponível).
    uint16_t frame_width() const { return frame_width_; }
    uint16_t frame_height() const { return frame_height_; }

    // Liga/desliga o loop de preview (~5 fps). Chamáveis de qualquer task.
    void StartPreview();
    void StopPreview();

    // Pede uma foto em resolução plena, codificada em JPEG. false = já havia
    // um pedido em voo (coalescido) ou a câmera não está disponível.
    bool RequestCaptureJpeg();
    bool capture_in_flight() const { return capture_in_flight_.load(); }

    // Empresta o frame estável mais recente. false quando ainda não há frame
    // novo ou quando um empréstimo anterior não foi devolvido.
    bool AcquireDisplayFrame(PvCameraPreviewFrame& frame);
    void ReleaseDisplayFrame();

    // Retira a captura pronta (JPEG + RGB565 decodificado), transferindo a
    // posse dos DOIS buffers. false quando não há nada novo.
    //
    // Também é o ponto de RECONCILIAÇÃO do PvApp: um aviso de conclusão que
    // não coube na fila de eventos não perde a foto — ela continua aqui até
    // alguém retirá-la (revisão F2, P1).
    bool TakeCapture(PvCameraCaptureResult& result);

private:
    enum class Job : uint8_t {
        PreviewStart,
        PreviewStop,
        CaptureJpeg,
    };

    bool Enqueue(Job job);
    void Loop();
    bool EnsurePreviewBuffers();
    void GrabPreviewFrame();
    void RunCaptureJpeg();
    void Notify(Event event);

    Camera* camera_ = nullptr;
    bool available_ = false;
    uint16_t frame_width_ = 0;
    uint16_t frame_height_ = 0;

    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    EventHandler handler_;

    // Só a task da câmera lê/escreve.
    bool preview_active_ = false;
    uint32_t preview_failures_ = 0;

    // Double-buffer de preview em PSRAM, alocado uma única vez (nunca por
    // frame). Esquema de sincronização, todo sob `state_mutex_`:
    //  - `writing_index_`: buffer que a task está preenchendo AGORA; enquanto
    //    isso, ele não é publicável nem emprestável;
    //  - `ready_index_`: último frame COMPLETO (último-frame-vence);
    //  - `display_index_`: buffer emprestado ao consumidor.
    // A task escolhe o índice de escrita evitando `display_index_` e, quando
    // precisa reusar o buffer que estava pronto, zera `ready_index_` ANTES de
    // escrever. Assim o consumidor nunca recebe um buffer pela metade, e o
    // buffer emprestado nunca é sobrescrito. A conversão em si (2,4 MB) roda
    // FORA do mutex — ele só protege a troca de índices.
    std::mutex state_mutex_;
    uint8_t* buffers_[2] = {nullptr, nullptr};
    size_t buffer_size_ = 0;
    int writing_index_ = -1;
    int ready_index_ = -1;
    int display_index_ = -1;
    size_t ready_len_ = 0;

    std::atomic<bool> capture_in_flight_{false};

    // Slot de UMA captura pronta (JPEG + decodificado). Último-vence: publicar
    // por cima de um resultado não retirado libera os dois buffers antigos.
    std::mutex capture_mutex_;
    PvCameraCaptureResult capture_;
};

#endif  // PV_CAMERA_H
