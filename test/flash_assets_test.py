"""Verify that Flash Tool labels match the Paramix Menu font at each scale."""
import importlib.util
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("flash_status", ROOT / "flash/src/gen_status.py")
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)


def decode(mask):
    return [[bool(byte & (128 >> bit)) for byte in mask[start:start + 16] for bit in range(8)]
            for start in range(0, len(mask), 16)]


def check_label(mask, label, scale):
    width = (len(label) * 6 - 1) * scale
    left = (128 - width) // 2
    assert len(mask) == 112 * scale
    for y, row in enumerate(decode(mask)):
        expected = [False] * 128
        for index, char in enumerate(label):
            glyph = font[ord(char) - 32][y // scale]
            for x in range(5):
                start = left + (index * 6 + x) * scale
                expected[start:start + scale] = [bool(glyph & (16 >> x))] * scale
        assert row == expected, label


source = gen.ASSETS.read_text()
font = gen.load_font()
for name, label, scale in (gen.TITLE, gen.SUBTITLE, *gen.STATUSES):
    match = re.search(name + r"\[\d+\]=\{([^}]+)", source)
    check_label(bytes(int(value, 16) for value in match[1].split(",")), label, scale)

values = bytes(int(value, 16) for value in re.findall(r"0x[0-9a-fA-F]+", gen.HINTS.read_text()))
assert len(values) == len(gen.RESET_HINTS) * 112
for i, label in enumerate(gen.RESET_HINTS):
    check_label(values[i * 112:(i + 1) * 112], label, 1)

before = (gen.ASSETS.read_bytes(), gen.HINTS.read_bytes())
gen.main()
assert before == (gen.ASSETS.read_bytes(), gen.HINTS.read_bytes()), "generation is not deterministic"
print("PASS: Flash Tool title, status and reset labels match the Menu font")
