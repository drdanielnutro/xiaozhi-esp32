// Teste de host do pv_wav: o parser RIFF/WAVE por chunks que interpreta a voz
// do turno antes de ela virar PCM.
//
// Compila e roda fora do ESP-IDF (ver run.sh), com ASan/UBSan ligados — o que
// é parte do teste: metade dos casos abaixo alimenta o parser com arquivos
// truncados ou com tamanhos mentirosos, e a exigência não é só "rejeitar", é
// "rejeitar sem ler um byte fora do buffer".
//
// Este diretório fica FORA do GLOB do build de firmware (main/CMakeLists.txt
// varre apenas professor_virtual/*.cc e professor_virtual/ui/*.cc) —
// intencional.

#include <cstdio>
#include <string>
#include <vector>

#include "pv_wav.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const char* what) {
    g_checks++;
    if (!condition) {
        g_failures++;
        std::printf("  FALHA: %s\n", what);
    }
}

void CheckEqSize(size_t actual, size_t expected, const char* what) {
    g_checks++;
    if (actual != expected) {
        g_failures++;
        std::printf("  FALHA: %s (esperado %zu, obtido %zu)\n", what, expected, actual);
    }
}

void CheckError(PvWavError actual, PvWavError expected, const char* what) {
    g_checks++;
    if (actual != expected) {
        g_failures++;
        std::printf("  FALHA: %s (esperado \"%s\", obtido \"%s\")\n", what,
                    PvWavErrorText(expected), PvWavErrorText(actual));
    }
}

// ---------------------------------------------------------------------------
// Montagem sintética dos arquivos. Nenhum WAV de fixture em disco: os casos
// interessantes são justamente os malformados, que nenhum gravador produz.
// ---------------------------------------------------------------------------

void AppendU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void AppendU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void AppendId(std::vector<uint8_t>& out, const char* id) {
    for (int i = 0; i < 4; i++) {
        out.push_back(static_cast<uint8_t>(id[i]));
    }
}

// Chunk completo: id + tamanho declarado + corpo + padding do RIFF quando o
// tamanho é ímpar.
void AppendChunk(std::vector<uint8_t>& out, const char* id, const std::vector<uint8_t>& body) {
    AppendId(out, id);
    AppendU32(out, static_cast<uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    if ((body.size() % 2) != 0) {
        out.push_back(0x00);
    }
}

std::vector<uint8_t> FmtBody(uint16_t audio_format, uint16_t channels, uint32_t sample_rate,
                             uint16_t bits) {
    std::vector<uint8_t> body;
    const uint16_t block_align = static_cast<uint16_t>(channels * bits / 8);
    AppendU16(body, audio_format);
    AppendU16(body, channels);
    AppendU32(body, sample_rate);
    AppendU32(body, sample_rate * block_align);  // byte_rate
    AppendU16(body, block_align);
    AppendU16(body, bits);
    return body;
}

std::vector<uint8_t> FmtPcm16k() { return FmtBody(1, 1, 16000, 16); }

std::vector<uint8_t> PcmBody(const std::vector<int16_t>& samples) {
    std::vector<uint8_t> body;
    for (int16_t sample : samples) {
        AppendU16(body, static_cast<uint16_t>(sample));
    }
    return body;
}

// Fecha o RIFF: escreve ChunkSize = tamanho físico − 8 (o que o contrato
// exige do backend).
void FixRiffSize(std::vector<uint8_t>& file) {
    const uint32_t size = static_cast<uint32_t>(file.size() - 8);
    file[4] = static_cast<uint8_t>(size & 0xFF);
    file[5] = static_cast<uint8_t>((size >> 8) & 0xFF);
    file[6] = static_cast<uint8_t>((size >> 16) & 0xFF);
    file[7] = static_cast<uint8_t>((size >> 24) & 0xFF);
}

// Envelope RIFF/WAVE em volta de uma lista de chunks já montada.
std::vector<uint8_t> BuildRiff(const std::vector<uint8_t>& chunks) {
    std::vector<uint8_t> file;
    AppendId(file, "RIFF");
    AppendU32(file, 0);  // corrigido no fim
    AppendId(file, "WAVE");
    file.insert(file.end(), chunks.begin(), chunks.end());
    FixRiffSize(file);
    return file;
}

// WAV canônico: cabeçalho de 44 bytes seguido do PCM.
std::vector<uint8_t> BuildCanonical(const std::vector<int16_t>& samples) {
    std::vector<uint8_t> chunks;
    AppendChunk(chunks, "fmt ", FmtPcm16k());
    AppendChunk(chunks, "data", PcmBody(samples));
    return BuildRiff(chunks);
}

// Reescreve o tamanho declarado de um chunk cujo id começa em `id_offset`.
// Serve para mentir sobre o tamanho sem mexer no corpo.
void SetChunkSize(std::vector<uint8_t>& file, size_t id_offset, uint32_t size) {
    file[id_offset + 4] = static_cast<uint8_t>(size & 0xFF);
    file[id_offset + 5] = static_cast<uint8_t>((size >> 8) & 0xFF);
    file[id_offset + 6] = static_cast<uint8_t>((size >> 16) & 0xFF);
    file[id_offset + 7] = static_cast<uint8_t>((size >> 24) & 0xFF);
}

bool Parse(const std::vector<uint8_t>& file, PvWavInfo& info, PvWavError& error) {
    return PvWavParse(file.data(), file.size(), info, error);
}

// Atalho para os casos de rejeição, que são a maioria.
void ExpectReject(const std::vector<uint8_t>& file, PvWavError expected, const char* what) {
    PvWavInfo info;
    PvWavError error = PvWavError::None;
    const bool ok = Parse(file, info, error);
    Check(!ok, what);
    if (!ok) {
        CheckError(error, expected, what);
    }
}

// ---------------------------------------------------------------------------
// Casos aceitos
// ---------------------------------------------------------------------------

// (1) WAV canônico: 44 bytes de cabeçalho + dados válidos.
void TestCanonical() {
    std::printf("\n[canônico]\n");
    const std::vector<int16_t> samples = {0, 1, -1, 32767, -32768, 258};
    const std::vector<uint8_t> file = BuildCanonical(samples);

    PvWavInfo info;
    PvWavError error = PvWavError::None;
    Check(Parse(file, info, error), "WAV canônico aceito");
    CheckError(error, PvWavError::None, "sem razão de rejeição");
    CheckEqSize(info.data_offset, 44, "offset canônico de 44 bytes");
    CheckEqSize(info.data_len, samples.size() * 2, "tamanho do PCM");
    CheckEqSize(info.sample_count(), samples.size(), "contagem de amostras");
    CheckEqSize(info.sample_rate, 16000, "taxa de amostragem");
    CheckEqSize(info.channels, 1, "canais");
    CheckEqSize(info.bits_per_sample, 16, "bits por amostra");

    // A conversão precisa devolver EXATAMENTE as amostras originais, inclusive
    // as negativas e os extremos — um erro de sinal aqui viraria estouro no
    // alto-falante.
    std::vector<int16_t> pcm;
    Check(PvWavCopyPcm(file.data(), file.size(), info, pcm), "conversão para PCM");
    Check(pcm == samples, "PCM little-endian idêntico ao original");
}

// (2) chunk extra ANTES do data: o offset deixa de ser 44 e o parser tem que
// acompanhar.
void TestExtraChunkBeforeData() {
    std::printf("\n[chunk extra antes do data]\n");
    std::vector<uint8_t> chunks;
    AppendChunk(chunks, "fmt ", FmtPcm16k());
    AppendChunk(chunks, "LIST", std::vector<uint8_t>(10, 0x41));
    const std::vector<int16_t> samples = {7, -7};
    AppendChunk(chunks, "data", PcmBody(samples));
    const std::vector<uint8_t> file = BuildRiff(chunks);

    PvWavInfo info;
    PvWavError error = PvWavError::None;
    Check(Parse(file, info, error), "WAV com LIST antes do data aceito");
    // 44 (canônico) + 8 do cabeçalho do LIST + 10 do corpo.
    CheckEqSize(info.data_offset, 62, "offset não canônico acompanhado");
    CheckEqSize(info.data_len, 4, "tamanho do PCM");

    std::vector<int16_t> pcm;
    Check(PvWavCopyPcm(file.data(), file.size(), info, pcm), "conversão para PCM");
    Check(pcm == samples, "PCM lido do offset deslocado");
}

// (3) chunk extra DEPOIS do data (LIST/INFO de metadados é comum).
void TestExtraChunkAfterData() {
    std::printf("\n[chunk extra depois do data]\n");
    std::vector<uint8_t> chunks;
    AppendChunk(chunks, "fmt ", FmtPcm16k());
    AppendChunk(chunks, "data", PcmBody({1, 2, 3, 4}));
    AppendChunk(chunks, "LIST", std::vector<uint8_t>(12, 0x42));
    const std::vector<uint8_t> file = BuildRiff(chunks);

    PvWavInfo info;
    PvWavError error = PvWavError::None;
    Check(Parse(file, info, error), "WAV com LIST depois do data aceito");
    CheckEqSize(info.data_offset, 44, "offset do data preservado");
    CheckEqSize(info.data_len, 8, "tamanho do PCM");
}

// (4) chunk de tamanho ÍMPAR: o byte de padding não entra no tamanho
// declarado. Ignorá-lo desalinharia todos os chunks seguintes.
void TestOddChunkPadding() {
    std::printf("\n[padding de chunk ímpar]\n");
    std::vector<uint8_t> chunks;
    AppendChunk(chunks, "junk", std::vector<uint8_t>(3, 0x43));  // 3 bytes + 1 de padding
    AppendChunk(chunks, "fmt ", FmtPcm16k());
    AppendChunk(chunks, "data", PcmBody({9, -9}));
    const std::vector<uint8_t> file = BuildRiff(chunks);

    PvWavInfo info;
    PvWavError error = PvWavError::None;
    Check(Parse(file, info, error), "chunks depois do ímpar ainda encontrados");
    // 12 (RIFF/WAVE) + 8+3+1 (junk) + 8+16 (fmt) + 8 (cabeçalho do data).
    CheckEqSize(info.data_offset, 56, "offset atrás do padding");
    CheckEqSize(info.data_len, 4, "tamanho do PCM");

    // Sem o padding, o parser leria o último byte do junk como início do
    // próximo id: este caso é a diferença entre encontrar o fmt e não achar
    // nada.
    std::vector<uint8_t> broken = file;
    broken.erase(broken.begin() + 23);  // remove o byte de padding do junk
    FixRiffSize(broken);
    PvWavInfo info_broken;
    PvWavError error_broken = PvWavError::None;
    Check(!Parse(broken, info_broken, error_broken), "sem o padding, o layout é rejeitado");
}

// (4b) o último chunk do arquivo também obedece à regra do padding: COM o byte
// de preenchimento o RIFF fecha no fim físico; SEM ele o arquivo acaba no meio
// da estrutura e tem de ser recusado (revisão F3, P2a).
void TestFinalOddChunk() {
    std::printf("\n[último chunk ímpar]\n");
    std::vector<uint8_t> chunks;
    AppendChunk(chunks, "fmt ", FmtPcm16k());
    AppendChunk(chunks, "data", PcmBody({4, -4}));
    AppendChunk(chunks, "junk", std::vector<uint8_t>(3, 0x44));  // 3 bytes + 1 de padding
    const std::vector<uint8_t> padded = BuildRiff(chunks);

    PvWavInfo info;
    PvWavError error = PvWavError::None;
    Check(Parse(padded, info, error), "último chunk ímpar COM padding aceito");
    CheckEqSize(info.data_offset, 44, "offset do data preservado");

    // Mesmo arquivo, sem o byte de padding final: o passeio pelos chunks
    // termina em len + 1. O ChunkSize do RIFF é reescrito para isolar o caso —
    // sem isso a recusa viria do envelope, não do fim dos chunks.
    std::vector<uint8_t> unpadded = padded;
    unpadded.pop_back();
    FixRiffSize(unpadded);
    ExpectReject(unpadded, PvWavError::TrailingGarbage, "último chunk ímpar SEM padding rejeitado");
}

// (4c) sobra de 1 a 7 bytes depois do último chunk: curta demais para ser um
// cabeçalho, então o laço de chunks simplesmente pararia antes do fim do
// arquivo e o resto viraria conteúdo não interpretado.
void TestTrailingBytes() {
    std::printf("\n[bytes sobrando no fim]\n");
    for (size_t extra = 1; extra <= 7; extra++) {
        std::vector<uint8_t> file = BuildCanonical({1, 2});
        file.insert(file.end(), extra, 0x45);
        FixRiffSize(file);  // isola o caso: o envelope continua coerente
        ExpectReject(file, PvWavError::TrailingGarbage, "sobra curta no fim rejeitada");
    }

    // 8 bytes sobrando JÁ são um cabeçalho de chunk: aí a recusa é a do
    // cabeçalho, não a da sobra — e o arquivo com sobra ZERO continua aceito.
    std::vector<uint8_t> exact = BuildCanonical({1, 2});
    PvWavInfo info;
    PvWavError error = PvWavError::None;
    Check(Parse(exact, info, error), "sem sobra o arquivo continua aceito");
}

// Um fmt de 18 bytes (com cbSize) continua sendo PCM canônico.
void TestFmtWithExtension() {
    std::printf("\n[fmt com cbSize]\n");
    std::vector<uint8_t> fmt = FmtPcm16k();
    AppendU16(fmt, 0);  // cbSize
    std::vector<uint8_t> chunks;
    AppendChunk(chunks, "fmt ", fmt);
    AppendChunk(chunks, "data", PcmBody({5, 6}));
    const std::vector<uint8_t> file = BuildRiff(chunks);

    PvWavInfo info;
    PvWavError error = PvWavError::None;
    Check(Parse(file, info, error), "fmt de 18 bytes aceito");
    CheckEqSize(info.data_offset, 46, "offset com fmt estendido");
}

// ---------------------------------------------------------------------------
// Casos rejeitados
// ---------------------------------------------------------------------------

// (5) e (6): o ChunkSize do RIFF é a checagem que rejeita tanto o placeholder
// de streaming quanto o download cortado.
void TestRiffSize() {
    std::printf("\n[ChunkSize do RIFF]\n");
    std::vector<uint8_t> streaming = BuildCanonical({1, 2});
    SetChunkSize(streaming, 0, 0xFFFFFF24);
    ExpectReject(streaming, PvWavError::BadRiffSize, "placeholder de streaming rejeitado");

    std::vector<uint8_t> too_big = BuildCanonical({1, 2});
    SetChunkSize(too_big, 0, static_cast<uint32_t>(too_big.size()));  // deveria ser size − 8
    ExpectReject(too_big, PvWavError::BadRiffSize, "ChunkSize maior que o arquivo rejeitado");

    std::vector<uint8_t> too_small = BuildCanonical({1, 2});
    SetChunkSize(too_small, 0, static_cast<uint32_t>(too_small.size() - 12));
    ExpectReject(too_small, PvWavError::BadRiffSize, "ChunkSize menor que o arquivo rejeitado");

    std::vector<uint8_t> zero = BuildCanonical({1, 2});
    SetChunkSize(zero, 0, 0);
    ExpectReject(zero, PvWavError::BadRiffSize, "ChunkSize zerado rejeitado");
}

// Envelope errado: nem RIFF, nem WAVE, ou curto demais.
void TestEnvelope() {
    std::printf("\n[envelope]\n");
    PvWavInfo info;
    PvWavError error = PvWavError::None;
    Check(!PvWavParse(nullptr, 0, info, error), "buffer nulo rejeitado");
    CheckError(error, PvWavError::TooShort, "buffer nulo");

    ExpectReject(std::vector<uint8_t>(), PvWavError::TooShort, "arquivo vazio rejeitado");
    ExpectReject(std::vector<uint8_t>(11, 0x00), PvWavError::TooShort,
                 "arquivo de 11 bytes rejeitado");

    std::vector<uint8_t> not_riff = BuildCanonical({1, 2});
    not_riff[0] = 'X';
    ExpectReject(not_riff, PvWavError::NotRiff, "sem RIFF rejeitado");

    std::vector<uint8_t> not_wave = BuildCanonical({1, 2});
    not_wave[8] = 'X';
    ExpectReject(not_wave, PvWavError::NotWave, "sem WAVE rejeitado");

    // Uma resposta de erro em HTML no lugar do áudio: exatamente o que chega
    // quando o backend devolve página de erro com 200.
    const std::string html = "<!doctype html><html><body>erro</body></html>";
    ExpectReject(std::vector<uint8_t>(html.begin(), html.end()), PvWavError::NotRiff,
                 "HTML no lugar do WAV rejeitado");
}

// (7), (8), (9): o chunk data em si.
void TestDataChunk() {
    std::printf("\n[chunk data]\n");
    std::vector<uint8_t> overflow = BuildCanonical({1, 2});
    SetChunkSize(overflow, 36, 0xFFFFFFFF);
    ExpectReject(overflow, PvWavError::DataOutOfBounds, "data estourando o arquivo rejeitado");

    std::vector<uint8_t> just_over = BuildCanonical({1, 2});
    SetChunkSize(just_over, 36, 5);  // há 4 bytes de corpo
    ExpectReject(just_over, PvWavError::DataOutOfBounds, "data com 1 byte a mais rejeitado");

    std::vector<uint8_t> odd_chunks;
    AppendChunk(odd_chunks, "fmt ", FmtPcm16k());
    AppendChunk(odd_chunks, "data", std::vector<uint8_t>(3, 0x01));
    ExpectReject(BuildRiff(odd_chunks), PvWavError::OddDataSize,
                 "data com tamanho ímpar rejeitado");

    std::vector<uint8_t> empty_chunks;
    AppendChunk(empty_chunks, "fmt ", FmtPcm16k());
    AppendChunk(empty_chunks, "data", std::vector<uint8_t>());
    ExpectReject(BuildRiff(empty_chunks), PvWavError::EmptyData, "data vazio rejeitado");
}

// (10) formato fora do contrato: cada campo tem sua própria razão de recusa,
// porque o log de campo precisa dizer o que o backend mandou.
void TestFormat() {
    std::printf("\n[formato]\n");
    struct Case {
        uint16_t audio_format;
        uint16_t channels;
        uint32_t sample_rate;
        uint16_t bits;
        PvWavError expected;
        const char* what;
    };
    const Case cases[] = {
        {3, 1, 16000, 16, PvWavError::UnsupportedFormat, "float (audio_format 3) rejeitado"},
        {7, 1, 16000, 16, PvWavError::UnsupportedFormat, "mu-law (audio_format 7) rejeitado"},
        {0xFFFE, 1, 16000, 16, PvWavError::UnsupportedFormat, "WAVE_FORMAT_EXTENSIBLE rejeitado"},
        {1, 2, 16000, 16, PvWavError::UnsupportedChannels, "estéreo rejeitado"},
        {1, 0, 16000, 16, PvWavError::UnsupportedChannels, "zero canais rejeitado"},
        {1, 1, 44100, 16, PvWavError::UnsupportedSampleRate, "44,1 kHz rejeitado"},
        {1, 1, 8000, 16, PvWavError::UnsupportedSampleRate, "8 kHz rejeitado"},
        {1, 1, 16000, 8, PvWavError::UnsupportedBitsPerSample, "8 bits rejeitado"},
        {1, 1, 16000, 24, PvWavError::UnsupportedBitsPerSample, "24 bits rejeitado"},
    };
    for (const Case& c : cases) {
        std::vector<uint8_t> chunks;
        AppendChunk(chunks, "fmt ", FmtBody(c.audio_format, c.channels, c.sample_rate, c.bits));
        AppendChunk(chunks, "data", PcmBody({1, 2}));
        ExpectReject(BuildRiff(chunks), c.expected, c.what);
    }

    // fmt curto demais para descrever PCM.
    std::vector<uint8_t> short_fmt;
    AppendChunk(short_fmt, "fmt ", std::vector<uint8_t>(14, 0x00));
    AppendChunk(short_fmt, "data", PcmBody({1, 2}));
    ExpectReject(BuildRiff(short_fmt), PvWavError::BadFmtChunk, "fmt de 14 bytes rejeitado");
}

// (12) ausência de fmt, ausência de data, e fmt depois do data.
void TestMissingChunks() {
    std::printf("\n[chunks ausentes ou fora de ordem]\n");
    std::vector<uint8_t> no_fmt;
    AppendChunk(no_fmt, "data", PcmBody({1, 2}));
    ExpectReject(BuildRiff(no_fmt), PvWavError::MissingFmt, "sem fmt rejeitado");

    std::vector<uint8_t> no_data;
    AppendChunk(no_data, "fmt ", FmtPcm16k());
    ExpectReject(BuildRiff(no_data), PvWavError::MissingData, "sem data rejeitado");

    std::vector<uint8_t> only_header;
    ExpectReject(BuildRiff(only_header), PvWavError::MissingFmt, "só RIFF/WAVE rejeitado");

    // fmt DEPOIS do data: os bytes existem, mas na hora de decidir se eles são
    // s16le mono 16 kHz não havia nada em que se basear.
    std::vector<uint8_t> late_fmt;
    AppendChunk(late_fmt, "data", PcmBody({1, 2}));
    AppendChunk(late_fmt, "fmt ", FmtPcm16k());
    ExpectReject(BuildRiff(late_fmt), PvWavError::MissingFmt, "fmt depois do data rejeitado");
}

// (11) truncamento: para CADA corte possível do arquivo, com e sem o
// ChunkSize do RIFF reescrito, o parser precisa rejeitar sem ler fora do
// buffer. Quem cobra a segunda parte é o ASan (ver run.sh).
void TestTruncation() {
    std::printf("\n[truncamento em todos os cortes]\n");
    const std::vector<uint8_t> full = BuildCanonical({1, 2, 3, 4, 5, 6});
    int accepted_raw = 0;
    int accepted_fixed = 0;

    for (size_t cut = 0; cut < full.size(); cut++) {
        // (a) corte cru: o ChunkSize do RIFF vira mentira, como num download
        // interrompido.
        std::vector<uint8_t> raw(full.begin(), full.begin() + cut);
        PvWavInfo info;
        PvWavError error = PvWavError::None;
        if (PvWavParse(raw.data(), raw.size(), info, error)) {
            accepted_raw++;
        }

        // (b) corte com o ChunkSize corrigido: o envelope fica coerente e a
        // rejeição tem que vir do passeio pelos chunks, não do atalho.
        if (cut >= 12) {
            std::vector<uint8_t> fixed(full.begin(), full.begin() + cut);
            FixRiffSize(fixed);
            PvWavInfo info_fixed;
            PvWavError error_fixed = PvWavError::None;
            if (PvWavParse(fixed.data(), fixed.size(), info_fixed, error_fixed)) {
                accepted_fixed++;
            }
        }
    }
    Check(accepted_raw == 0, "nenhum corte cru aceito");
    Check(accepted_fixed == 0, "nenhum corte com ChunkSize corrigido aceito");

    // O arquivo inteiro continua válido: o laço acima não vale nada se o caso
    // completo também for rejeitado.
    PvWavInfo info;
    PvWavError error = PvWavError::None;
    Check(Parse(full, info, error), "o arquivo completo continua aceito");
}

// A cópia do PCM revalida os limites por conta própria: `info` pode ter vindo
// de outro buffer.
void TestCopyPcmBounds() {
    std::printf("\n[limites da cópia do PCM]\n");
    const std::vector<uint8_t> file = BuildCanonical({1, 2, 3});
    PvWavInfo info;
    PvWavError error = PvWavError::None;
    Check(Parse(file, info, error), "base do caso");

    std::vector<int16_t> pcm;
    Check(!PvWavCopyPcm(nullptr, file.size(), info, pcm), "buffer nulo recusado");
    Check(!PvWavCopyPcm(file.data(), info.data_offset, info, pcm), "buffer curto recusado");

    PvWavInfo bogus = info;
    bogus.data_len += 2;
    Check(!PvWavCopyPcm(file.data(), file.size(), bogus, pcm), "tamanho inflado recusado");

    bogus = info;
    bogus.data_offset = file.size();
    Check(!PvWavCopyPcm(file.data(), file.size(), bogus, pcm), "offset no fim recusado");

    bogus = info;
    bogus.data_len = 0;
    Check(!PvWavCopyPcm(file.data(), file.size(), bogus, pcm), "tamanho zero recusado");

    bogus = info;
    bogus.data_len = 3;
    Check(!PvWavCopyPcm(file.data(), file.size(), bogus, pcm), "tamanho ímpar recusado");

    Check(pcm.empty(), "nenhuma recusa deixa amostras para trás");
}

// Um WAV do tamanho de uma resposta real (1 s de voz), para exercitar o
// caminho completo com dado de verdade.
void TestRealisticSize() {
    std::printf("\n[tamanho realista]\n");
    std::vector<int16_t> samples(16000);
    for (size_t i = 0; i < samples.size(); i++) {
        samples[i] = static_cast<int16_t>(static_cast<int>((i * 37) % 65536) - 32768);
    }
    const std::vector<uint8_t> file = BuildCanonical(samples);

    PvWavInfo info;
    PvWavError error = PvWavError::None;
    Check(Parse(file, info, error), "1 s de voz aceito");
    CheckEqSize(info.sample_count(), 16000, "16000 amostras em 1 s");

    std::vector<int16_t> pcm;
    Check(PvWavCopyPcm(file.data(), file.size(), info, pcm), "conversão de 1 s");
    Check(pcm == samples, "1 s de PCM idêntico ao original");
}

}  // namespace

int main() {
    std::printf("pv_wav — teste de host\n");
    TestCanonical();
    TestExtraChunkBeforeData();
    TestExtraChunkAfterData();
    TestOddChunkPadding();
    TestFinalOddChunk();
    TestTrailingBytes();
    TestFmtWithExtension();
    TestRiffSize();
    TestEnvelope();
    TestDataChunk();
    TestFormat();
    TestMissingChunks();
    TestTruncation();
    TestCopyPcmBounds();
    TestRealisticSize();

    std::printf("\n%d verificações, %d falha(s)\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("FALHOU\n");
    return 1;
}
