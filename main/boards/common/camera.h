#ifndef CAMERA_H
#define CAMERA_H

#include <cstddef>
#include <cstdint>
#include <string>

// Frame bruto devolvido por Camera::CaptureRaw(), no formato nativo negociado
// com o sensor (sem conversão de cor).
//
// POSSE: `data` é alocado pela implementação em PSRAM e a posse passa para o
// CHAMADOR quando CaptureRaw() devolve true — libere com heap_caps_free(). Se
// CaptureRaw() devolver false, `data` fica nulo e nada precisa ser liberado.
struct CameraRawFrame {
    uint8_t* data = nullptr;  // saída: buffer do chamador (heap_caps_free)
    size_t len = 0;           // saída: bytes válidos em `data`
    uint16_t width = 0;       // saída: largura em pixels
    uint16_t height = 0;      // saída: altura em pixels
    uint32_t format = 0;      // saída: FOURCC V4L2 do conteúdo (ex.: 'YUYV')
};

// Pedido/resposta de um frame de preview já convertido para RGB565 little
// endian (o formato que o LVGL consome direto).
//
// POSSE: `buffer` é do CHAMADOR e precisa ser REUTILIZÁVEL — a implementação
// só escreve dentro dele, nunca aloca nem libera nada por frame. O tamanho
// exigido é width*height*2 do sensor (consulte GetSensorResolution()); se
// `buffer_size` for menor, a chamada falha sem escrever.
struct CameraPreviewFrame {
    uint8_t* buffer = nullptr;  // entrada: destino reutilizável do chamador
    size_t buffer_size = 0;     // entrada: capacidade de `buffer` em bytes
    uint16_t width = 0;         // saída: largura em pixels
    uint16_t height = 0;        // saída: altura em pixels
    size_t len = 0;             // saída: bytes escritos (width*height*2)
};

class Camera {
public:
    virtual void SetExplainUrl(const std::string& url, const std::string& token) = 0;
    virtual bool Capture() = 0;
    virtual bool SetHMirror(bool enabled) = 0;
    virtual bool SetVFlip(bool enabled) = 0;
    virtual bool SetSwapBytes(bool enabled) { return false; }  // Optional, default no-op
    virtual std::string Explain(const std::string& question) = 0;

    // --- Extensões opcionais (default: não suportado) -----------------------
    // Existem para que um aplicativo possa ler frames sem virar dono do device
    // de vídeo nem depender de uma classe concreta de câmera. Implementações
    // que não sabem fazer isso simplesmente devolvem false e o chamador degrada
    // com elegância.
    //
    // Serialização: as três chamadas abaixo NÃO são thread-safe entre si nem
    // com Capture()/Explain(). O chamador deve concentrá-las numa única task.

    // Resolução nativa do frame entregue por CaptureRaw()/AcquirePreviewFrame().
    // Serve para dimensionar os buffers do chamador antes da primeira captura.
    virtual bool GetSensorResolution(uint16_t& width, uint16_t& height) { return false; }

    // Captura um frame FRESCO em resolução plena, no formato nativo do sensor.
    // Ver CameraRawFrame para a posse do buffer.
    virtual bool CaptureRaw(CameraRawFrame& frame) { return false; }

    // Consome UM frame do driver, converte para RGB565 LE dentro do buffer
    // reutilizável do chamador e devolve o buffer do driver imediatamente.
    // Ver CameraPreviewFrame para a posse do buffer.
    virtual bool AcquirePreviewFrame(CameraPreviewFrame& frame) { return false; }
};

#endif  // CAMERA_H
