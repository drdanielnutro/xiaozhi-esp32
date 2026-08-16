#!/usr/bin/env bash
# Compila e roda os testes de host do Professor Virtual (sem ESP-IDF):
#   - test_session_mirror: parsers do contrato (§7.2/§7.3) — precisa de cJSON;
#   - test_turn_response: parser ESTRITO da resposta do turno (§7.5, perfil
#     v1.1) — binário próprio para separar o regime estrito do tolerante;
#   - test_pv_wav: parser RIFF/WAVE da voz do turno — binário SEPARADO, sem
#     cJSON, porque o pv_wav é módulo puro e não deve ganhar dependência
#     nenhuma nem no teste.
# Uso: bash main/professor_virtual/host_test/run.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PV_DIR="$(cd "${HERE}/.." && pwd)"
REPO_ROOT="$(cd "${PV_DIR}/../.." && pwd)"
CJSON_DIR="${REPO_ROOT}/managed_components/espressif__cjson/cJSON"
OUT_DIR="${HERE}/build"

if [ ! -f "${CJSON_DIR}/cJSON.c" ]; then
    echo "cJSON não encontrado em ${CJSON_DIR}; rode um build do firmware antes." >&2
    exit 1
fi

mkdir -p "${OUT_DIR}"

SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"

# No macOS, /usr/bin/clang++ só acha os cabeçalhos da libc++ com o SDK
# explícito; em Linux a variável fica vazia e nada muda.
SYSROOT=""
CXX_STDLIB_INC=""
if command -v xcrun >/dev/null 2>&1; then
    SDK_PATH="$(xcrun --show-sdk-path 2>/dev/null || true)"
    if [ -n "${SDK_PATH}" ] && [ -d "${SDK_PATH}" ]; then
        SYSROOT="-isysroot ${SDK_PATH}"
        # Command Line Tools parciais não trazem libc++ em usr/include/c++/v1;
        # nesse caso apontamos a cópia do SDK.
        if [ -d "${SDK_PATH}/usr/include/c++/v1" ]; then
            CXX_STDLIB_INC="-isystem ${SDK_PATH}/usr/include/c++/v1"
        fi
    fi
fi

# cJSON é código de terceiros: compilado como C e sem os avisos do projeto.
/usr/bin/clang -std=c99 -w -g ${SAN} ${SYSROOT} -I "${CJSON_DIR}" \
    -c "${CJSON_DIR}/cJSON.c" -o "${OUT_DIR}/cJSON.o"

/usr/bin/clang++ -std=c++17 -Wall -Wextra -Werror -g ${SAN} ${SYSROOT} ${CXX_STDLIB_INC} \
    -I "${CJSON_DIR}" -I "${PV_DIR}" \
    -o "${OUT_DIR}/test_session_mirror" \
    "${HERE}/test_session_mirror.cc" \
    "${PV_DIR}/pv_session_mirror.cc" \
    "${PV_DIR}/pv_route_text.cc" \
    "${OUT_DIR}/cJSON.o"

/usr/bin/clang++ -std=c++17 -Wall -Wextra -Werror -g ${SAN} ${SYSROOT} ${CXX_STDLIB_INC} \
    -I "${CJSON_DIR}" -I "${PV_DIR}" \
    -o "${OUT_DIR}/test_turn_response" \
    "${HERE}/test_turn_response.cc" \
    "${PV_DIR}/pv_session_mirror.cc" \
    "${OUT_DIR}/cJSON.o"

/usr/bin/clang++ -std=c++17 -Wall -Wextra -Werror -g ${SAN} ${SYSROOT} ${CXX_STDLIB_INC} \
    -I "${PV_DIR}" \
    -o "${OUT_DIR}/test_pv_wav" \
    "${HERE}/test_pv_wav.cc" \
    "${PV_DIR}/pv_wav.cc"

"${OUT_DIR}/test_session_mirror"
"${OUT_DIR}/test_turn_response"
"${OUT_DIR}/test_pv_wav"
