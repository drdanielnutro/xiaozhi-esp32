#include "pv_wav.h"

#include <cstring>

namespace {

// Cabeçalho mínimo de um RIFF: "RIFF" + ChunkSize + "WAVE".
constexpr size_t kRiffHeaderSize = 12;

// Cabeçalho de cada chunk: id de 4 bytes + tamanho de 4 bytes.
constexpr size_t kChunkHeaderSize = 8;

// Campos do fmt PCM canônico (audio_format..bits_per_sample). Um fmt maior é
// aceito (WAVE permite bytes extras), mas menor não descreve PCM.
constexpr uint32_t kFmtPcmSize = 16;

// audio_format do PCM sem compressão. Note que WAVE_FORMAT_EXTENSIBLE (0xFFFE)
// NÃO é aceito: o contrato promete o PCM simples, e aceitar a forma extensível
// obrigaria a interpretar o GUID do subformato — código a mais para tolerar um
// arquivo que o backend não produz.
constexpr uint16_t kFormatPcm = 1;

uint16_t ReadU16Le(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t ReadU32Le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool ChunkIdIs(const uint8_t* p, const char* id) { return std::memcmp(p, id, 4) == 0; }

// Marca a falha e encerra. Existe para que todo caminho de erro saia por um
// único ponto, sem esquecer de escrever `error`.
bool Fail(PvWavError reason, PvWavError& error) {
    error = reason;
    return false;
}

}  // namespace

const char* PvWavErrorText(PvWavError error) {
    switch (error) {
        case PvWavError::None:
            return "ok";
        case PvWavError::TooShort:
            return "arquivo menor que o cabeçalho RIFF";
        case PvWavError::NotRiff:
            return "não começa com RIFF";
        case PvWavError::NotWave:
            return "RIFF sem a forma WAVE";
        case PvWavError::BadRiffSize:
            return "ChunkSize do RIFF não bate com o tamanho do arquivo";
        case PvWavError::TruncatedChunk:
            return "chunk declara mais bytes do que o arquivo tem";
        case PvWavError::BadFmtChunk:
            return "chunk fmt menor que 16 bytes";
        case PvWavError::UnsupportedFormat:
            return "não é PCM sem compressão";
        case PvWavError::UnsupportedChannels:
            return "não é mono";
        case PvWavError::UnsupportedSampleRate:
            return "não é 16 kHz";
        case PvWavError::UnsupportedBitsPerSample:
            return "não é 16 bits por amostra";
        case PvWavError::MissingFmt:
            return "chunk fmt ausente antes do chunk data";
        case PvWavError::MissingData:
            return "chunk data ausente";
        case PvWavError::EmptyData:
            return "chunk data vazio";
        case PvWavError::OddDataSize:
            return "chunk data com tamanho ímpar";
        case PvWavError::DataOutOfBounds:
            return "chunk data estoura o arquivo";
        case PvWavError::TrailingGarbage:
            return "os chunks não terminam no fim do arquivo";
    }
    return "erro desconhecido";
}

bool PvWavParse(const uint8_t* bytes, size_t len, PvWavInfo& info, PvWavError& error) {
    info = PvWavInfo{};
    error = PvWavError::None;

    if (bytes == nullptr || len < kRiffHeaderSize) {
        return Fail(PvWavError::TooShort, error);
    }
    if (!ChunkIdIs(bytes, "RIFF")) {
        return Fail(PvWavError::NotRiff, error);
    }

    // A checagem que mais paga: `ChunkSize` == tamanho físico − 8 rejeita de
    // uma vez o download cortado no meio E o placeholder de streaming
    // (0xFFFFFF24 e parentes), que faria o resto do parser confiar em um
    // arquivo que nunca termina. Comparado em 64 bits para não estourar.
    const uint64_t riff_size = ReadU32Le(bytes + 4);
    if (riff_size != static_cast<uint64_t>(len) - 8u) {
        return Fail(PvWavError::BadRiffSize, error);
    }
    if (!ChunkIdIs(bytes + 8, "WAVE")) {
        return Fail(PvWavError::NotWave, error);
    }

    bool have_fmt = false;
    bool have_data = false;

    // Percorre os chunks de verdade. Nada de offset fixo de 44 bytes: um
    // `LIST`/`fact` antes do `data` é legal e desloca o áudio.
    size_t offset = kRiffHeaderSize;
    while (offset + kChunkHeaderSize <= len) {
        const uint8_t* id = bytes + offset;
        const uint32_t size = ReadU32Le(bytes + offset + 4);
        const size_t body = offset + kChunkHeaderSize;
        const size_t remaining = len - body;  // body <= len pela condição do while

        const bool is_data = ChunkIdIs(id, "data");
        if (size > remaining) {
            // Mesma falha física, razões distintas no log: "o áudio não veio
            // inteiro" e "o cabeçalho está corrompido" pedem investigações
            // diferentes.
            return Fail(is_data ? PvWavError::DataOutOfBounds : PvWavError::TruncatedChunk, error);
        }

        if (ChunkIdIs(id, "fmt ") && !have_fmt) {
            if (size < kFmtPcmSize) {
                return Fail(PvWavError::BadFmtChunk, error);
            }
            const uint16_t audio_format = ReadU16Le(bytes + body);
            info.channels = ReadU16Le(bytes + body + 2);
            info.sample_rate = ReadU32Le(bytes + body + 4);
            info.bits_per_sample = ReadU16Le(bytes + body + 14);
            // byte_rate e block_align são deliberadamente ignorados: são
            // redundantes com os campos acima e há gravadores que os erram sem
            // que o PCM esteja errado. Rejeitar por eles perderia áudio bom.
            if (audio_format != kFormatPcm) {
                return Fail(PvWavError::UnsupportedFormat, error);
            }
            if (info.channels != kPvWavChannels) {
                return Fail(PvWavError::UnsupportedChannels, error);
            }
            if (info.sample_rate != kPvWavSampleRate) {
                return Fail(PvWavError::UnsupportedSampleRate, error);
            }
            if (info.bits_per_sample != kPvWavBitsPerSample) {
                return Fail(PvWavError::UnsupportedBitsPerSample, error);
            }
            have_fmt = true;
        } else if (is_data && !have_data) {
            // O fmt precisa vir ANTES: sem ele não há como afirmar que estes
            // bytes são s16le mono 16 kHz, e tocar por suposição é justamente
            // o ruído que este módulo existe para evitar.
            if (!have_fmt) {
                return Fail(PvWavError::MissingFmt, error);
            }
            if (size == 0) {
                return Fail(PvWavError::EmptyData, error);
            }
            if ((size % 2) != 0) {
                return Fail(PvWavError::OddDataSize, error);
            }
            info.data_offset = body;
            info.data_len = size;
            have_data = true;
        }

        // Padrão RIFF: chunk de tamanho ímpar carrega um byte de padding que
        // NÃO entra no tamanho declarado. Ignorá-lo desalinharia todos os
        // chunks seguintes — e o parser passaria a ler lixo como cabeçalho.
        offset = body + size;
        if ((size % 2) != 0) {
            offset += 1;
        }
        // O laço sempre avança pelo menos kChunkHeaderSize, então não há como
        // ficar preso mesmo com uma sequência de chunks vazios.
    }

    // O RIFF tem de ser consumido EXATAMENTE (revisão F3, P2a): `offset` só
    // pode parar no fim físico do arquivo. Menos que isso significa 1 a 7 bytes
    // de lixo que não formam nem um cabeçalho de chunk; `len + 1` significa
    // último chunk ímpar SEM o byte de padding — o arquivo acabou no meio da
    // estrutura. O ChunkSize do RIFF não pega nenhum dos dois, porque ele mede
    // o total e não a soma dos chunks.
    if (offset != len) {
        return Fail(PvWavError::TrailingGarbage, error);
    }

    if (!have_fmt) {
        return Fail(PvWavError::MissingFmt, error);
    }
    if (!have_data) {
        return Fail(PvWavError::MissingData, error);
    }
    return true;
}

bool PvWavCopyPcm(const uint8_t* bytes, size_t len, const PvWavInfo& info,
                  std::vector<int16_t>& out) {
    out.clear();
    // Revalidação barata: `info` pode ter vindo de outro buffer (o chamador
    // move vetores por aí), e ler fora daqui seria a pior falha possível.
    if (bytes == nullptr || info.data_len == 0 || (info.data_len % 2) != 0 ||
        info.data_offset > len || info.data_len > len - info.data_offset) {
        return false;
    }

    const size_t samples = info.data_len / 2;
    out.resize(samples);
    const uint8_t* p = bytes + info.data_offset;
    for (size_t i = 0; i < samples; i++) {
        // Montagem explícita em little-endian: nada de memcpy sobre int16_t,
        // que dependeria da ordem de bytes do alvo e do alinhamento do buffer.
        const uint16_t raw = static_cast<uint16_t>(static_cast<uint16_t>(p[2 * i]) |
                                                   (static_cast<uint16_t>(p[2 * i + 1]) << 8));
        out[i] = static_cast<int16_t>(raw);
    }
    return true;
}
