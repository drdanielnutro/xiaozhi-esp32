#ifndef PV_AUDIO_H
#define PV_AUDIO_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

// Reprodução da resposta do turno: som local de feedback + voz do tutor.
//
// Por que existe uma task própria: a sequência de um turno é
// "toca o feedback -> ESPERA drenar -> toca a voz -> ESPERA drenar -> avisa", e
// as duas esperas somam segundos. `AudioService::PlayPcm` ainda por cima
// BLOQUEIA o chamador enquanto alimenta a fila de playback (até a duração
// inteira do áudio). Nada disso pode rodar na task principal do PvApp (que
// precisa continuar consumindo eventos), na task do LVGL (a tela travaria),
// na task de rede do PvWorker (que estaria segurando o próximo turno) nem em
// callback de áudio (deadlock com as próprias filas). Logo: task dedicada,
// criada uma vez em Start() e nunca destruída, como as demais do PV.
//
// Fronteiras (mesmas do PvWorker e do PvCamera): o PvAudio nunca toca LVGL,
// nunca toca a DeviceStateMachine e nunca faz HTTP. Ele só produz som e avisa.
//
// Contrato de concorrência:
//  - PlayTurnAudio()/PlayFeedback() podem ser chamados de qualquer task; eles
//    apenas guardam o payload e enfileiram um pedido;
//  - cabe UMA resposta de turno por vez. Um pedido novo com outro em andamento
//    é RECUSADO com false — nunca sobrescreve o que está tocando. Quem chama
//    decide o que fazer com o áudio recusado (na F3, o PvApp simplesmente não
//    pede um turno novo antes de VoiceDone/VoiceFailed);
//  - o handler de eventos roda NA TASK DO PvAudio e deve apenas sinalizar
//    (postar na fila do consumidor), como o DoneHandler do PvWorker. Bloquear
//    dentro dele prende a reprodução do próximo turno.
//
// Ciclo de vida: o PvAudio é membro do PvApp (singleton que vive enquanto o
// firmware roda). Por isso não há Stop(): não existe o caso de destruir a task
// com áudio em voo.
class PvAudio {
public:
    enum class Event : uint8_t {
        VoiceDone,    // a voz do turno terminou de sair no alto-falante
        VoiceFailed,  // WAV inválido, PlayPcm recusado ou espera estourada
    };

    // Sons locais da F3 (decisão F3-D1: só estes dois nesta fase).
    enum class Feedback : uint8_t {
        None,     // resposta sem veredicto (ex.: dica) — só a voz
        Correct,  // correct-ding.ogg
        Wrong,    // wrong-neutral.ogg
    };

    using EventHandler = std::function<void(Event)>;

    PvAudio() = default;
    PvAudio(const PvAudio&) = delete;
    PvAudio& operator=(const PvAudio&) = delete;

    // Liga o AudioService (Initialize + Start, SEM wake word e SEM
    // processamento de voz — a F3 não grava nada) e cria a fila e a task.
    // Idempotente. Devolve false quando a placa não tem codec de áudio ou
    // quando algo falha; daí em diante todos os pedidos viram no-op seguro e
    // registrado, como no PvCamera.
    bool Start(EventHandler handler);

    // true quando há codec e a task está no ar.
    bool available() const { return available_; }

    // Toca a resposta completa de um turno: feedback local (se houver), depois
    // a voz contida no WAV. TOMA POSSE de `wav_bytes` em caso de sucesso.
    //
    // false = indisponível ou já havia uma resposta em andamento; nesse caso
    // `wav_bytes` continua com o chamador (nada foi movido) e NENHUM evento
    // será emitido por este pedido.
    //
    // Sucesso aqui significa apenas "aceito"; o fim da voz chega como
    // VoiceDone (ou VoiceFailed) pelo handler, sempre exatamente uma vez.
    bool PlayTurnAudio(std::vector<uint8_t>&& wav_bytes, Feedback feedback);

    // true entre o aceite de PlayTurnAudio() e o evento correspondente.
    bool busy() const { return busy_.load(); }

    // Toca só o som local, sem voz e sem evento. Usado para retorno imediato
    // de UI; não conflita com um turno em andamento além da serialização
    // natural da task (o pedido espera a vez).
    void PlayFeedback(Feedback feedback);

private:
    enum class Job : uint8_t {
        TurnAudio,     // feedback + voz + evento
        FeedbackOnly,  // só o som local
    };

    // Item da fila: POD puro. O WAV (megabytes) NÃO cabe aqui — ele fica em
    // `pending_wav_`, sob `request_mutex_`, e a task o retira movendo, como o
    // PvWorker faz com os resultados de rede.
    struct JobItem {
        Job job;
        Feedback feedback;
    };

    bool Enqueue(Job job, Feedback feedback);
    void Loop();
    void RunTurnAudio(Feedback feedback);
    void PlayFeedbackSound(Feedback feedback);
    // Espera a fila de playback esvaziar. Volta false no estouro do teto
    // defensivo — nenhuma espera pode prender esta task para sempre.
    bool WaitPlaybackDrained(uint32_t timeout_ms);
    void Notify(Event event);

    bool available_ = false;
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    EventHandler handler_;

    // Semáforo binário alimentado por AudioServiceCallbacks::on_playback_drained.
    // O callback pode disparar de VÁRIAS tasks (a de saída, a do codec Opus, ou
    // a própria task do PvAudio dentro do PlayPcm), então ele só dá o semáforo
    // — nenhuma lógica mora lá dentro. A espera ainda faz polling de
    // IsPlaybackIdle() porque o aviso é de BORDA: se o dreno acontecer entre o
    // enfileiramento e o início da espera, não há borda nenhuma para esperar.
    SemaphoreHandle_t drained_ = nullptr;

    // Uma resposta por vez. `busy_` é a porta de entrada (compare_exchange em
    // PlayTurnAudio); o payload correspondente vive no slot abaixo.
    std::atomic<bool> busy_{false};
    std::mutex request_mutex_;
    std::vector<uint8_t> pending_wav_;
};

#endif  // PV_AUDIO_H
