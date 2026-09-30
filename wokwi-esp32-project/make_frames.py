#!/usr/bin/env python3
"""
Rebuilds the JPEG frames that are baked into chip.c.

Scene 0 : colour-bar test pattern
Scene 1 : potato leaf photo-ish scene
(scene 2 = simulated capture failure, no image needed)

Swap in your own pictures:
    python3 tools/make_frames.py my_photo.jpg other.jpg
Any image works; it is resized to WIDTH x HEIGHT and re-encoded as JPEG.

The arrays are written back into chip.c between the FRAMES markers.
"""
import io
import math
import random
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

WIDTH, HEIGHT = 320, 240
MAX_BYTES = 9000               # keep UART transfers snappy
CHIP = Path(__file__).resolve().parent.parent / "chip.c"
BEGIN = "// <<<FRAMES_BEGIN>>>"
END = "// <<<FRAMES_END>>>"


def color_bars() -> Image.Image:
    img = Image.new("RGB", (WIDTH, HEIGHT))
    d = ImageDraw.Draw(img)
    bars = [(192, 192, 192), (192, 192, 0), (0, 192, 192), (0, 192, 0),
            (192, 0, 192), (192, 0, 0), (0, 0, 192), (16, 16, 16)]
    w = WIDTH / len(bars)
    for i, c in enumerate(bars):
        d.rectangle([i * w, 0, (i + 1) * w, HEIGHT * 0.72], fill=c)
    for x in range(WIDTH):                      # grey ramp
        v = int(255 * x / WIDTH)
        d.rectangle([x, HEIGHT * 0.72, x + 1, HEIGHT * 0.86], fill=(v, v, v))
    d.rectangle([0, HEIGHT * 0.86, WIDTH, HEIGHT], fill=(24, 24, 24))
    d.text((8, HEIGHT - 22), "WOKWI VIRTUAL CAM  320x240", fill=(255, 255, 255))
    return img


def leaf() -> Image.Image:
    rnd = random.Random(11)
    img = Image.new("RGB", (WIDTH, HEIGHT), (56, 74, 42))
    d = ImageDraw.Draw(img)
    for _ in range(1500):
        x, y = rnd.randrange(WIDTH), rnd.randrange(HEIGHT)
        c = rnd.randrange(-18, 18)
        d.point((x, y), fill=(56 + c, 74 + c, 42 + c))
    d.ellipse([28, 34, 292, 208], fill=(64, 142, 58))
    d.polygon([(292, 121), (316, 121), (292, 150)], fill=(64, 142, 58))
    d.line([(34, 126), (300, 128)], fill=(146, 186, 92), width=4)
    for k in range(-5, 6):
        if k == 0:
            continue
        x0 = 70 + abs(k) * 18
        d.line([(x0, 127), (x0 + 60, 127 + k * 16)], fill=(120, 168, 80), width=2)
    for cx, cy, r in [(112, 88, 30), (196, 150, 38), (86, 168, 20)]:
        for step in range(10, 0, -1):
            rr = r * step / 10
            col = (168 - step * 4, 158 - step * 8, 60 - step * 4) if step > 7 else \
                  (92 - step * 3, 58 - step * 3, 30)
            d.ellipse([cx - rr, cy - rr * 0.8, cx + rr, cy + rr * 0.8], fill=col)
    for _ in range(240):
        a = rnd.random() * math.tau
        r = rnd.random() * 46
        d.point((196 + math.cos(a) * r, 150 + math.sin(a) * r * 0.8), fill=(70, 46, 24))
    return img.filter(ImageFilter.GaussianBlur(0.6))


def encode(img: Image.Image) -> bytes:
    img = img.convert("RGB").resize((WIDTH, HEIGHT))
    q = 40
    while True:
        buf = io.BytesIO()
        img.save(buf, format="JPEG", quality=q, optimize=True)
        data = buf.getvalue()
        if len(data) <= MAX_BYTES or q <= 8:
            return data
        q -= 4


def c_array(name: str, data: bytes) -> str:
    rows = ["  " + " ".join(f"0x{b:02x}," for b in data[i:i + 16])
            for i in range(0, len(data), 16)]
    return f"static const uint8_t {name}[{len(data)}] = {{\n" + "\n".join(rows) + "\n};\n"


def main() -> None:
    args = sys.argv[1:]
    images = [Image.open(a) for a in args[:2]] if args else [color_bars(), leaf()]
    while len(images) < 2:
        images.append(images[-1])

    block = [BEGIN, f"#define CAM_FRAME_WIDTH  {WIDTH}",
             f"#define CAM_FRAME_HEIGHT {HEIGHT}", ""]
    for idx, (name, img) in enumerate(zip(["frame_scene0", "frame_scene1"], images)):
        data = encode(img)
        print(f"scene {idx}: {len(data)} bytes")
        block.append(c_array(name, data))
    block.append(END)

    text = CHIP.read_text()
    start = text.index(BEGIN)
    stop = text.index(END) + len(END)
    CHIP.write_text(text[:start] + "\n".join(block) + text[stop:])
    print("patched", CHIP)


if __name__ == "__main__":
    main()
