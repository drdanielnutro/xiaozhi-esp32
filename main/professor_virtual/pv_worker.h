#ifndef PV_WORKER_H
#define PV_WORKER_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>

#include "pv_backend_client.h"
#include "pv_session_mirror.h"

// Task de rede do Professor Virtual.
//
// Por que existe: TODAS as chamadas do PvBackendClient são bloqueantes (até
// 15 s na hidratação; até 120 s no POST do turno). Elas não podem rodar nem
// na task principal do PvApp (que precisa continuar consumindo eventos de
// rede e de UI) nem na task do LVGL. O worker
// é a única parte do PV que fala HTTP; em troca, ele NUNCA toca LVGL nem a
// DeviceStateMachine — só devolve dado para a task principal decidir.
//
// Contrato de concorrência:
//  - pedidos entram por RequestHydrate()/RequestHealth(), chamáveis de
//    qualquer task (task principal e callback do esp_timer);
//  - no máximo UM pedido de cada tipo fica em voo: pedidos repetidos são
//    coalescidos (a função devolve false e nada é enfileirado);
//  - o resultado NÃO cabe no POD da fila do PvApp (PvSessionState/PvLesson têm
//    vetores e strings). Ele é guardado aqui, sob `result_mutex_`, e a task
//    principal o retira com Take*(), que MOVE o conteúdo para fora do worker
//    (transferência de posse: depois do Take, o slot fica vazio). O evento
//    postado na fila do PvApp é só o aviso de "tem resultado pronto".
class PvWorker {
public:
    enum class Job : uint8_t {
        Hydrate,      // health -> state -> lesson (§9.1)
        HealthCheck,  // só GET /api/health (§9.7)
        Turn,         // POST /api/turn + downloads de mídia (F3, §7.5)
    };

    // Em que etapa o ciclo de hidratação parou; serve para log e para a
    // mensagem pt-BR na tela de status.
    enum class Stage : uint8_t {
        Health,
        State,
        Lesson,
        Complete,
    };

    struct HydrationResult {
        PvBackendResult backend;  // etapa que interrompeu (ou Ok)
        Stage stage = Stage::Complete;
        // Geração de conectividade do PvApp no momento do PEDIDO. A task
        // principal descarta resultados de uma geração anterior (revisão F1
        // P1: um resultado em voo durante a desconexão não pode "reviver" o
        // estado Online com dado velho).
        uint32_t generation = 0;
    };

    // Em que etapa o turno parou. O contrato distingue "o POST falhou" (o
    // turno pode ou não ter sido aplicado — vale a regra de idempotência) de
    // "o POST passou e uma etapa de mídia falhou" (o turno FOI aplicado e a
    // resposta existe; só a mídia não chegou ou não pôde ser exibida).
    enum class TurnStage : uint8_t {
        Post,
        AudioDownload,
        ImageDownload,
        ImageDecode,
        Complete,
    };

    // Resultado COMPLETO de um turno por foto (decisão F3-D4: um job composto,
    // UMA tentativa HTTP por etapa, nenhum retry automático — nem em 502, nem
    // em timeout; 409/502 chegam ao PvApp em backend.http_status e é LÁ que a
    // regra de idempotência do contrato é aplicada). A re-hidratação pós-turno
    // NÃO faz parte do job: o PvApp dispara o Hydrate existente ao reagir.
    //
    // POSSE (só há conteúdo quando stage == Complete):
    //  - `wav` sai por movimento em TakeTurn(), como qualquer vetor;
    //  - `rgb` é um buffer CRU do decodificador de JPEG (heap_caps_free). Ele é
    //    a razão de esta struct ser MOVE-ONLY e ter destrutor: quem consome o
    //    resultado precisa liberá-lo em TODOS os caminhos — inclusive no
    //    descarte por geração obsoleta —, e um `return` esquecido vazaria
    //    megabytes de PSRAM. Com o destrutor, esquecer é impossível; a única
    //    saída sem liberação é ForgetRgb(), usada quando a posse passa
    //    explicitamente para a tela.
    struct TurnResult {
        PvBackendResult backend;  // etapa que interrompeu (ou Ok)
        TurnStage stage = TurnStage::Post;
        uint32_t generation = 0;
        PvTurnResponse response;
        std::vector<uint8_t> wav;

        // Imagem do tutor JÁ DECODIFICADA em RGB565 little endian, pronta para
        // a lv_image_dsc_t da tela. O JPEG bruto não sobrevive ao job: a tela
        // só desenha RGB e manter os dois vivos dobraria o pico de PSRAM.
        uint8_t* rgb = nullptr;
        size_t rgb_len = 0;
        uint16_t rgb_width = 0;
        uint16_t rgb_height = 0;
        size_t rgb_stride = 0;  // bytes por linha (pode exceder width * 2)

        TurnResult() = default;
        ~TurnResult();
        TurnResult(TurnResult&& other) noexcept;
        TurnResult& operator=(TurnResult&& other) noexcept;
        TurnResult(const TurnResult&) = delete;
        TurnResult& operator=(const TurnResult&) = delete;

        // Libera o RGB e zera as medidas. Idempotente.
        void FreeRgb();
        // Abre mão do RGB SEM liberar: só para quem acabou de transferir a
        // posse do buffer a outro dono (a tela de câmera).
        void ForgetRgb();
    };

    // Chamado NA TASK DO WORKER quando um job termina. A implementação deve
    // apenas sinalizar a task principal (postar na fila), nunca bloquear.
    using DoneHandler = std::function<void(Job)>;

    PvWorker() = default;
    PvWorker(const PvWorker&) = delete;
    PvWorker& operator=(const PvWorker&) = delete;

    // Cria a fila e a task. Idempotente; devolve false se algo falhar.
    bool Start(DoneHandler handler);

    // Enfileiram um job. false = já havia um igual em voo (coalescido) ou o
    // worker não está no ar. `generation` é a geração de conectividade do
    // PvApp; volta intacta no resultado para a task principal detectar
    // resultados obsoletos.
    bool RequestHydrate(uint32_t generation);
    bool RequestHealth(uint32_t generation);

    // Enfileira UM turno por foto. `request_id` é o UUID v4 do turno lógico
    // (PvBackendClient::NewRequestId(), gerado pelo chamador, que é quem
    // conhece a fronteira "turno novo vs retransmissão" — o worker nunca
    // decide isso). POSSE: `jpeg` só é movido quando o retorno é true; com
    // false (turno em voo, worker fora do ar ou fila cheia), o buffer continua
    // do chamador e NADA será enviado nem sinalizado por este pedido.
    bool RequestTurnPhoto(uint32_t generation, std::string session_id, std::string request_id,
                          std::vector<uint8_t>&& jpeg);

    // Retiram o resultado pronto (move para fora). false quando não há nada
    // novo — o que acontece, de propósito, quando dois avisos do mesmo job
    // chegam na fila e o segundo já não tem dado a entregar.
    bool TakeHydration(HydrationResult& result, PvSessionState& state, PvLesson& lesson);
    bool TakeHealth(PvBackendResult& result, uint32_t& generation);
    bool TakeTurn(TurnResult& result);

    bool hydrate_in_flight() const { return hydrate_in_flight_.load(); }
    bool health_in_flight() const { return health_in_flight_.load(); }
    bool turn_in_flight() const { return turn_in_flight_.load(); }

    // "Hidratação pendente" de ponta a ponta: em execução OU com resultado
    // pronto que o PvApp ainda não consumiu com TakeHydration(). Existe para a
    // guarda de envio do turno (revisão F3 rodada 2, P1): entre o worker
    // gravar o resultado e a task principal aplicá-lo ao espelho existe uma
    // janela em que `hydrate_in_flight()` já é false, mas o espelho corrente
    // ainda é o ANTIGO — um turno aceito nessa janela usaria sessão/tarefa
    // velhas. O resultado é gravado com `hydration_ready_ = true` ANTES de
    // `hydrate_in_flight_` cair, então esta disjunção não tem vão no meio.
    bool hydration_pending();

private:
    // Item da fila interna: o job e a geração de conectividade do pedido.
    struct JobItem {
        Job job;
        uint32_t generation;
    };

    bool Enqueue(Job job, uint32_t generation, std::atomic<bool>& in_flight);
    void Loop();
    void RunHydrate(uint32_t generation);
    void RunHealth(uint32_t generation);
    void RunTurn(uint32_t generation);

    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    DoneHandler done_handler_;

    std::atomic<bool> hydrate_in_flight_{false};
    std::atomic<bool> health_in_flight_{false};
    std::atomic<bool> turn_in_flight_{false};

    std::mutex result_mutex_;
    bool hydration_ready_ = false;
    HydrationResult hydration_result_;
    PvSessionState hydration_state_;
    PvLesson hydration_lesson_;
    bool health_ready_ = false;
    PvBackendResult health_result_;
    uint32_t health_generation_ = 0;

    // Entrada e saída do turno, ambas sob `result_mutex_` (seções curtas: só
    // movimentação de buffers, nunca HTTP). A ENTRADA vive aqui pela mesma
    // razão da saída: sessão, id e JPEG não cabem no JobItem POD da fila.
    std::string turn_session_id_;
    std::string turn_request_id_;
    std::vector<uint8_t> turn_jpeg_;
    bool turn_ready_ = false;
    TurnResult turn_result_;
};

#endif  // PV_WORKER_H
