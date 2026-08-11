"""Testes de host da Fase 2 (câmera) do Professor Virtual.

Cobrem as duas partes da F2 que NÃO precisam de placa nem de ESP-IDF:

1. `scripts/pv/extract_jpeg_dump.py`, o outro lado do dump serial de
   diagnóstico. É o caminho canônico de validação da legibilidade da foto, e
   uma regressão silenciosa nele (aceitar bytes corrompidos, pegar o bloco
   errado) invalidaria a medição física sem que ninguém percebesse.
2. O contrato de sensor no `config.json` da placa: as variantes do Professor
   Virtual dependem do modo RAW10 1280x960 binning; as demais variantes da
   mesma placa NÃO podem ser arrastadas para esse modo, porque o assistente
   XiaoZhi delas foi validado no RAW8 800x800.

A cobertura em C++ dos módulos com fakes de Camera/fila fica registrada para a
F8 (infraestrutura de teste do firmware).
"""

import base64
import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "extract_jpeg_dump", ROOT / "scripts" / "pv" / "extract_jpeg_dump.py"
)
DUMP = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(DUMP)

BOARD_CONFIG = (
    ROOT
    / "main"
    / "boards"
    / "waveshare"
    / "esp32-p4-wifi6-touch-lcd"
    / "config.json"
)

# Mesma largura de linha que o firmware usa (pv_photo_dump.cc).
LINE_CHARS = 120

# Bytes de exemplo com SOI de JPEG: o script avisa (sem falhar) quando os dois
# primeiros bytes não são FFD8, e não queremos esse ruído nos testes.
def sample_jpeg(size: int, seed: int) -> bytes:
    body = bytes((seed * 31 + i * 7) % 256 for i in range(size - 2))
    return b"\xff\xd8" + body


def render_block(
    data: bytes,
    width: int = 1280,
    height: int = 960,
    declared_len: int | None = None,
    declared_crc: int | None = None,
    with_end: bool = True,
) -> list[str]:
    """Monta o bloco exatamente como o firmware o imprime no console."""
    crc = zlib.crc32(data) & 0xFFFFFFFF if declared_crc is None else declared_crc
    length = len(data) if declared_len is None else declared_len
    lines = [f"PV-JPEG-BEGIN len={length} crc32={crc:08x} w={width} h={height}"]
    encoded = base64.b64encode(data).decode("ascii")
    for start in range(0, len(encoded), LINE_CHARS):
        lines.append(encoded[start : start + LINE_CHARS])
    if with_end:
        lines.append("PV-JPEG-END")
    return lines


def write_log(directory: Path, *chunks: list[str]) -> Path:
    path = directory / "pv.log"
    body = "\n".join("\n".join(chunk) for chunk in chunks) + "\n"
    path.write_text(body, encoding="utf-8")
    return path


def run_extract(log_path: Path, out_dir: Path, all_blocks: bool = False) -> int:
    """Executa o script silenciando o relatório para o operador."""
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return DUMP.extract(str(log_path), str(out_dir), all_blocks)


class ExtractJpegDumpTest(unittest.TestCase):
    def test_intact_block_round_trips_byte_for_byte(self):
        data = sample_jpeg(5000, seed=1)
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            log = write_log(base, render_block(data))
            self.assertEqual(run_extract(log, base), 0)
            saved = base / "foto_1280x960.jpg"
            self.assertTrue(saved.exists())
            self.assertEqual(saved.read_bytes(), data)

    def test_corrupted_crc_is_rejected(self):
        data = sample_jpeg(3000, seed=2)
        good_crc = zlib.crc32(data) & 0xFFFFFFFF
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            log = write_log(
                base, render_block(data, declared_crc=good_crc ^ 0xFFFFFFFF)
            )
            self.assertNotEqual(run_extract(log, base), 0)
            self.assertFalse((base / "foto_1280x960.jpg").exists())

    def test_block_without_end_marker_is_rejected(self):
        data = sample_jpeg(3000, seed=3)
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            lines = render_block(data, with_end=False)
            # Dump interrompido no meio: além de não ter END, o log acaba antes
            # da última linha de base64.
            log = write_log(base, lines[:-3])
            self.assertNotEqual(run_extract(log, base), 0)
            self.assertFalse((base / "foto_1280x960.jpg").exists())

    def test_interleaved_log_lines_do_not_corrupt_the_payload(self):
        data = sample_jpeg(4096, seed=4)
        lines = render_block(data)
        noisy = [lines[0]]
        for index, line in enumerate(lines[1:-1]):
            noisy.append(line)
            if index % 5 == 0:
                # Linhas de ESP_LOGx têm espaços e parênteses: nunca casam com
                # o regex de base64 puro do script.
                noisy.append(f"I (1234{index}) PvCamera: preview frame {index}")
        noisy.append(lines[-1])
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            log = write_log(base, noisy)
            self.assertEqual(run_extract(log, base), 0)
            self.assertEqual((base / "foto_1280x960.jpg").read_bytes(), data)

    def test_two_blocks_use_the_last_one(self):
        first = sample_jpeg(2048, seed=5)
        second = sample_jpeg(3072, seed=6)
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            log = write_log(
                base,
                render_block(first, width=800, height=800),
                render_block(second, width=1280, height=960),
            )
            self.assertEqual(run_extract(log, base), 0)
            self.assertEqual((base / "foto_1280x960.jpg").read_bytes(), second)
            self.assertFalse((base / "foto_800x800.jpg").exists())


class ExtractAllBlocksTest(unittest.TestCase):
    """`--all` é o modo do spike UVC da F2B: um JPEG por degrau da escada.

    Um degrau que chega corrompido não pode levar junto os degraus bons — a
    bancada custa flash + minutos de dump serial por resolução, e repetir tudo
    por causa de um bloco perdido inviabiliza a medição.
    """

    def test_all_flag_extracts_every_block_in_log_order(self):
        rungs = ((800, 600, 2048), (1920, 1080, 3072), (2592, 1944, 4096))
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            payloads = [sample_jpeg(size, seed=index) for index, (_, _, size) in enumerate(rungs)]
            log = write_log(
                base,
                *[
                    render_block(data, width=width, height=height)
                    for data, (width, height, _) in zip(payloads, rungs)
                ],
            )
            self.assertEqual(run_extract(log, base, all_blocks=True), 0)
            for index, (data, (width, height, _)) in enumerate(zip(payloads, rungs), start=1):
                saved = base / f"foto_{index:02d}_{width}x{height}.jpg"
                self.assertTrue(saved.exists(), saved.name)
                self.assertEqual(saved.read_bytes(), data)

    def test_all_flag_survives_a_corrupted_block_in_the_middle(self):
        first = sample_jpeg(2048, seed=11)
        broken = sample_jpeg(2560, seed=12)
        last = sample_jpeg(3072, seed=13)
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            log = write_log(
                base,
                render_block(first, width=800, height=600),
                render_block(
                    broken,
                    width=1920,
                    height=1080,
                    declared_crc=zlib.crc32(broken) ^ 0xFFFFFFFF,
                ),
                render_block(last, width=2592, height=1944),
            )
            # Saída != 0 avisa o operador do bloco perdido, mas os íntegros já
            # estão no disco — e o índice do terceiro continua sendo 03.
            self.assertNotEqual(run_extract(log, base, all_blocks=True), 0)
            self.assertEqual((base / "foto_01_800x600.jpg").read_bytes(), first)
            self.assertFalse((base / "foto_02_1920x1080.jpg").exists())
            self.assertEqual((base / "foto_03_2592x1944.jpg").read_bytes(), last)

    def test_all_flag_ignores_interleaved_log_lines(self):
        first = sample_jpeg(3000, seed=21)
        second = sample_jpeg(3600, seed=22)
        blocks = []
        for data, (width, height) in ((first, (800, 600)), (second, (3264, 2448))):
            lines = render_block(data, width=width, height=height)
            noisy = [lines[0]]
            for index, line in enumerate(lines[1:-1]):
                noisy.append(line)
                if index % 4 == 0:
                    noisy.append(f"I (5000{index}) PvUvcSpike: PV-UVC-WARMUP descartados={index}")
            noisy.append(lines[-1])
            blocks.append(noisy)
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            log = write_log(base, *blocks)
            self.assertEqual(run_extract(log, base, all_blocks=True), 0)
            self.assertEqual((base / "foto_01_800x600.jpg").read_bytes(), first)
            self.assertEqual((base / "foto_02_3264x2448.jpg").read_bytes(), second)

    def test_all_flag_without_any_block_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            log = write_log(base, ["I (100) PvUvcSpike: sem dump nenhum aqui"])
            self.assertNotEqual(run_extract(log, base, all_blocks=True), 0)


class BoardCameraFormatTest(unittest.TestCase):
    """O modo do sensor é parte do contrato da F2 e não pode vazar de volta."""

    RAW10_FLAGS = (
        "CONFIG_CAMERA_OV5647_MIPI_RAW10_1280X960_BINNING_45FPS=y",
        "CONFIG_CAMERA_OV5647_MIPI_DEFAULT_FMT_RAW10_1280X960_BINNING_45FPS=y",
    )
    RAW8_ON = "CONFIG_CAMERA_OV5647_MIPI_RAW8_800X800_50FPS=y"
    RAW8_OFF = "CONFIG_CAMERA_OV5647_MIPI_RAW8_800X800_50FPS=n"

    def setUp(self):
        self.builds = json.loads(BOARD_CONFIG.read_text(encoding="utf-8"))["builds"]
        self.assertTrue(self.builds)

    def test_professor_virtual_variants_use_raw10_1280x960(self):
        names = []
        for build in self.builds:
            if "professor-virtual" not in build["name"]:
                continue
            names.append(build["name"])
            appended = build["sdkconfig_append"]
            for flag in self.RAW10_FLAGS:
                self.assertIn(flag, appended, build["name"])
            self.assertIn(self.RAW8_OFF, appended, build["name"])
            self.assertNotIn(self.RAW8_ON, appended, build["name"])
        self.assertTrue(names, "nenhuma variante professor-virtual no config.json")

    def test_other_variants_keep_raw8_800x800(self):
        for build in self.builds:
            if "professor-virtual" in build["name"]:
                continue
            appended = build["sdkconfig_append"]
            self.assertIn(self.RAW8_ON, appended, build["name"])
            for flag in self.RAW10_FLAGS:
                self.assertNotIn(flag, appended, build["name"])


class UvcSpikeVariantTest(unittest.TestCase):
    """A variante do spike da F2B só é útil se vier com o caminho UVC ligado."""

    NAME = "esp32-p4-wifi6-touch-lcd-7b-professor-virtual-uvc-spike"
    REQUIRED = (
        "CONFIG_PV_UVC_SPIKE=y",
        "CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE=y",
        "CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=4096",
        "CONFIG_UVC_PRINTF_CONFIGURATION_DESCRIPTOR=y",
    )

    def test_spike_variant_enables_the_uvc_path(self):
        builds = json.loads(BOARD_CONFIG.read_text(encoding="utf-8"))["builds"]
        spike = [build for build in builds if build["name"] == self.NAME]
        self.assertEqual(len(spike), 1, f"variante {self.NAME} ausente ou duplicada")
        appended = spike[0]["sdkconfig_append"]
        for flag in self.REQUIRED:
            self.assertIn(flag, appended)
        # O gate depende de PROFESSOR_VIRTUAL no Kconfig: sem ele o spike nem
        # entra no build (o glob dos fontes do PV também some).
        self.assertIn("CONFIG_PROFESSOR_VIRTUAL=y", appended)

    def test_no_other_variant_enables_the_spike(self):
        builds = json.loads(BOARD_CONFIG.read_text(encoding="utf-8"))["builds"]
        for build in builds:
            if build["name"] == self.NAME:
                continue
            self.assertNotIn("CONFIG_PV_UVC_SPIKE=y", build["sdkconfig_append"], build["name"])


if __name__ == "__main__":
    unittest.main()
