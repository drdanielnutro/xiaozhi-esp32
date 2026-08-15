#!/usr/bin/env bash
#
# Grava SÓ a partição do aplicativo, preservando a NVS (Wi-Fi, URL do backend
# e token do Professor Virtual).
#
# Por que existe: o `merged-binary.bin` do release começa em 0x0 e cobre
# ~13,5 MB, passando por cima da partição `nvs` (0x3b000) — por isso cada
# gravação por ele obriga a reconfigurar Wi-Fi e backend do zero. O app mora
# em ota_0 (0x200000); gravar só ele mantém a placa configurada. Use o
# merged-binary quando quiser justamente uma placa de fábrica (ou quando a
# tabela de partições/bootloader mudarem).
#
# Uso:
#   scripts/pv/flash_app.sh [caminho-do-app.bin] [porta-serial]
#
# Sem argumentos: usa build/xiaozhi.bin e detecta a porta /dev/cu.usbmodem*.
# Feche qualquer monitor serial antes (a porta precisa estar livre).

set -euo pipefail

# Offsets desta placa (partitions/v2/32m.csv; confira com
# gen_esp32part.py build/partition_table/partition-table.bin se a tabela mudar)
readonly APP_OFFSET=0x200000    # ota_0
readonly OTADATA_OFFSET=0x10d000
readonly CHIP=esp32p4
readonly BAUD=460800

readonly REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly IDF_PYTHON="$HOME/.espressif/tools/python/v6.0.2/venv/bin/python"

APP_BIN="${1:-$REPO_ROOT/build/xiaozhi.bin}"
PORT="${2:-}"

if [[ ! -f "$APP_BIN" ]]; then
    echo "erro: app não encontrado: $APP_BIN" >&2
    echo "      gere-o com scripts/release.py ou passe o caminho como 1º argumento" >&2
    exit 1
fi

if [[ -z "$PORT" ]]; then
    # shellcheck disable=SC2012
    PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
    if [[ -z "$PORT" ]]; then
        echo "erro: nenhuma porta /dev/cu.usbmodem* encontrada; passe a porta como 2º argumento" >&2
        exit 1
    fi
fi

if [[ ! -x "$IDF_PYTHON" ]]; then
    echo "erro: Python do ESP-IDF 6.0.2 não encontrado em $IDF_PYTHON" >&2
    exit 1
fi

# otadata inicial força o boot por ota_0 (o slot que acabamos de gravar) mesmo
# se uma OTA anterior tiver apontado para ota_1. Fica fora da NVS.
OTADATA_BIN="$REPO_ROOT/build/ota_data_initial.bin"
FLASH_ARGS=("$APP_OFFSET" "$APP_BIN")
if [[ -f "$OTADATA_BIN" ]]; then
    FLASH_ARGS+=("$OTADATA_OFFSET" "$OTADATA_BIN")
fi

echo "app:   $APP_BIN"
echo "porta: $PORT"
echo "grava: ota_0 em $APP_OFFSET (NVS em 0x3b000 preservada)"

exec "$IDF_PYTHON" -m esptool --chip "$CHIP" -p "$PORT" -b "$BAUD" \
    write-flash "${FLASH_ARGS[@]}"
