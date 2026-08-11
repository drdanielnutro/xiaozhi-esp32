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

Com `--all` ele extrai TODOS os blocos completos do log, em ordem, salvando
`foto_<NN>_<w>x<h>.jpg` (o índice é a posição do bloco no log). É o modo do
spike UVC da F2B, que despeja um JPEG por degrau da escada de resoluções: cada
bloco é validado sozinho e um bloco corrompido não impede os demais de sair.

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


def find_blocks(lines: list[str]) -> tuple[list[tuple[dict, list[str]]], bool]:
    """Devolve (blocos COMPLETOS na ordem do log, sobrou_bloco_sem_END)."""
    blocks: list[tuple[dict, list[str]]] = []
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
            blocks.append((header, payload))
            header = None
            payload = []
            continue
        if B64_RE.match(line):
            payload.append(line)

    return blocks, header is not None


def find_last_block(lines: list[str]) -> tuple[dict, list[str]]:
    """Devolve (cabeçalho, linhas base64) do último bloco COMPLETO do log."""
    blocks, dangling = find_blocks(lines)
    if not blocks:
        if dangling:
            raise DumpError(
                "bloco PV-JPEG-BEGIN encontrado sem PV-JPEG-END: "
                "a captura do console foi interrompida no meio do dump"
            )
        raise DumpError("nenhum bloco PV-JPEG-BEGIN/END encontrado no arquivo")
    return blocks[-1]


def read_lines(path: str) -> list[str]:
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.readlines()


def save_block(header: dict, payload: list[str], out_path: str) -> int:
    """Valida um bloco e o grava. Devolve o código de saída DESTE bloco (0 = ok).

    2 = o bloco nem chegou a ser decodificado; 1 = decodificou mas os bytes não
    conferem (len ou CRC-32). Nada é gravado quando o resultado não é 0.
    """
    if not payload:
        print("FALHA: bloco sem nenhuma linha de base64", file=sys.stderr)
        return 2

    expected_len = header["len"]
    expected_crc = header["crc32"]
    print(f"bloco: len={expected_len} crc32={expected_crc:08x} "
          f"w={header['width']} h={header['height']}")

    try:
        data = base64.b64decode("".join(payload), validate=True)
    except (binascii.Error, ValueError) as exc:
        print(f"FALHA: base64 inválido: {exc}", file=sys.stderr)
        return 2

    actual_crc = zlib.crc32(data) & 0xFFFFFFFF
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

    with open(out_path, "wb") as handle:
        handle.write(data)

    print(f"OK: {len(data)} bytes íntegros (len e CRC-32 conferem)")
    print(f"salvo em: {out_path}")
    return 0


def extract(path: str, out_dir: str | None, all_blocks: bool = False) -> int:
    try:
        lines = read_lines(path)
    except OSError as exc:
        print(f"FALHA: não consegui ler {path}: {exc}", file=sys.stderr)
        return 2

    base = out_dir if out_dir else os.path.dirname(os.path.abspath(path))

    if not all_blocks:
        try:
            header, payload = find_last_block(lines)
        except DumpError as exc:
            print(f"FALHA: {exc}", file=sys.stderr)
            return 2
        os.makedirs(base, exist_ok=True)
        out_path = os.path.join(base, f"foto_{header['width']}x{header['height']}.jpg")
        return save_block(header, payload, out_path)

    blocks, dangling = find_blocks(lines)
    if not blocks:
        if dangling:
            print(
                "FALHA: bloco PV-JPEG-BEGIN encontrado sem PV-JPEG-END: "
                "a captura do console foi interrompida no meio do dump",
                file=sys.stderr,
            )
        else:
            print("FALHA: nenhum bloco PV-JPEG-BEGIN/END encontrado no arquivo", file=sys.stderr)
        return 2

    os.makedirs(base, exist_ok=True)
    saved = 0
    failed = 0
    # O índice é a POSIÇÃO do bloco no log, não a contagem dos que deram certo:
    # assim um degrau da escada que chegou corrompido não desloca a numeração
    # dos outros, e o nome do arquivo continua casando com a ordem do console.
    for index, (header, payload) in enumerate(blocks, start=1):
        out_path = os.path.join(
            base, f"foto_{index:02d}_{header['width']}x{header['height']}.jpg"
        )
        print(f"--- bloco {index}/{len(blocks)}")
        if save_block(header, payload, out_path) == 0:
            saved += 1
        else:
            failed += 1

    if dangling:
        print(
            "AVISO: o log termina com um PV-JPEG-BEGIN sem PV-JPEG-END; "
            "esse último dump ficou incompleto e foi ignorado."
        )
    print(f"--- total: {len(blocks)} bloco(s), {saved} salvo(s), {failed} com falha")
    return 0 if failed == 0 else 1


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
    parser.add_argument(
        "-a",
        "--all",
        action="store_true",
        dest="all_blocks",
        help="extrai TODOS os blocos completos do log (foto_<NN>_<w>x<h>.jpg), "
             "em vez de só o último",
    )
    args = parser.parse_args()
    return extract(args.log, args.out_dir, args.all_blocks)


if __name__ == "__main__":
    sys.exit(main())
