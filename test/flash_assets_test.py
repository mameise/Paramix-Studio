"""Lossless status packing: compare against pre-compression pixel hashes."""
import hashlib
import importlib.util
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("flash_status", ROOT / "flash/src/gen_status.py")
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)


def array(source, name):
    match = re.search(name + r"\[\d+\]=\{([^}]+)", source)
    return [int(value, 16) for value in match[1].split(",")]


def digest(pixels):
    return hashlib.sha256(struct.pack("<" + "H" * len(pixels), *pixels)).hexdigest()


source = gen.ASSETS.read_text()
assert digest(array(source, "static_bg")) == "dabd0034025c708d7c58b5423143aa3139c63b328d532ba89621a066e9af5e69"
hashes = {
    "st_ready": "8c61a40844b5811dd21f8d7adcd64d1a6f6a8cba9bb8d086c53c3085b5c422f5",
    "st_reading": "6c51f214e080cfd79af610071c97de1e47bbf604d7f2d6923e078691ee0770a3",
    "st_writing": "1e1df08b967253aace4ad2c677a56e778a891e6062011a1323e3cf30880f987d",
    "st_done": "83997593ff0a107d22a717484a4bb7368482deb48e392b03abf5be03ea08f624",
}
font = gen.load_font()
for _, name, label, color in gen.STATUSES:
    packed = array(source, name)
    assert len(packed) == 258 and packed[0] | (packed[1] << 8) == color
    pixels = [color if packed[2 + i // 8] & (0x80 >> (i % 8)) else gen.BACKGROUND for i in range(2048)]
    assert pixels == gen.render(label, color, font)
    assert digest(pixels) == hashes[name], name + " changed visible pixels"

hints = gen.HINTS.read_text()
values = [int(value, 16) for value in re.findall(r"0x[0-9a-fA-F]+", hints)]
assert len(values) == 3 * 112
for i, label in enumerate(gen.RESET_HINTS):
    assert bytes(values[i * 112:(i + 1) * 112]) == gen.render_hint(label, font)
    assert any(values[i * 112:(i + 1) * 112])

before = (gen.ASSETS.read_bytes(), gen.HINTS.read_bytes())
gen.main()
assert before == (gen.ASSETS.read_bytes(), gen.HINTS.read_bytes()), "generation is not deterministic"
print("PASS: lossless status pixels/colors, unchanged background, reset hint masks and deterministic generation")
