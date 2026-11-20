"""End-to-end API, offline-bundle, and TC-12 bit-by-bit accuracy tests for ByteGuard."""

from __future__ import annotations

import base64
import io
import re
import sys
from pathlib import Path

from fastapi.testclient import TestClient
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from web.app import app  # noqa: E402


client = TestClient(app)


def decode_data_url_bytes(data_url: str) -> bytes:
    header, b64 = data_url.split(",", 1)
    assert "image/png" in header
    img = Image.open(io.BytesIO(base64.b64decode(b64))).convert("RGB")
    return img.tobytes()


def bit_accuracy_pct(raw_a: bytes, raw_b: bytes) -> float:
    assert len(raw_a) == len(raw_b)
    total_bits = len(raw_a) * 8
    same_bits = sum(8 - (ba ^ bb).bit_count() for ba, bb in zip(raw_a, raw_b))
    return (100.0 * same_bits) / total_bits


def test_offline_assets_and_homepage() -> None:
    index_html = (REPO_ROOT / "web" / "static" / "index.html").read_text(encoding="utf-8")
    assert not re.search(r"https?://", index_html, re.IGNORECASE), "index.html must not contain external URLs"

    for route in (
        "/",
        "/api/samples",
        "/static/styles.css",
        "/static/app.js",
        "/static/vendor/cytoscape.min.js",
        "/static/vendor/chart.umd.min.js",
    ):
        resp = client.get(route)
        assert resp.status_code == 200, f"GET {route} failed: {resp.status_code}"

    samples = client.get("/api/samples").json()["samples"]
    assert len(samples) >= 6
    names = [s["name"] for s in samples]
    assert names[0] == "taj_mahal.png"
    for required in ("taj_mahal.png", "nature_moraine_lake.png", "city_architecture.png"):
        assert required in names
        assert client.get(f"/static/samples/{required}").status_code == 200


def test_tc12_bitflip_end_to_end_exact_bit_match() -> None:
    p_resp = client.post(
        "/protect",
        data={
            "sample_name": "satellite_grid.png",
            "block_size": "576",
            "protection_checks": "288",
            "seed": "42",
        },
    )
    assert p_resp.status_code == 200
    orig_bytes = decode_data_url_bytes(p_resp.json()["original_png"])

    s_resp = client.post(
        "/scramble",
        json={"mode": "bitflip", "level": 0.010, "seed": 42, "strokes": []},
    )
    assert s_resp.status_code == 200
    s_data = s_resp.json()
    damaged_bytes = decode_data_url_bytes(s_data["protected_damaged_png"])
    unprot_bytes = decode_data_url_bytes(s_data["unprotected_damaged_png"])

    assert abs(bit_accuracy_pct(orig_bytes, damaged_bytes) - s_data["metrics"]["protected_correct_pct"]) < 1e-3
    assert abs(bit_accuracy_pct(orig_bytes, unprot_bytes) - s_data["metrics"]["unprotected_correct_pct"]) < 1e-3

    r_resp = client.post("/repair", json={"step_only": False, "max_rounds": 25})
    assert r_resp.status_code == 200
    snaps = r_resp.json()["snapshots"]
    assert len(snaps) >= 2
    for snap in snaps:
        rep_bytes = decode_data_url_bytes(snap["repaired_png"])
        expected_pct = bit_accuracy_pct(orig_bytes, rep_bytes)
        assert abs(expected_pct - snap["metrics"]["protected_correct_pct"]) < 1e-3
    assert snaps[-1]["metrics"]["protected_correct_pct"] == 100.0

    # High bitflip noise (5.0%) triggers warning and leaves partial repair below 100%
    s_hi = client.post(
        "/scramble",
        json={"mode": "bitflip", "level": 0.05, "seed": 42, "strokes": []},
    )
    assert s_hi.status_code == 200
    assert s_hi.json()["recommendation"]["status"] == "warning"
    r_hi = client.post("/repair", json={"step_only": False, "max_rounds": 25}).json()
    assert r_hi["snapshots"][-1]["metrics"]["protected_correct_pct"] < 100.0


def test_lostchunk_and_stopping_sets() -> None:
    client.post(
        "/protect",
        data={
            "sample_name": "jiit_crest_demo.png",
            "block_size": "576",
            "protection_checks": "288",
            "seed": "42",
        },
    )

    # Low/moderate lostchunk -> 100% recovery
    s_low = client.post("/scramble", json={"mode": "lostchunk", "level": 0.20, "seed": 42}).json()
    assert s_low["metrics"]["protected_correct_pct"] < 100.0
    r_low = client.post("/repair", json={"step_only": False}).json()
    assert r_low["snapshots"][-1]["metrics"]["protected_correct_pct"] == 100.0

    # High lostchunk (55%) -> partial recovery + stopping sets
    s_high = client.post("/scramble", json={"mode": "lostchunk", "level": 0.55, "seed": 42}).json()
    r_high = client.post("/repair", json={"step_only": False}).json()
    last = r_high["snapshots"][-1]["metrics"]
    assert s_high["metrics"]["protected_correct_pct"] < last["protected_correct_pct"] < 100.0
    assert last["stopping_set_bits_total"] > 0

    for view, color_mode in (("focus", "status"), ("focus", "welsh_powell"), ("full", "status")):
        g_resp = client.get(f"/graph?round=-1&view={view}&color_mode={color_mode}")
        assert g_resp.status_code == 200
        g_data = g_resp.json()
        assert len(g_data["elements"]["nodes"]) > 0
        assert len(g_data["elements"]["edges"]) > 0


def test_brush_scratch_and_step_repair() -> None:
    p_resp = client.post(
        "/protect",
        data={
            "sample_name": "landscape_demo.png",
            "block_size": "576",
            "protection_checks": "288",
            "seed": "42",
        },
    )
    assert p_resp.status_code == 200

    strokes = [{"x": 40.0, "y": 48.0, "radius": 6.0}, {"x": 48.0, "y": 48.0, "radius": 6.0}]
    s_resp = client.post(
        "/scramble",
        json={"mode": "brush", "level": 0.0, "seed": 42, "strokes": strokes},
    )
    assert s_resp.status_code == 200
    assert s_resp.json()["metrics"]["protected_correct_pct"] < 100.0

    # Step-by-step repair
    last_pct = 0.0
    for _ in range(15):
        step_resp = client.post("/repair", json={"step_only": True}).json()
        last_pct = step_resp["snapshots"][0]["metrics"]["protected_correct_pct"]
        if step_resp["is_finished"]:
            break
    assert last_pct == 100.0


def test_hamming_sandbox_tc06_and_tc07() -> None:
    # TC-06: {v1, v3, v5} lost -> recovered in 3 steps
    r6 = client.post(
        "/api/hamming/run",
        json={"data_bits": [1, 0, 0, 0], "lost_vars": [0, 2, 4], "flipped_vars": []},
    )
    assert r6.status_code == 200
    d6 = r6.json()
    assert d6["girth"] == 4
    assert d6["connected"] is True
    assert d6["fully_recovered"] is True
    assert len(d6["steps"]) == 4  # step 0 + 3 peeling waves

    # TC-07: {v1, v2, v3} lost -> stops immediately on stopping set [0, 1, 2]
    r7 = client.post(
        "/api/hamming/run",
        json={"data_bits": [1, 0, 0, 0], "lost_vars": [0, 1, 2], "flipped_vars": []},
    )
    assert r7.status_code == 200
    d7 = r7.json()
    assert d7["fully_recovered"] is False
    assert len(d7["steps"]) == 1
    assert d7["steps"][0]["stopping_set"] == [0, 1, 2]


def test_photo_samples_and_file_upload() -> None:
    # 1. Real downloaded photo (taj_mahal.png) protect -> scramble -> repair to 100%
    p_taj = client.post(
        "/protect",
        data={
            "sample_name": "taj_mahal.png",
            "block_size": "576",
            "protection_checks": "288",
            "seed": "42",
        },
    )
    assert p_taj.status_code == 200
    taj_data = p_taj.json()
    assert taj_data["was_resized"] is False
    assert taj_data["width"] == 256
    assert client.post("/scramble", json={"mode": "lostchunk", "level": 0.20, "seed": 42}).status_code == 200
    r_taj = client.post("/repair", json={"step_only": False}).json()
    assert r_taj["snapshots"][-1]["metrics"]["protected_correct_pct"] == 100.0

    # 2. Small custom file upload preserves exact dimensions
    small_buf = io.BytesIO()
    Image.new("RGB", (35, 29), (20, 120, 210)).save(small_buf, format="PNG")
    small_buf.seek(0)
    up_small = client.post(
        "/protect",
        data={"block_size": "576", "protection_checks": "288", "seed": "42", "max_dimension": "320"},
        files={"image_file": ("tiny_upload.png", small_buf.getvalue(), "image/png")},
    )
    assert up_small.status_code == 200
    u1 = up_small.json()
    assert u1["width"] == 35 and u1["height"] == 29
    assert u1["was_resized"] is False

    # 3. Large multi-megapixel-like upload (800x600) auto-fits proportionally to 320x240
    big_buf = io.BytesIO()
    Image.new("RGB", (800, 600), (190, 90, 40)).save(big_buf, format="PNG")
    big_buf.seek(0)
    up_big = client.post(
        "/protect",
        data={"block_size": "576", "protection_checks": "288", "seed": "42", "max_dimension": "320"},
        files={"image_file": ("big_photo.png", big_buf.getvalue(), "image/png")},
    )
    assert up_big.status_code == 200
    u2 = up_big.json()
    assert u2["source_width"] == 800 and u2["source_height"] == 600
    assert u2["width"] == 320 and u2["height"] == 240
    assert u2["was_resized"] is True
