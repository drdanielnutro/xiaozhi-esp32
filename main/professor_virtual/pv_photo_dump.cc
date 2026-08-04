// PROVISÓRIO DA F2 — ver o cabeçalho pv_photo_dump.h.
#include "pv_photo_dump.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_rom_crc.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>

#include <cinttypes>
#include <cstdio>
#include <mutex>
#include <utility>

#define TAG "PvPhotoDump"

namespace {

// 2880 bytes por bloco NÃO é arbitrário: é múltiplo de 3 (o quantum do base64,
// então nenhum bloco termina com padding no meio do fluxo) e produz 3840
// caracteres, exatamente 32 linhas de 120 colunas. Só o último bloco fecha uma
// linha parcial.
constexpr size_t kBlockBytes = 2880;
constexpr size_t kLineChars = 120;
constexpr size_t kEncodedMax = 4 * ((kBlockBytes + 2) / 3);  // 3840

// A task só faz base64 + fwrite; o buffer de saída (~3,8 KB) vai para o heap,
// não para a pilha.
constexpr uint32_t kStackSize = 3584;

// Prioridade 1: abaixo da câmera (3), do LVGL e do áudio. Um dump de ~400 KB
// leva dezenas de segundos no console e não pode disputar CPU com nada.
constexpr UBaseType_t kPriority = 1;

std::mutex g_mutex;
bool g_busy = false;
// EMPRÉSTIMO: `g_data` aponta para o buffer do chamador (a tela de câmera).
// `g_owns` só vira true se o dono nos entregar a posse com TryHandOff() —
// ver o protocolo em pv_photo_dump.h.
const uint8_t* g_data = nullptr;
bool g_owns = false;
size_t g_len = 0;
uint16_t g_width = 0;
uint16_t g_height = 0;
PvPhotoDump::DoneHandler g_done;

// Fecha o dump, reabre o módulo para um novo pedido e avisa o consumidor.
// Chamada UMA vez por dump, sempre no fim da task (inclusive nos erros).
//
// Só libera o buffer se a posse tiver sido transferida para cá; no caso normal
// (a tela ainda está com a foto) o dono continua sendo ela.
void FinishAndNotify() {
    PvPhotoDump::DoneHandler done;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_owns && g_data != nullptr) {
            // const_cast: o buffer é do heap (heap_caps_malloc na captura) e a
            // posse é nossa agora; o `const` descrevia apenas o empréstimo.
            heap_caps_free(const_cast<uint8_t*>(g_data));
        }
        g_data = nullptr;
        g_owns = false;
        g_len = 0;
        g_width = 0;
        g_height = 0;
        g_busy = false;
        done = g_done;
    }
    if (done) {
        done();
    }
}

void DumpTask(void* arg) {
    (void)arg;

    const uint8_t* data = nullptr;
    size_t len = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        data = g_data;
        len = g_len;
        width = g_width;
        height = g_height;
    }

    if (data == nullptr || len == 0) {
        ESP_LOGE(TAG, "Dump sem dados; nada a exportar");
        FinishAndNotify();
        vTaskDelete(nullptr);
        return;
    }

    // Buffer de saída reutilizado a cada bloco: o texto base64 inteiro (~1,33x
    // o JPEG) NUNCA existe na memória de uma vez.
    char* encoded = static_cast<char*>(heap_caps_malloc(kEncodedMax + 1, MALLOC_CAP_8BIT));
    if (encoded == nullptr) {
        ESP_LOGE(TAG, "Sem memória para o buffer de base64 (%u bytes)", (unsigned)kEncodedMax + 1);
        FinishAndNotify();
        vTaskDelete(nullptr);
        return;
    }

    // esp_rom_crc32_le(0, ...) é o CRC-32 padrão (zlib): o mesmo que o
    // scripts/pv/extract_jpeg_dump.py calcula com zlib.crc32.
    const uint32_t crc = esp_rom_crc32_le(0, data, len);
    const int64_t started_us = esp_timer_get_time();

    // printf/fwrite direto: o dump não pode depender do nível de log nem levar
    // prefixo de tag por linha — o script do host casa linha a linha.
    printf("\nPV-JPEG-BEGIN len=%u crc32=%08" PRIx32 " w=%u h=%u\n", (unsigned)len, crc,
           (unsigned)width, (unsigned)height);

    bool ok = true;
    size_t offset = 0;
    while (offset < len) {
        const size_t chunk = (len - offset) < kBlockBytes ? (len - offset) : kBlockBytes;
        size_t out_len = 0;
        if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(encoded), kEncodedMax + 1,
                                  &out_len, data + offset, chunk) != 0) {
            ok = false;
            break;
        }
        for (size_t i = 0; i < out_len; i += kLineChars) {
            const size_t line = (out_len - i) < kLineChars ? (out_len - i) : kLineChars;
            fwrite(encoded + i, 1, line, stdout);
            fputc('\n', stdout);
        }
        offset += chunk;
        // Um tick por bloco (1 ms nesta build): dá vazão ao console e mantém a
        // task fora do caminho de qualquer outra.
        vTaskDelay(1);
    }

    printf("PV-JPEG-END\n");
    fflush(stdout);
    heap_caps_free(encoded);

    const int64_t elapsed_ms = (esp_timer_get_time() - started_us) / 1000;
    if (ok) {
        ESP_LOGI(TAG, "Dump concluído: %u bytes, crc32 %08" PRIx32 ", %u ms", (unsigned)len, crc,
                 (unsigned)elapsed_ms);
    } else {
        // O bloco sai truncado de propósito: o marcador END e o `len` do BEGIN
        // fazem o script do host recusar o resultado em vez de salvar lixo.
        ESP_LOGE(TAG, "Dump abortado no byte %u de %u", (unsigned)offset, (unsigned)len);
    }

    FinishAndNotify();
    vTaskDelete(nullptr);
}

}  // namespace

void PvPhotoDump::SetDoneHandler(DoneHandler handler) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_done = std::move(handler);
}

bool PvPhotoDump::busy() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_busy;
}

bool PvPhotoDump::TryHandOff(const uint8_t* jpeg) {
    if (jpeg == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    // O ponteiro precisa ser EXATAMENTE o que está sendo lido agora: aceitar a
    // posse de um buffer que não é o do dump corrente faria o dump liberar
    // memória alheia (e o dono da memória de verdade nunca a liberaria).
    if (!g_busy || g_data == nullptr || g_data != jpeg || g_owns) {
        return false;
    }
    g_owns = true;
    ESP_LOGI(TAG, "Posse do JPEG transferida ao dump em andamento");
    return true;
}

bool PvPhotoDump::Request(const uint8_t* jpeg, size_t len, uint16_t width, uint16_t height) {
    if (jpeg == nullptr || len == 0) {
        return false;
    }
    {
        // Reserva a vaga E registra o empréstimo de uma vez: dois toques no
        // botão não podem gerar duas tasks, e a task que vai nascer precisa
        // encontrar o ponteiro já publicado.
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_busy) {
            ESP_LOGW(TAG, "Dump já em andamento; pedido coalescido");
            return false;
        }
        // EMPRÉSTIMO, SEM CÓPIA (decisão de streaming sem cópia extra): o JPEG
        // tem centenas de KB e duplicá-lo em PSRAM só para desacoplar ciclos de
        // vida é caro demais. O desacoplamento vem do handoff de posse — ver o
        // protocolo em pv_photo_dump.h.
        g_busy = true;
        g_owns = false;
        g_data = jpeg;
        g_len = len;
        g_width = width;
        g_height = height;
    }

    if (xTaskCreate(DumpTask, "pv_dump", kStackSize, nullptr, kPriority, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Falha ao criar a task de dump");
        std::lock_guard<std::mutex> lock(g_mutex);
        // Nada foi emprestado de fato: o buffer continua sendo do chamador e
        // não pode ser liberado aqui.
        g_data = nullptr;
        g_owns = false;
        g_len = 0;
        g_width = 0;
        g_height = 0;
        g_busy = false;
        return false;
    }
    return true;
}
