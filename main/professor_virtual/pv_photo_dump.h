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
//    de ~4 KB; nada de uma segunda cópia de centenas de KB do texto. Nem do
//    JPEG: o dump trabalha sobre o buffer da tela POR EMPRÉSTIMO (ver o
//    protocolo de posse abaixo), sem duplicar centenas de KB de PSRAM.
//  - Fora das tasks quentes: roda numa task one-shot de prioridade baixa, para
//    não bloquear o PvApp, o LVGL nem a câmera.
//  - Imune ao nível de log: escreve com printf/fwrite em stdout, e não com
//    ESP_LOGx (que acrescentaria prefixo por linha e poderia ser filtrado).
//
// Este arquivo é autocontido de propósito: apagar pv_photo_dump.{h,cc}, o
// botão "Exportar (diagnóstico)" da tela de câmera, o TryHandOff() do
// ReleasePhotoLocked (em pv_camera_screen.cc) e as chamadas marcadas com
// "PROVISÓRIO DA F2" no pv_app.cc remove o caminho inteiro.
// ===========================================================================

#include <cstddef>
#include <cstdint>
#include <functional>

namespace PvPhotoDump {

// Roda NA TASK DO DUMP: deve apenas sinalizar (postar na fila do consumidor),
// como os handlers do PvWorker e do PvCamera.
using DoneHandler = std::function<void()>;

void SetDoneHandler(DoneHandler handler);

// ---------------------------------------------------------------------------
// PROTOCOLO DE POSSE DO JPEG (empréstimo com transferência condicional)
//
// Request() NÃO copia o JPEG: guarda o PONTEIRO do chamador e lê dele durante
// todo o dump (dezenas de segundos). Enquanto isso, o dono continua sendo o
// chamador — na prática, a PvCameraScreen.
//
// Quando o dono for soltar o buffer (a criança tocou "Nova foto"/"Voltar", ou
// uma rota nova entrou por cima), ele NÃO pode simplesmente liberá-lo: chama
// TryHandOff(ponteiro) antes. Se um dump ainda estiver ativo COM ESSE MESMO
// ponteiro, a posse passa para cá e quem chama heap_caps_free() é o dump, no
// fim da task; TryHandOff devolve true e o dono só zera a referência dele. Se
// devolver false (nenhum dump, dump de outro buffer, ou dump já concluído — e
// nesse caso ele já parou de ler), a liberação é do dono, como sempre.
//
// Ordem garantida pelo mesmo mutex nos dois lados: ou o handoff chega antes do
// fim do dump (o dump libera), ou chega depois (o dono libera). Não existe
// janela em que os dois liberem ou em que ninguém libere.
// ---------------------------------------------------------------------------

// Guarda o ponteiro do JPEG (EMPRÉSTIMO, sem cópia) e dispara a task one-shot
// que imprime o bloco. Chamável de qualquer task.
//
// false quando já há um dump em andamento (duplo acionamento coalescido), a
// entrada é vazia ou a task não pôde ser criada. Em qualquer um desses casos
// nada foi emprestado e o chamador segue dono absoluto do buffer.
bool Request(const uint8_t* jpeg, size_t len, uint16_t width, uint16_t height);

// Oferece a posse de `jpeg` ao dump em andamento. Ver o protocolo acima.
bool TryHandOff(const uint8_t* jpeg);

bool busy();

}  // namespace PvPhotoDump

#endif  // PV_PHOTO_DUMP_H
