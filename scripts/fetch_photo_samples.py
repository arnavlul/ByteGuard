"""Download and prepare offline sample photographs for ByteGuard."""

from __future__ import annotations

import io
import urllib.request
from pathlib import Path
from PIL import Image


PHOTO_SOURCES = {
    "taj_mahal.png": (
        "https://thumb.wikimedia.org/wikipedia/commons/thumb/7/74/"
        "Taj_Mahal%2C_Agra%2C_India_edit2.jpg/500px-Taj_Mahal%2C_Agra%2C_India_edit2.jpg"
    ),
    "nature_moraine_lake.png": (
        "https://thumb.wikimedia.org/wikipedia/commons/thumb/a/ab/"
        "Moraine_Lake_sunrise.jpg/500px-Moraine_Lake_sunrise.jpg"
    ),
    "city_architecture.png": (
        "https://thumb.wikimedia.org/wikipedia/commons/thumb/1/15/"
        "Sunset_View_From_Linton_Park_Nyc_%28112088763%29.jpeg/"
        "500px-Sunset_View_From_Linton_Park_Nyc_%28112088763%29.jpeg"
    ),
}


def fetch_and_resize(max_width: int = 256) -> None:
    out_dir = Path(__file__).resolve().parent.parent / "web" / "static" / "samples"
    out_dir.mkdir(parents=True, exist_ok=True)

    for filename, url in PHOTO_SOURCES.items():
        req = urllib.request.Request(
            url,
            headers={"User-Agent": "ByteGuardDemo/1.0 (https://localhost; educational)"},
        )
        with urllib.request.urlopen(req, timeout=20) as resp:
            raw = resp.read()

        img = Image.open(io.BytesIO(raw)).convert("RGB")
        if img.width > max_width:
            new_h = max(1, round(img.height * (max_width / float(img.width))))
            img = img.resize((max_width, new_h), Image.Resampling.LANCZOS)

        out_path = out_dir / filename
        img.save(out_path, format="PNG")
        print(f"Saved {filename}: {img.width}x{img.height} {img.mode} ({out_path.stat().st_size} bytes)")


if __name__ == "__main__":
    fetch_and_resize(256)
