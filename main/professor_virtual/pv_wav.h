#ifndef PV_WAV_H
#define PV_WAV_H

#include <cstddef>
#include <cstdint>
#include <vector>

// Interpretação do WAV que o backend entrega como voz do turno.
//
// Por que este módulo é PURO (só C++ padrão: nem cJSON, nem ESP-IDF, nem
// FreeRTOS): o layout RIFF é a única parte do caminho de áudio que dá para
// exercitar exaustivamente sem placa. Arquivo truncado, tamanho ímpar, chunk
// que estoura o buffer e placeholder de streaming são exatamente os casos que
// derrubam o firmware em campo e que ninguém consegue reproduzir na bancada —
// então eles vivem aqui, cobertos por teste de host com ASan/UBSan
// (host_test/test_pv_wav.cc). Quem fala com o AudioService é o pv_audio.
//
// O parser é ESTRITO de propósito. O contrato do dispositivo (§ mídia por URL)
// promete um arquivo FINITO, com `RIFF ChunkSize` = tamanho físico − 8 e
// `data` contendo PCM real s16le mono 16 kHz. Tudo que foge disso é sinal de
// problema (download cortado, resposta de erro em HTML, gravação em streaming
// com tamanho placeholder) e vira falha explícita, nunca ruído no
// alto-falante da criança.
//
// E, por construção, ele NUNCA presume o offset canônico de 44 bytes: percorre
// os chunks de verdade, porque um `LIST`/`fact` antes do `data` é legal em
// RIFF e desloca o áudio.

// Formato exigido pelo contrato. Público porque o pv_audio repassa a taxa ao
// AudioService::PlayPcm e o teste de host monta cabeçalhos com estes valores.
constexpr uint32_t kPvWavSampleRate = 16000;
constexpr uint16_t kPvWavChannels = 1;
constexpr uint16_t kPvWavBitsPerSample = 16;

// Razão da rejeição. É valor estável para log: o texto muda, o enum não.
enum class PvWavError : uint8_t {
    None,
    // Nem o cabeçalho RIFF/WAVE de 12 bytes cabe no que chegou.
    TooShort,
    // Não começa com "RIFF" (uma resposta de erro em HTML cai aqui).
    NotRiff,
    // "RIFF" sem a forma "WAVE".
    NotWave,
    // ChunkSize != tamanho físico − 8. Pega o download cortado E o
    // placeholder de streaming (0xFFFFFF24 e parentes).
    BadRiffSize,
    // Um chunk qualquer declara mais bytes do que o arquivo tem.
    TruncatedChunk,
    // Chunk fmt menor que os 16 bytes do PCM canônico.
    BadFmtChunk,
    // audio_format != 1 (não é PCM sem compressão).
    UnsupportedFormat,
    UnsupportedChannels,       // não é mono
    UnsupportedSampleRate,     // não é 16 kHz
    UnsupportedBitsPerSample,  // não é 16 bits
    // Sem chunk fmt, ou fmt aparecendo só depois do data.
    MissingFmt,
    MissingData,  // sem chunk data
    EmptyData,    // chunk data de 0 bytes
    // Chunk data com tamanho ímpar: não fecha amostras de 16 bits.
    OddDataSize,
    // O chunk data declara mais bytes do que o arquivo tem.
    DataOutOfBounds,
    // O passeio pelos chunks não terminou EXATAMENTE no fim do arquivo: sobrou
    // lixo curto demais para ser um chunk (1 a 7 bytes) ou faltou o byte de
    // padding do último chunk ímpar. Nos dois casos o arquivo não é o RIFF que
    // ele diz ser, e o ChunkSize sozinho não pega isso.
    TrailingGarbage,
};

// Texto pt-BR curto para log. Nunca devolve nullptr.
const char* PvWavErrorText(PvWavError error);

// Onde o PCM começa e quanto ele mede DENTRO do buffer original — o parser não
// copia nada. Os campos de formato voltam preenchidos porque o log de campo
// precisa dizer o que veio quando a validação falha por formato.
struct PvWavInfo {
    size_t data_offset = 0;  // primeiro byte do PCM, medido do início do arquivo
    size_t data_len = 0;     // bytes de PCM (sempre par e > 0 quando aceito)
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;

    // Amostras de 16 bits contidas em `data_len`.
    size_t sample_count() const { return data_len / 2; }
};

// Valida e localiza o PCM. `bytes` pode ser nullptr (com len 0) sem risco.
//
// true: `info` está preenchido e `data_offset + data_len <= len`.
// false: `error` diz por quê; `info` fica com o que deu para apurar (útil no
// log dos casos de formato).
bool PvWavParse(const uint8_t* bytes, size_t len, PvWavInfo& info, PvWavError& error);

// Converte o PCM little-endian apontado por `info` em amostras nativas.
//
// Separado do parse porque é AQUI que a memória dobra de tamanho: o chamador
// escolhe o momento (na task do PvAudio, nunca no laço de eventos). `info`
// precisa ter vindo de um PvWavParse() bem-sucedido sobre o MESMO buffer; a
// função revalida os limites antes de ler, e devolve false se não baterem.
bool PvWavCopyPcm(const uint8_t* bytes, size_t len, const PvWavInfo& info,
                  std::vector<int16_t>& out);

#endif  // PV_WAV_H
