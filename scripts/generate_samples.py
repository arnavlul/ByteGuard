"""Generate deterministic, high-contrast demo PNG images for ByteGuard."""

from pathlib import Path
from PIL import Image, ImageDraw


def make_satellite_grid(out_path: Path) -> None:
    w, h = 96, 96
    img = Image.new("RGB", (w, h), (10, 15, 32))
    draw = ImageDraw.Draw(img)

    # Subtle telemetry grid
    for x in range(0, w, 12):
        draw.line([(x, 0), (x, h)], fill=(24, 42, 74), width=1)
    for y in range(0, h, 12):
        draw.line([(0, y), (w, y)], fill=(24, 42, 74), width=1)

    # Outer cyan targeting ring
    draw.ellipse([12, 12, 84, 84], outline=(0, 225, 255), width=2)
    draw.ellipse([22, 22, 74, 74], outline=(40, 140, 220), width=1)

    # Planetary sphere in center
    draw.ellipse([30, 30, 66, 66], fill=(22, 115, 210), outline=(120, 240, 255), width=2)
    # Continents / bands on sphere
    draw.arc([32, 34, 64, 62], start=20, end=160, fill=(60, 235, 180), width=3)
    draw.arc([32, 38, 64, 58], start=200, end=330, fill=(255, 200, 70), width=3)

    # Satellite solar wings & body at top-right
    draw.rectangle([66, 16, 86, 26], fill=(255, 195, 50), outline=(255, 245, 180))
    draw.rectangle([72, 13, 80, 29], fill=(230, 240, 255))

    # Telemetry bars at bottom
    colors = [(0, 240, 200), (0, 200, 255), (255, 195, 50), (255, 95, 120)]
    for i, col in enumerate(colors):
        x0 = 14 + i * 18
        draw.rectangle([x0, 82, x0 + 13, 89], fill=col)

    # Crosshair markers
    draw.line([(48, 6), (48, 16)], fill=(255, 205, 60), width=2)
    draw.line([(48, 80), (48, 90)], fill=(255, 205, 60), width=2)
    draw.line([(6, 48), (16, 48)], fill=(255, 205, 60), width=2)
    draw.line([(80, 48), (90, 48)], fill=(255, 205, 60), width=2)

    img.save(out_path, format="PNG")


def make_jiit_crest_demo(out_path: Path) -> None:
    w, h = 96, 96
    img = Image.new("RGB", (w, h), (242, 245, 250))
    draw = ImageDraw.Draw(img)

    # Corner accents
    draw.rectangle([0, 0, w - 1, h - 1], outline=(20, 50, 115), width=4)

    # Geometric shield
    shield = [(18, 14), (78, 14), (78, 52), (48, 84), (18, 52)]
    draw.polygon(shield, fill=(22, 58, 135), outline=(210, 160, 40))

    inner_shield = [(24, 20), (72, 20), (72, 50), (48, 76), (24, 50)]
    draw.polygon(inner_shield, fill=(35, 85, 175), outline=(255, 215, 80))

    # Torch flame & handle
    draw.rectangle([45, 42, 51, 66], fill=(245, 200, 65))
    draw.polygon([(41, 42), (55, 42), (52, 34), (44, 34)], fill=(255, 220, 90))
    draw.polygon([(48, 22), (56, 33), (48, 36), (40, 33)], fill=(255, 95, 45))
    draw.polygon([(48, 25), (52, 32), (48, 34), (44, 32)], fill=(255, 235, 110))

    # Crisp accent stripes
    draw.rectangle([28, 48, 40, 53], fill=(255, 255, 255))
    draw.rectangle([56, 48, 68, 53], fill=(255, 255, 255))

    img.save(out_path, format="PNG")


def make_landscape_demo(out_path: Path) -> None:
    w, h = 128, 96
    img = Image.new("RGB", (w, h))
    px = img.load()

    # Vibrant sunset sky gradient
    for y in range(h):
        t = y / float(h - 1)
        if t < 0.65:
            k = t / 0.65
            r = int(28 + k * 215)
            g = int(24 + k * 85)
            b = int(95 + (1.0 - abs(k - 0.4) * 2.0) * 60)
        else:
            k = (t - 0.65) / 0.35
            r = int(30 - k * 15)
            g = int(45 + k * 25)
            b = int(90 + k * 45)
        r = max(0, min(255, r))
        g = max(0, min(255, g))
        b = max(0, min(255, b))
        for x in range(w):
            px[x, y] = (r, g, b)

    draw = ImageDraw.Draw(img)

    # Stars in upper sky
    stars = [(14, 10), (35, 16), (62, 8), (94, 14), (112, 9), (24, 24), (80, 21), (105, 25)]
    for sx, sy in stars:
        draw.rectangle([sx, sy, sx + 1, sy + 1], fill=(255, 250, 220))

    # Bright sun on horizon
    draw.ellipse([48, 38, 80, 70], fill=(255, 215, 75), outline=(255, 245, 160))

    # Dark city skyline silhouette
    buildings = [
        (0, 56, 16, 72),
        (16, 46, 30, 72),
        (30, 52, 44, 72),
        (44, 60, 56, 72),
        (72, 58, 84, 72),
        (84, 42, 98, 72),
        (98, 50, 114, 72),
        (114, 55, 128, 72),
    ]
    for x0, y0, x1, y1 in buildings:
        draw.rectangle([x0, y0, x1, y1], fill=(16, 18, 32))
        # Lit windows
        for wy in range(y0 + 3, y1 - 3, 5):
            for wx in range(x0 + 3, x1 - 3, 4):
                if (wx + wy) % 3 != 0:
                    draw.rectangle([wx, wy, wx + 1, wy + 2], fill=(255, 220, 110))

    # Water reflection bars at bottom
    draw.line([(0, 72), (w, 72)], fill=(255, 180, 90), width=1)
    for ry in range(75, h, 4):
        span = max(4, 20 - (ry - 75))
        draw.line([(64 - span, ry), (64 + span, ry)], fill=(255, 210, 110), width=1)

    img.save(out_path, format="PNG")


def main() -> None:
    out_dir = Path(__file__).resolve().parent.parent / "web" / "static" / "samples"
    out_dir.mkdir(parents=True, exist_ok=True)
    make_satellite_grid(out_dir / "satellite_grid.png")
    make_jiit_crest_demo(out_dir / "jiit_crest_demo.png")
    make_landscape_demo(out_dir / "landscape_demo.png")
    for p in sorted(out_dir.glob("*.png")):
        with Image.open(p) as im:
            print(f"Generated {p.name}: {im.size[0]}x{im.size[1]} {im.mode} ({p.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
