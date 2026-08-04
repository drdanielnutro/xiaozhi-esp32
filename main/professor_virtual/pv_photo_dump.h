#ifndef PV_PHOTO_DUMP_H
#define PV_PHOTO_DUMP_H

// ===========================================================================
// PROVISÓRIO DA F2 — REMOVER JUNTO COM A FASE.
//
// Caminho canônico de validação off-device da legibilidade da foto (decisão
// F2-LegibilityValidation): despeja no console serial o JPEG REALMENTE
// codificado pelo firmware, em base64, enquadrado por marcadores + tamanho +
// CRC32, para reconstrução e inspeção a 100% no Mac
// (scripts/pv/extract_jpeg_dump.py).
//
// Regras que este módulo existe para respeitar:
//  - NUNCA automático: só roda por acionamento explícito de diagnóstico. A
//    imagem pode conter conteúdo da criança e não vira log normal.
//  - Em STREAMING: o base64 é gerado em blocos pequenos reutilizando um buffer
//    de ~4 KB; nada de uma segunda cópia de centenas de KB do texto.
//  - Fora das tasks quentes: roda numa task one-shot de prioridade baixa, para
//    não bloquear o PvApp, o LVGL nem a câmera.
//  - Imune ao nível de log: escreve com printf/fwrite em stdout, e não com
//    ESP_LOGx (que acrescentaria prefixo por linha e poderia ser filtrado).
//
// Este arquivo é autocontido de propósito: apagar pv_photo_dump.{h,cc}, o
// botão "Exportar (diagnóstico)" da tela de câmera e as três chamadas
// marcadas com "PROVISÓRIO DA F2" no pv_app.cc remove o caminho inteiro.
// ===========================================================================

#include <cstddef>
#include <cstdint>
#include <functional>

namespace PvPhotoDump {

// Roda NA TASK DO DUMP: deve apenas sinalizar (postar na fila do consumidor),
// como os handlers do PvWorker e do PvCamera.
using DoneHandler = std::function<void()>;

void SetDoneHandler(DoneHandler handler);

// Copia `len` bytes do JPEG para um buffer PRÓPRIO (posse do módulo) e dispara
// a task one-shot que imprime o bloco. Chamável de qualquer task.
//
// false quando já há um dump em andamento (duplo acionamento coalescido), a
// entrada é vazia ou a cópia não coube na memória. O chamador continua dono do
// buffer que passou: nada aqui referencia `jpeg` depois do retorno.
bool Request(const uint8_t* jpeg, size_t len, uint16_t width, uint16_t height);

bool busy();

}  // namespace PvPhotoDump

#endif  // PV_PHOTO_DUMP_H
