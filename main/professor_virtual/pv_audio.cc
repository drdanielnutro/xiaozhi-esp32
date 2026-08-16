#include "pv_audio.h"

#include <esp_log.h>

#include <string_view>
#include <utility>

#include "application.h"
#include "audio_service.h"
#include "board.h"
#include "pv_wav.h"

#define TAG "PvAudio"

// Sons locais de feedback, embarcados pelo CMake (GLOB de
// professor_virtual/assets_sounds/*.ogg). Os nomes dos símbolos vêm da regra do
// ESP-IDF para EMBED_FILES: prefixo `_binary_`, nome do arquivo com todo
// caractere não alfanumérico virando `_`, sufixo `_start`/`_end`. Declarados
// como em main/assets/lang_config.h — o rótulo `asm` fixa o símbolo, então o
// mangling do C++ não atrapalha.
extern const char correct_ding_ogg_start[] asm("_binary_correct_ding_ogg_start");
extern const char correct_ding_ogg_end[] asm("_binary_correct_ding_ogg_end");
extern const char wrong_neutral_ogg_start[] asm("_binary_wrong_neutral_ogg_start");
extern const char wrong_neutral_ogg_end[] asm("_binary_wrong_neutral_ogg_end");

namespace {

// Dois tipos de pedido e uma resposta de turno por vez: a fila nunca cresce.
// A folga cobre pedidos de feedback avulsos chegando durante um turno.
constexpr UBaseType_t kQueueLength = 4;

// 4 KB de pilha. O caminho mais fundo é PlayPcm() -> esp_ae_rate_cvt (buffers
// de HEAP) e PlaySound() -> OggDemuxer (também de heap); a conversão do WAV em
// PCM mora em um std::vector. Nada aqui usa a pilha para dado grande.
constexpr uint32_t kStackSize = 4096;

// Mesma faixa da task da câmera: acima da task de rede (2), abaixo das tasks
// de saída (4) e entrada (8) do AudioService — esta task não pode competir com
// quem realmente empurra amostras para o codec.
constexpr UBaseType_t kPriority = 3;

// Teto defensivo de cada espera de dreno. Não é o tempo esperado (uma resposta
// do tutor tem poucos segundos): é o seguro contra um aviso perdido somado a
// um IsPlaybackIdle() que nunca vira true, que prenderia esta task PARA SEMPRE
// e mataria o áudio do resto da sessão. Estourar conta como falha do turno.
constexpr uint32_t kDrainTimeoutMs = 60000;

// Intervalo do polling de IsPlaybackIdle() enquanto o semáforo não vem. Curto
// o bastante para não acrescentar silêncio perceptível entre o feedback e a
// voz, longo o bastante para ser irrelevante no consumo da CPU.
constexpr uint32_t kDrainPollMs = 50;

std::string_view FeedbackSound(PvAudio::Feedback feedback) {
    switch (feedback) {
        case PvAudio::Feedback::Correct:
            return std::string_view(
                correct_ding_ogg_start,
                static_cast<size_t>(correct_ding_ogg_end - correct_ding_ogg_start));
        case PvAudio::Feedback::Wrong:
            return std::string_view(
                wrong_neutral_ogg_start,
                static_cast<size_t>(wrong_neutral_ogg_end - wrong_neutral_ogg_start));
        case PvAudio::Feedback::None:
            break;
    }
    return std::string_view();
}

}  // namespace

bool PvAudio::Start(EventHandler handler) {
    if (task_ != nullptr) {
        return available_;
    }
    handler_ = std::move(handler);

    // Placa sem codec: tudo vira no-op registrado, como o PvCamera faz sem
    // câmera. O firmware do PV precisa continuar subindo (tela, câmera e rede
    // não dependem do alto-falante).
    auto* codec = Board::GetInstance().GetAudioCodec();
    if (codec == nullptr) {
        ESP_LOGW(TAG, "Placa sem codec de áudio; feedback e voz ficam desligados");
        return false;
    }

    drained_ = xSemaphoreCreateBinary();
    if (drained_ == nullptr) {
        ESP_LOGE(TAG, "Falha ao criar o semáforo de dreno");
        return false;
    }
    queue_ = xQueueCreate(kQueueLength, sizeof(JobItem));
    if (queue_ == nullptr) {
        ESP_LOGE(TAG, "Falha ao criar a fila de pedidos de áudio");
        vSemaphoreDelete(drained_);
        drained_ = nullptr;
        return false;
    }

    // Decisão F3-D3: no modo PV, Application::Initialize() NUNCA roda — quem
    // liga o áudio é este Start(). O construtor da Application é benigno (só
    // event group e um timer não iniciado), então pegar o serviço aqui não
    // arrasta o assistente junto.
    auto& audio = Application::GetInstance().GetAudioService();
    audio.Initialize(codec);

    // Único usuário de SetCallbacks no modo PV (a struct é copiada inteira, e
    // os demais campos ficam vazios de propósito: nada aqui escuta wake word,
    // VAD ou fila de envio). O callback pode vir de qualquer task, então ele
    // só dá o semáforo — a decisão fica na espera, que roda nesta task.
    AudioServiceCallbacks callbacks;
    callbacks.on_playback_drained = [this]() { xSemaphoreGive(drained_); };
    audio.SetCallbacks(callbacks);

    // Sem EnableWakeWordDetection() e sem EnableVoiceProcessing(): a F3 só
    // REPRODUZ áudio. Ligar a captura custaria RAM e CPU do AFE e acenderia o
    // microfone sem que nada consumisse o resultado.
    audio.Start();

    auto entry = [](void* arg) {
        static_cast<PvAudio*>(arg)->Loop();
        vTaskDelete(nullptr);
    };
    if (xTaskCreate(entry, "pv_audio", kStackSize, this, kPriority, &task_) != pdPASS) {
        // O AudioService já subiu; sem a task ninguém lhe pede nada, e o
        // no-op registrado abaixo vale para todos os pedidos seguintes. O
        // callback registrado acima é desfeito ANTES de destruir o semáforo:
        // um dreno futuro qualquer não pode dar Give num handle morto.
        ESP_LOGE(TAG, "Falha ao criar a task de áudio");
        AudioServiceCallbacks empty;
        audio.SetCallbacks(empty);
        vQueueDelete(queue_);
        queue_ = nullptr;
        vSemaphoreDelete(drained_);
        drained_ = nullptr;
        task_ = nullptr;
        return false;
    }

    available_ = true;
    ESP_LOGI(TAG, "Áudio disponível");
    return true;
}

bool PvAudio::Enqueue(Job job, Feedback feedback) {
    if (!available_ || queue_ == nullptr) {
        return false;
    }
    JobItem item{job, feedback};
    if (xQueueSend(queue_, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Fila de áudio cheia; pedido %d descartado", static_cast<int>(job));
        return false;
    }
    return true;
}

bool PvAudio::PlayTurnAudio(std::vector<uint8_t>&& wav_bytes, Feedback feedback) {
    if (!available_) {
        ESP_LOGW(TAG, "Voz pedida sem áudio disponível");
        return false;
    }
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) {
        // Recusa explícita, nunca sobrescrita: sobrepor a voz de um turno com
        // a do próximo confundiria a criança e perderia o evento do primeiro.
        ESP_LOGW(TAG, "Voz pedida com outra em andamento; recusada");
        return false;
    }

    // A posse só é tomada depois do aceite: quem recebeu false continua dono
    // do vetor.
    {
        std::lock_guard<std::mutex> lock(request_mutex_);
        pending_wav_ = std::move(wav_bytes);
    }
    if (!Enqueue(Job::TurnAudio, feedback)) {
        // Devolve o slot e libera a memória: sem item na fila, nenhuma task
        // viria buscar este payload nem emitiria o evento.
        std::lock_guard<std::mutex> lock(request_mutex_);
        std::vector<uint8_t>().swap(pending_wav_);
        busy_.store(false);
        return false;
    }
    return true;
}

void PvAudio::PlayFeedback(Feedback feedback) {
    if (feedback == Feedback::None) {
        return;
    }
    if (!Enqueue(Job::FeedbackOnly, feedback)) {
        ESP_LOGD(TAG, "Feedback ignorado (áudio indisponível)");
    }
}

void PvAudio::Loop() {
    while (true) {
        JobItem item{Job::FeedbackOnly, Feedback::None};
        if (xQueueReceive(queue_, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (item.job) {
            case Job::TurnAudio:
                RunTurnAudio(item.feedback);
                break;
            case Job::FeedbackOnly:
                PlayFeedbackSound(item.feedback);
                // Espera o dreno mesmo sem evento a emitir: assim o próximo
                // pedido da fila começa com o pipeline limpo, e o teto de
                // tempo continua valendo.
                WaitPlaybackDrained(kDrainTimeoutMs);
                break;
        }
    }
}

void PvAudio::RunTurnAudio(Feedback feedback) {
    std::vector<uint8_t> wav;
    {
        std::lock_guard<std::mutex> lock(request_mutex_);
        wav.swap(pending_wav_);
    }

    // Fecha o turno com exatamente UM evento, aconteça o que acontecer no
    // meio. `busy_` cai ANTES do aviso para que o PvApp possa aceitar o
    // próximo turno assim que reagir ao evento.
    auto finish = [this](Event event) {
        busy_.store(false);
        Notify(event);
    };

    PvWavInfo info;
    PvWavError error = PvWavError::None;
    if (!PvWavParse(wav.data(), wav.size(), info, error)) {
        ESP_LOGE(TAG, "WAV inválido (%u bytes): %s", (unsigned)wav.size(), PvWavErrorText(error));
        finish(Event::VoiceFailed);
        return;
    }
    ESP_LOGI(TAG, "WAV aceito: %u amostras a partir do byte %u", (unsigned)info.sample_count(),
             (unsigned)info.data_offset);

    auto& audio = Application::GetInstance().GetAudioService();

    // (b) e (c): o som local vem ANTES da voz e precisa terminar antes dela.
    // Sem esperar o dreno, o ding e a fala do tutor se sobreporiam — o
    // veredicto ficaria ambíguo justamente no instante em que a criança
    // precisa entendê-lo.
    if (feedback != Feedback::None) {
        PlayFeedbackSound(feedback);
        if (!WaitPlaybackDrained(kDrainTimeoutMs)) {
            ESP_LOGE(TAG, "Dreno do feedback estourou o teto; turno falhou");
            finish(Event::VoiceFailed);
            return;
        }
    }

    // (d): conversão para amostras nativas. Este é o pico de memória do turno
    // (WAV + PCM vivos ao mesmo tempo), por isso o WAV é liberado logo em
    // seguida, antes da reprodução.
    std::vector<int16_t> pcm;
    if (!PvWavCopyPcm(wav.data(), wav.size(), info, pcm)) {
        ESP_LOGE(TAG, "Falha ao converter o PCM do WAV");
        finish(Event::VoiceFailed);
        return;
    }
    std::vector<uint8_t>().swap(wav);

    // O semáforo é zerado ANTES de enfileirar: um aviso de dreno anterior não
    // pode satisfazer a espera desta voz.
    xSemaphoreTake(drained_, 0);
    if (!audio.PlayPcm(std::move(pcm), static_cast<int>(kPvWavSampleRate))) {
        // false = o pipeline abortou no meio (Stop()/ResetDecoder()); parte do
        // áudio pode ter saído, então isto é falha do turno, não sucesso.
        ESP_LOGE(TAG, "PlayPcm recusou ou abortou a voz do turno");
        finish(Event::VoiceFailed);
        return;
    }

    // (e): PlayPcm devolver true significa "o pipeline aceitou tudo", não "o
    // alto-falante terminou". O evento só sai depois do dreno de verdade.
    if (!WaitPlaybackDrained(kDrainTimeoutMs)) {
        ESP_LOGE(TAG, "Dreno da voz estourou o teto; turno falhou");
        finish(Event::VoiceFailed);
        return;
    }
    finish(Event::VoiceDone);
}

void PvAudio::PlayFeedbackSound(Feedback feedback) {
    const std::string_view sound = FeedbackSound(feedback);
    if (sound.empty()) {
        return;
    }
    xSemaphoreTake(drained_, 0);
    Application::GetInstance().GetAudioService().PlaySound(sound);
}

bool PvAudio::WaitPlaybackDrained(uint32_t timeout_ms) {
    auto& audio = Application::GetInstance().GetAudioService();
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (true) {
        if (audio.IsPlaybackIdle()) {
            return true;
        }
        // O semáforo acorda no instante do dreno; o timeout curto cobre o
        // aviso que chegou cedo demais (borda perdida) e o que não chegou.
        xSemaphoreTake(drained_, pdMS_TO_TICKS(kDrainPollMs));
        if (audio.IsPlaybackIdle()) {
            return true;
        }
        // Comparação em aritmética com sinal: sobrevive à volta do contador de
        // ticks, que estoura a cada ~49 dias.
        if (static_cast<int32_t>(xTaskGetTickCount() - deadline) >= 0) {
            return false;
        }
    }
}

void PvAudio::Notify(Event event) {
    if (handler_) {
        handler_(event);
    }
}
