// Teste de host do ParseTurnResponse: parser ESTRITO da resposta 200 de
// POST /api/turn no perfil v1.1 (§7.5 + "Campos novos na resposta").
//
// A diferença para test_session_mirror é o REGIME: lá campo ausente vira
// default, aqui vira rejeição. Cada caso abaixo altera UM campo do corpo
// válido, para provar que a rejeição veio do campo em questão e não de um
// corpo quebrado por acidente.
//
// Compila e roda fora do ESP-IDF (ver run.sh).

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "pv_session_mirror.h"

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

void CheckEq(const std::string& actual, const std::string& expected, const char* what) {
    g_checks++;
    if (actual != expected) {
        g_failures++;
        std::printf("  FALHA: %s (esperado \"%s\", obtido \"%s\")\n", what, expected.c_str(),
                    actual.c_str());
    }
}

void CheckEqInt(int actual, int expected, const char* what) {
    g_checks++;
    if (actual != expected) {
        g_failures++;
        std::printf("  FALHA: %s (esperado %d, obtido %d)\n", what, expected, actual);
    }
}

using Field = std::pair<std::string, std::string>;

// Corpo válido do contrato, campo a campo (o valor já é JSON pronto).
const std::vector<Field>& BaseTurn() {
    static const std::vector<Field> base = {
        {"veredicto", "\"correct\""},
        {"texto_explicacao", "\"Muito bem! A conta está certa.\""},
        {"audio_base64", "\"\""},
        {"image_base64", "\"\""},
        {"session_status", "\"active\""},
        {"current_item", "\"item_2\""},
        {"current_tarefa", "\"item_2_task_1\""},
        {"wrong_answer_count", "1"},
        {"adult_intervention_required", "false"},
        {"audio_url", "\"/api/media/turn_0123456789abcdef0123456789abcdef_audio.wav\""},
        {"image_url", "\"/api/media/turn_0123456789abcdef0123456789abcdef_image.jpg\""},
        {"audio_format", "\"wav\""},
        {"image_format", "\"jpg\""},
        {"request_id", "\"3f2504e0-4f89-41d3-9a0c-0305e82c3301\""},
    };
    return base;
}

// Monta o corpo trocando (ou acrescentando) os campos de `overrides` e
// removendo os de `removed`. Assim cada caso muda uma coisa só.
std::string MakeTurn(const std::vector<Field>& overrides = {},
                     const std::vector<std::string>& removed = {}) {
    std::vector<Field> fields = BaseTurn();
    for (const auto& override_field : overrides) {
        bool replaced = false;
        for (auto& field : fields) {
            if (field.first == override_field.first) {
                field.second = override_field.second;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            fields.push_back(override_field);
        }
    }
    for (const auto& key : removed) {
        for (size_t i = 0; i < fields.size(); i++) {
            if (fields[i].first == key) {
                fields.erase(fields.begin() + static_cast<long>(i));
                break;
            }
        }
    }

    std::string json = "{";
    for (size_t i = 0; i < fields.size(); i++) {
        if (i > 0) {
            json += ",";
        }
        json += "\"" + fields[i].first + "\":" + fields[i].second;
    }
    json += "}";
    return json;
}

// Açúcar: espera rejeição e confirma que `out` ficou limpo.
void ExpectReject(const std::string& json, const char* what) {
    PvTurnResponse out;
    // Suja o destino de propósito: o parser tem de limpar antes de falhar.
    Check(ParseTurnResponse(MakeTurn().c_str(), out), "pré-condição: corpo válido antes do caso");
    g_checks++;
    if (ParseTurnResponse(json.c_str(), out)) {
        g_failures++;
        std::printf("  FALHA: %s (aceitou o corpo)\n", what);
        return;
    }
    Check(out.request_id.empty() && out.audio_url.empty() && out.image_url.empty() &&
              out.texto_explicacao.empty() && out.session_status.empty() &&
              out.wrong_answer_count == 0 && !out.adult_intervention_required,
          "destino limpo após rejeição");
}

void ExpectAccept(const std::string& json, const char* what) {
    PvTurnResponse out;
    Check(ParseTurnResponse(json.c_str(), out), what);
}

void TestValidResponse() {
    std::printf("resposta válida completa\n");
    PvTurnResponse out;
    Check(ParseTurnResponse(MakeTurn().c_str(), out), "aceita o corpo do contrato");
    Check(out.veredicto == PvVerdict::Correct, "veredicto correct");
    CheckEq(out.texto_explicacao, "Muito bem! A conta está certa.", "texto_explicacao");
    CheckEq(out.session_status, "active", "session_status");
    CheckEq(out.current_item, "item_2", "current_item");
    CheckEq(out.current_tarefa, "item_2_task_1", "current_tarefa");
    CheckEqInt(out.wrong_answer_count, 1, "wrong_answer_count");
    Check(!out.adult_intervention_required, "adult_intervention_required");
    CheckEq(out.audio_url, "/api/media/turn_0123456789abcdef0123456789abcdef_audio.wav",
            "audio_url");
    CheckEq(out.image_url, "/api/media/turn_0123456789abcdef0123456789abcdef_image.jpg",
            "image_url");
    CheckEq(out.request_id, "3f2504e0-4f89-41d3-9a0c-0305e82c3301", "request_id ecoado");
}

void TestVerdicts() {
    std::printf("veredictos da lista fechada\n");
    struct Case {
        const char* text;
        PvVerdict expected;
    };
    const Case cases[] = {
        {"correct", PvVerdict::Correct},
        {"wrong", PvVerdict::Wrong},
        {"teach", PvVerdict::Teach},
        {"unidentifiable", PvVerdict::Unidentifiable},
    };
    for (const Case& item : cases) {
        PvTurnResponse out;
        std::string json = MakeTurn({{"veredicto", std::string("\"") + item.text + "\""}});
        Check(ParseTurnResponse(json.c_str(), out), "aceita veredicto do contrato");
        Check(out.veredicto == item.expected, "veredicto mapeado");
        CheckEq(PvVerdictName(out.veredicto), item.text, "PvVerdictName é estável");
    }

    ExpectReject(MakeTurn({{"veredicto", "\"maybe\""}}), "veredicto fora do enum");
    ExpectReject(MakeTurn({{"veredicto", "\"Correct\""}}), "veredicto com caixa diferente");
    ExpectReject(MakeTurn({{"veredicto", "null"}}), "veredicto null");
    ExpectReject(MakeTurn({{"veredicto", "3"}}), "veredicto numérico");
    ExpectReject(MakeTurn({}, {"veredicto"}), "veredicto ausente");
}

void TestSessionStatus() {
    std::printf("session_status da lista fechada\n");
    ExpectAccept(MakeTurn({{"session_status", "\"completed\""}}), "aceita completed");
    ExpectReject(MakeTurn({{"session_status", "\"closed\""}}), "session_status closed");
    ExpectReject(MakeTurn({{"session_status", "\"expired\""}}), "session_status expired");
    ExpectReject(MakeTurn({{"session_status", "null"}}), "session_status null");
    ExpectReject(MakeTurn({}, {"session_status"}), "session_status ausente");
}

void TestBase64MustBeEmpty() {
    std::printf("perfil media=url: base64 vazios\n");
    ExpectReject(MakeTurn({}, {"audio_base64"}), "audio_base64 ausente");
    ExpectReject(MakeTurn({{"audio_base64", "\"UklGRg==\""}}), "audio_base64 preenchido");
    ExpectReject(MakeTurn({{"audio_base64", "null"}}), "audio_base64 null");
    ExpectReject(MakeTurn({}, {"image_base64"}), "image_base64 ausente");
    ExpectReject(MakeTurn({{"image_base64", "\"/9j/4AAQ\""}}), "image_base64 preenchido");
}

void TestMediaFields() {
    std::printf("URLs e formatos de mídia\n");
    ExpectReject(MakeTurn({}, {"audio_url"}), "audio_url ausente");
    ExpectReject(MakeTurn({{"audio_url", "\"\""}}), "audio_url vazia");
    ExpectReject(MakeTurn({{"audio_url", "null"}}), "audio_url null");
    ExpectReject(MakeTurn({}, {"image_url"}), "image_url ausente");
    ExpectReject(MakeTurn({{"image_url", "\"\""}}), "image_url vazia");
    ExpectReject(MakeTurn({{"image_url", "null"}}), "image_url null");

    ExpectReject(MakeTurn({{"audio_format", "\"mp3\""}}), "audio_format mp3");
    ExpectReject(MakeTurn({}, {"audio_format"}), "audio_format ausente");
    ExpectReject(MakeTurn({{"image_format", "\"png\""}}), "image_format png");
    ExpectReject(MakeTurn({}, {"image_format"}), "image_format ausente");
}

// A URL da mídia decide PARA ONDE o token do dispositivo vai: o download o
// manda no cabeçalho. Por isso o parser só aceita caminho relativo sob a rota
// fixa "/api/media/" — qualquer outra forma tiraria a credencial da origem que
// o adulto configurou (revisão F3, P1c).
void TestMediaUrlOrigin() {
    std::printf("origem da mídia travada em /api/media/\n");
    const char* kOutside[] = {
        "\"http://mau.exemplo.com/api/media/x.wav\"",   // absoluta http
        "\"https://mau.exemplo.com/api/media/x.wav\"",  // absoluta https
        "\"HTTP://MAU.EXEMPLO.COM/api/media/x.wav\"",   // absoluta em maiúsculas
        "\"//mau.exemplo.com/api/media/x.wav\"",        // relativa a protocolo
        "\"/outro/caminho.wav\"",                       // fora da rota de mídia
        "\"/api/media\"",                               // prefixo incompleto
        "\"/api/media/\"",                              // sem nome de arquivo
        "\"api/media/x.wav\"",                          // sem barra inicial
        "\"/API/MEDIA/x.wav\"",                         // rota com outra caixa
        "\" /api/media/x.wav\"",                        // espaço antes da barra
    };
    for (const char* value : kOutside) {
        ExpectReject(MakeTurn({{"audio_url", value}}), "audio_url fora de /api/media/");
        ExpectReject(MakeTurn({{"image_url", value}}), "image_url fora de /api/media/");
    }

    // O caminho do contrato continua aceito, e chega intacto ao chamador.
    PvTurnResponse out;
    const std::string audio = "/api/media/turn_0123456789abcdef0123456789abcdef_audio.wav";
    const std::string image = "/api/media/turn_0123456789abcdef0123456789abcdef_image.jpg";
    std::string json =
        MakeTurn({{"audio_url", "\"" + audio + "\""}, {"image_url", "\"" + image + "\""}});
    Check(ParseTurnResponse(json.c_str(), out), "caminho do contrato aceito");
    CheckEq(out.audio_url, audio, "audio_url preservada");
    CheckEq(out.image_url, image, "image_url preservada");
}

void TestRequestId() {
    std::printf("eco do request_id\n");
    ExpectReject(MakeTurn({}, {"request_id"}), "request_id ausente");
    ExpectReject(MakeTurn({{"request_id", "\"\""}}), "request_id vazio");
    ExpectReject(MakeTurn({{"request_id", "null"}}), "request_id null (turno v1)");
    ExpectReject(MakeTurn({{"request_id", "12345"}}), "request_id numérico");
}

void TestScalars() {
    std::printf("escalares com tipo exato\n");
    ExpectReject(MakeTurn({{"wrong_answer_count", "\"3\""}}), "wrong_answer_count string");
    ExpectReject(MakeTurn({}, {"wrong_answer_count"}), "wrong_answer_count ausente");
    ExpectReject(MakeTurn({{"wrong_answer_count", "null"}}), "wrong_answer_count null");
    ExpectReject(MakeTurn({}, {"adult_intervention_required"}),
                 "adult_intervention_required ausente");
    ExpectReject(MakeTurn({{"adult_intervention_required", "\"true\""}}),
                 "adult_intervention_required string");
    ExpectReject(MakeTurn({{"adult_intervention_required", "1"}}),
                 "adult_intervention_required numérico");

    // Número INTEIRO de verdade (revisão F3, P2c): o valueint do cJSON é o
    // double truncado e saturado, então sem a comparação exata 1.5 viraria 1 e
    // 1e12 viraria INT_MAX — contadores que o servidor nunca mandou.
    ExpectReject(MakeTurn({{"wrong_answer_count", "1.5"}}), "wrong_answer_count fracionário");
    ExpectReject(MakeTurn({{"wrong_answer_count", "-0.5"}}),
                 "wrong_answer_count fracionário negativo");
    ExpectReject(MakeTurn({{"wrong_answer_count", "1e12"}}), "wrong_answer_count acima de int");
    ExpectReject(MakeTurn({{"wrong_answer_count", "-1e12"}}), "wrong_answer_count abaixo de int");

    PvTurnResponse whole;
    Check(ParseTurnResponse(MakeTurn({{"wrong_answer_count", "3.0"}}).c_str(), whole),
          "aceita 3.0 (inteiro escrito com ponto)");
    CheckEqInt(whole.wrong_answer_count, 3, "3.0 vira 3");
    Check(ParseTurnResponse(MakeTurn({{"wrong_answer_count", "0"}}).c_str(), whole), "aceita zero");
    CheckEqInt(whole.wrong_answer_count, 0, "zero preservado");

    PvTurnResponse out;
    std::string json = MakeTurn({{"adult_intervention_required", "true"},
                                 {"wrong_answer_count", "3"},
                                 {"session_status", "\"completed\""}});
    Check(ParseTurnResponse(json.c_str(), out), "aceita failsafe + sessão concluída");
    Check(out.adult_intervention_required, "adult_intervention_required verdadeiro");
    CheckEqInt(out.wrong_answer_count, 3, "wrong_answer_count 3");
    CheckEq(out.session_status, "completed", "session_status completed");
}

void TestPosition() {
    std::printf("posição pós-turno (§9.4)\n");
    ExpectReject(MakeTurn({}, {"current_item"}), "current_item ausente");
    ExpectReject(MakeTurn({}, {"current_tarefa"}), "current_tarefa ausente");
    ExpectReject(MakeTurn({{"current_item", "null"}}), "current_item null");
    ExpectReject(MakeTurn({{"current_tarefa", "null"}}), "current_tarefa null");

    // Vazio é aceito: sessão sem próxima tarefa ainda precisa ser exibida, e
    // "" também difere da posição anterior, que é o que o §9.4 compara.
    PvTurnResponse out;
    std::string json = MakeTurn({{"current_item", "\"\""}, {"current_tarefa", "\"\""}});
    Check(ParseTurnResponse(json.c_str(), out), "aceita posição vazia");
    Check(out.current_item.empty() && out.current_tarefa.empty(), "posição vazia preservada");
}

void TestExplanation() {
    std::printf("texto_explicacao\n");
    ExpectReject(MakeTurn({}, {"texto_explicacao"}), "texto_explicacao ausente");
    ExpectReject(MakeTurn({{"texto_explicacao", "null"}}), "texto_explicacao null");
    PvTurnResponse out;
    std::string json = MakeTurn({{"texto_explicacao", "\"\""}});
    Check(ParseTurnResponse(json.c_str(), out), "aceita texto_explicacao vazio");
    Check(out.texto_explicacao.empty(), "texto vazio preservado");
}

void TestAdditiveAndMalformed() {
    std::printf("campos extras e corpos malformados\n");
    // O contrato é aditivo: chave desconhecida não pode derrubar o turno.
    PvTurnResponse out;
    std::string json = MakeTurn({{"campo_futuro", "{\"x\":[1,2,3]}"}, {"outro", "\"valor\""}});
    Check(ParseTurnResponse(json.c_str(), out), "tolera campos desconhecidos");
    CheckEq(out.request_id, "3f2504e0-4f89-41d3-9a0c-0305e82c3301", "extras não afetam o eco");

    ExpectReject("{\"veredicto\": ", "JSON inválido (truncado)");
    ExpectReject("não é json", "JSON inválido (texto solto)");
    ExpectReject("[]", "raiz array");
    ExpectReject("\"correct\"", "raiz string");
    ExpectReject("{}", "objeto vazio");

    PvTurnResponse null_out;
    Check(!ParseTurnResponse(nullptr, null_out), "ponteiro nulo rejeitado");
}

}  // namespace

int main() {
    std::printf("ParseTurnResponse — teste de host\n");
    TestValidResponse();
    TestVerdicts();
    TestSessionStatus();
    TestBase64MustBeEmpty();
    TestMediaFields();
    TestMediaUrlOrigin();
    TestRequestId();
    TestScalars();
    TestPosition();
    TestExplanation();
    TestAdditiveAndMalformed();

    std::printf("\n%d verificações, %d falha(s)\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("FALHOU\n");
    return 1;
}
