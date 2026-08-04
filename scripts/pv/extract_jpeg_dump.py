#!/usr/bin/env python3
"""Reconstrói no Mac o JPEG despejado pelo firmware do Professor Virtual.

PROVISÓRIO DA F2 — este script existe para o caminho de validação off-device da
legibilidade da foto (decisão F2-LegibilityValidation) e sai junto com o
`main/professor_virtual/pv_photo_dump.{h,cc}`.

Como capturar o dump (a foto só é despejada quando alguém toca em
"Exportar (diagnóstico)" na tela de revisão da câmera — nunca sozinha):

    idf.py -p /dev/tty.usbmodem* monitor | tee /tmp/pv.log
    # ou, sem o IDF:
    screen -L -Logfile /tmp/pv.log /dev/tty.usbmodem* 115200

Depois:

    python3 scripts/pv/extract_jpeg_dump.py /tmp/pv.log

O bloco no console tem este formato exato:

    PV-JPEG-BEGIN len=<bytes> crc32=<hex8> w=<largura> h=<altura>
    <base64 em linhas de 120 colunas>
    ...
    PV-JPEG-END

O script pega o ÚLTIMO bloco completo do arquivo, decodifica o base64, confere
o tamanho e o CRC-32 (zlib.crc32, o mesmo que o esp_rom_crc32_le do firmware
produz com semente 0) e salva `foto_<w>x<h>.jpg` ao lado do log.

Python 3 puro: nenhuma dependência externa.
"""

from __future__ import annotations

import argparse
import base64
import binascii
import os
import re
import sys
import zlib

BEGIN_RE = re.compile(
    r"PV-JPEG-BEGIN\s+len=(\d+)\s+crc32=([0-9a-fA-F]{1,8})\s+w=(\d+)\s+h=(\d+)"
)
END_MARK = "PV-JPEG-END"

# Linhas de base64 do dump. O monitor serial pode intercalar linhas de log
# (ESP_LOGx) no meio do bloco: aceitar só linhas 100% base64 é o que torna a
# extração robusta sem precisar desligar o log durante a exportação.
B64_RE = re.compile(r"^[A-Za-z0-9+/]+={0,2}$")


class DumpError(Exception):
    """Falha de extração com mensagem já pronta para o operador."""


def find_last_block(lines: list[str]) -> tuple[dict, list[str]]:
    """Devolve (cabeçalho, linhas base64) do último bloco COMPLETO do log."""
    best: tuple[dict, list[str]] | None = None
    header: dict | None = None
    payload: list[str] = []

    for raw in lines:
        line = raw.strip()
        match = BEGIN_RE.search(line)
        if match:
            # Um BEGIN novo abandona um bloco anterior sem END (dump abortado
            # ou log truncado no meio).
            header = {
                "len": int(match.group(1)),
                "crc32": int(match.group(2), 16),
                "width": int(match.group(3)),
                "height": int(match.group(4)),
            }
            payload = []
            continue
        if header is None:
            continue
        if END_MARK in line:
            best = (header, payload)
            header = None
            payload = []
            continue
        if B64_RE.match(line):
            payload.append(line)

    if best is None:
        if header is not None:
            raise DumpError(
                "bloco PV-JPEG-BEGIN encontrado sem PV-JPEG-END: "
                "a captura do console foi interrompida no meio do dump"
            )
        raise DumpError("nenhum bloco PV-JPEG-BEGIN/END encontrado no arquivo")
    return best


def extract(path: str, out_dir: str | None) -> int:
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            lines = handle.readlines()
    except OSError as exc:
        print(f"FALHA: não consegui ler {path}: {exc}", file=sys.stderr)
        return 2

    try:
        header, payload = find_last_block(lines)
    except DumpError as exc:
        print(f"FALHA: {exc}", file=sys.stderr)
        return 2

    if not payload:
        print("FALHA: bloco sem nenhuma linha de base64", file=sys.stderr)
        return 2

    try:
        data = base64.b64decode("".join(payload), validate=True)
    except (binascii.Error, ValueError) as exc:
        print(f"FALHA: base64 inválido: {exc}", file=sys.stderr)
        return 2

    expected_len = header["len"]
    expected_crc = header["crc32"]
    actual_crc = zlib.crc32(data) & 0xFFFFFFFF

    print(f"bloco: len={expected_len} crc32={expected_crc:08x} "
          f"w={header['width']} h={header['height']}")
    print(f"decodificado: {len(data)} bytes, crc32={actual_crc:08x}")

    if len(data) != expected_len:
        print(
            f"FALHA: tamanho não confere (esperado {expected_len}, obtido {len(data)}). "
            "O console provavelmente perdeu linhas — repita a exportação.",
            file=sys.stderr,
        )
        return 1
    if actual_crc != expected_crc:
        print(
            f"FALHA: CRC-32 não confere (esperado {expected_crc:08x}, "
            f"obtido {actual_crc:08x}). Os bytes chegaram corrompidos.",
            file=sys.stderr,
        )
        return 1
    if not data.startswith(b"\xff\xd8"):
        print("AVISO: os dois primeiros bytes não são SOI (FFD8); "
              "o arquivo pode não ser um JPEG válido.")

    base = out_dir if out_dir else os.path.dirname(os.path.abspath(path))
    os.makedirs(base, exist_ok=True)
    out_path = os.path.join(base, f"foto_{header['width']}x{header['height']}.jpg")
    with open(out_path, "wb") as handle:
        handle.write(data)

    print(f"OK: {len(data)} bytes íntegros (len e CRC-32 conferem)")
    print(f"salvo em: {out_path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Extrai o JPEG despejado pelo Professor Virtual no console serial."
    )
    parser.add_argument("log", help="arquivo com a captura do console serial")
    parser.add_argument(
        "-o",
        "--out-dir",
        default=None,
        help="diretório de saída (padrão: o diretório do log)",
    )
    args = parser.parse_args()
    return extract(args.log, args.out_dir)


if __name__ == "__main__":
    sys.exit(main())
