"""ByteGuard FastAPI service layer (thin wrapper over the C++17 pybind11 engine)."""

from __future__ import annotations

import base64
import io
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from fastapi import FastAPI, File, Form, HTTPException, UploadFile
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from PIL import Image as PILImage
from PIL import ImageOps
from pydantic import BaseModel, Field

WEB_DIR = Path(__file__).resolve().parent
STATIC_DIR = WEB_DIR / "static"
SAMPLES_DIR = STATIC_DIR / "samples"

if str(WEB_DIR) not in sys.path:
    sys.path.insert(0, str(WEB_DIR))

import byteguard_cpp as bg  # noqa: E402


app = FastAPI(title="ByteGuard", version="1.0.0")
app.mount("/static", StaticFiles(directory=str(STATIC_DIR)), name="static")

# Cached PEG/Hamming graphs keyed by (n, m, seed) and current active Session.
GRAPH_CACHE: Dict[Tuple[int, int, int], bg.TannerGraph] = {}
STATE: Dict[str, Any] = {
    "session": None,
    "sample_name": "taj_mahal.png",
    "original_png": "",
    "block_size": 576,
    "protection_checks": 288,
    "seed": 42,
    "initial_damaged_vars": [],
}


class BrushStrokeIn(BaseModel):
    x: float
    y: float
    radius: float = 8.0


class ScrambleRequest(BaseModel):
    mode: str = "lostchunk"
    level: float = Field(default=0.20, ge=0.0, le=1.0)
    seed: int = 42
    strokes: List[BrushStrokeIn] = Field(default_factory=list)


class RepairRequest(BaseModel):
    step_only: bool = False
    max_rounds: int = Field(default=25, ge=1, le=100)


class HammingRequest(BaseModel):
    data_bits: List[int] = Field(default_factory=lambda: [1, 0, 0, 0])
    lost_vars: List[int] = Field(default_factory=lambda: [0, 2, 4])
    flipped_vars: List[int] = Field(default_factory=list)


def get_or_build_graph(n: int, m: int, seed: int) -> bg.TannerGraph:
    if n == 7 and m == 3:
        return bg.TannerGraph.hamming74()
    key = (n, m, seed)
    if key not in GRAPH_CACHE:
        GRAPH_CACHE[key] = bg.PEGBuilder(seed).build(n, m)
    return GRAPH_CACHE[key]


def pil_to_cpp_image(pil_img: PILImage.Image) -> bg.Image:
    rgb = pil_img.convert("RGB")
    w, h = rgb.size
    return bg.Image(w, h, 3, rgb.tobytes())


def cpp_image_to_png_b64(cpp_img: bg.Image) -> str:
    pil_img = PILImage.frombytes(
        "RGB", (cpp_img.width, cpp_img.height), cpp_img.pixels_bytes()
    )
    buf = io.BytesIO()
    pil_img.save(buf, format="PNG")
    encoded = base64.b64encode(buf.getvalue()).decode("ascii")
    return f"data:image/png;base64,{encoded}"


def metrics_to_dict(m: bg.Metrics) -> Dict[str, Any]:
    return {
        "protected_correct_pct": round(m.protected_correct_pct, 4),
        "unprotected_correct_pct": round(m.unprotected_correct_pct, 4),
        "damaged_protected_pct": round(m.damaged_protected_pct, 4),
        "triple_copy_correct_pct": round(m.triple_copy_correct_pct, 4),
        "code_rate": round(m.code_rate, 4),
        "ldpc_overhead_ratio": round(m.ldpc_overhead_ratio, 4),
        "triple_copy_overhead_ratio": round(m.triple_copy_overhead_ratio, 2),
        "original_bytes": m.original_bytes,
        "protected_bytes": m.protected_bytes,
        "triple_copy_bytes": m.triple_copy_bytes,
        "total_data_bits": m.total_data_bits,
        "correct_data_bits": m.correct_data_bits,
        "remaining_unknown_bits": m.remaining_unknown_bits,
        "failed_checks": m.failed_checks,
        "total_checks": m.total_checks,
        "total_blocks": m.total_blocks,
        "fully_repaired_blocks": m.fully_repaired_blocks,
        "stopping_set_bits_total": m.stopping_set_bits_total,
        "round": m.round,
    }


def block_snapshot_summary(blk: bg.BlockGraphSnapshot) -> Dict[str, Any]:
    missing_cnt = sum(1 for b in blk.bits if b == bg.BIT_UNKNOWN)
    wrong_cnt = sum(
        1
        for cur, orig in zip(blk.bits, blk.original_bits)
        if cur != bg.BIT_UNKNOWN and cur != orig
    )
    failed_cnt = sum(1 for f in blk.check_failed if f)
    return {
        "block_index": blk.block_index,
        "round": blk.round,
        "missing_bits": missing_cnt,
        "wrong_bits": wrong_cnt,
        "failed_checks": failed_cnt,
        "stopping_set": list(blk.stopping_set),
        "newly_fixed_vars": list(blk.newly_fixed_vars),
    }


def build_recommendation(mode: str, level: float, n: int, m: int) -> Dict[str, Any]:
    parity_ratio = float(m) / float(max(1, n))
    norm = mode.lower()
    if norm in ("bitflip", "bit_flip", "flip"):
        safe_limit = 0.015 * (parity_ratio / 0.5)
        if level > safe_limit:
            return {
                "status": "warning",
                "message": (
                    f"High bit-flip noise ({level * 100:.1f}%) for m/n={parity_ratio:.2f}. "
                    "Raising Protection Level (m) or lowering damage improves full recovery."
                ),
            }
    elif level > parity_ratio * 0.78:
        return {
            "status": "warning",
            "message": (
                f"Erasure level ({level * 100:.0f}%) nears code limit (m/n={parity_ratio:.2f}); "
                "stopping sets may limit full repair. Try raising Protection Level (m)."
            ),
        }
    return {
        "status": "ok",
        "message": f"Protection m/n={parity_ratio:.2f} is well-suited for {level * 100:.1f}% {mode} damage.",
    }


def require_session() -> bg.Session:
    sess = STATE.get("session")
    if sess is None:
        raise HTTPException(status_code=400, detail="No active session. Call POST /protect first.")
    return sess


@app.get("/")
def index() -> FileResponse:
    return FileResponse(str(STATIC_DIR / "index.html"))


@app.get("/api/samples")
def list_samples() -> Dict[str, Any]:
    ordered = [
        ("taj_mahal.png", "Taj Mahal"),
        ("nature_moraine_lake.png", "Nature (Moraine Lake)"),
        ("city_architecture.png", "City Skyline"),
        ("satellite_grid.png", "Satellite Grid"),
        ("jiit_crest_demo.png", "Crest Emblem"),
        ("landscape_demo.png", "Sunset Pixel Art"),
    ]
    samples = []
    seen = set()
    if SAMPLES_DIR.exists():
        for name, label in ordered:
            p = SAMPLES_DIR / name
            if p.exists():
                seen.add(name)
                samples.append(
                    {
                        "name": name,
                        "label": label,
                        "url": f"/static/samples/{name}",
                    }
                )
        for p in sorted(SAMPLES_DIR.glob("*.png")):
            if p.name not in seen:
                samples.append(
                    {
                        "name": p.name,
                        "label": p.stem.replace("_", " ").title(),
                        "url": f"/static/samples/{p.name}",
                    }
                )
    return {"samples": samples}


@app.post("/protect")
async def protect_image(
    image_file: Optional[UploadFile] = File(default=None),
    sample_name: str = Form(default="taj_mahal.png"),
    block_size: int = Form(default=576),
    protection_checks: int = Form(default=288),
    seed: int = Form(default=42),
    max_dimension: int = Form(default=320),
) -> Dict[str, Any]:
    if block_size != 7 and (block_size < 16 or block_size > 2048):
        raise HTTPException(status_code=422, detail="block_size must be 7 or between 16 and 2048")
    if protection_checks < 1 or protection_checks >= block_size:
        raise HTTPException(
            status_code=422, detail="protection_checks (m) must satisfy 1 <= m < block_size"
        )

    is_upload = image_file is not None
    if is_upload:
        raw = await image_file.read()
        try:
            pil_img = ImageOps.exif_transpose(PILImage.open(io.BytesIO(raw)))
        except Exception as exc:
            raise HTTPException(status_code=400, detail=f"Invalid image file: {exc}") from exc
        chosen_name = image_file.filename or "uploaded_image.png"
    else:
        sample_path = SAMPLES_DIR / Path(sample_name).name
        if not sample_path.exists():
            raise HTTPException(status_code=404, detail=f"Sample not found: {sample_name}")
        pil_img = PILImage.open(sample_path)
        chosen_name = sample_path.name

    source_width, source_height = pil_img.size
    was_resized = False
    if is_upload and max_dimension > 0 and max(source_width, source_height) > max_dimension:
        scale = max_dimension / float(max(source_width, source_height))
        new_w = max(1, round(source_width * scale))
        new_h = max(1, round(source_height * scale))
        pil_img = pil_img.resize((new_w, new_h), PILImage.Resampling.LANCZOS)
        was_resized = True

    cpp_img = pil_to_cpp_image(pil_img)
    graph = get_or_build_graph(block_size, protection_checks, seed)
    session = bg.Session.with_graph(cpp_img, graph)
    session.protect()

    original_png = cpp_image_to_png_b64(session.original_image())
    STATE["session"] = session
    STATE["sample_name"] = chosen_name
    STATE["original_png"] = original_png
    STATE["block_size"] = block_size
    STATE["protection_checks"] = protection_checks
    STATE["seed"] = seed
    STATE["initial_damaged_vars"] = []

    return {
        "sample_name": chosen_name,
        "width": cpp_img.width,
        "height": cpp_img.height,
        "source_width": source_width,
        "source_height": source_height,
        "was_resized": was_resized,
        "original_png": original_png,
        "graph_summary": session.graph_summary(),
        "metrics": metrics_to_dict(session.metrics()),
        "recommendation": build_recommendation("lostchunk", 0.20, block_size, protection_checks),
    }


@app.post("/scramble")
def scramble_image(req: ScrambleRequest) -> Dict[str, Any]:
    session = require_session()
    cpp_strokes = [bg.BrushStroke(s.x, s.y, s.radius) for s in req.strokes]
    try:
        snap0 = session.scramble(req.mode, req.level, req.seed, cpp_strokes)
    except Exception as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc

    blk = snap0.inspected_block
    damaged_vars = [
        v
        for v, (cur, orig) in enumerate(zip(blk.bits, blk.original_bits))
        if cur != orig
    ]
    STATE["initial_damaged_vars"] = damaged_vars

    return {
        "round": snap0.round,
        "phase": snap0.phase,
        "unprotected_damaged_png": cpp_image_to_png_b64(session.unprotected_damaged_image()),
        "protected_damaged_png": cpp_image_to_png_b64(snap0.repaired_image),
        "metrics": metrics_to_dict(snap0.metrics),
        "recommendation": build_recommendation(
            req.mode, req.level, STATE["block_size"], STATE["protection_checks"]
        ),
        "inspected_block_summary": block_snapshot_summary(blk),
        "graph_summary": session.graph_summary(),
        "is_finished": session.is_repair_finished(),
    }


@app.post("/repair")
def repair_image(req: RepairRequest) -> Dict[str, Any]:
    session = require_session()
    if not session.history():
        raise HTTPException(status_code=400, detail="Call POST /scramble before /repair.")

    if req.step_only:
        snap = session.repair_step()
        return {
            "mode": "step",
            "is_finished": session.is_repair_finished(),
            "snapshots": [
                {
                    "round": snap.round,
                    "phase": snap.phase,
                    "repaired_png": cpp_image_to_png_b64(snap.repaired_image),
                    "metrics": metrics_to_dict(snap.metrics),
                    "inspected_block_summary": block_snapshot_summary(snap.inspected_block),
                }
            ],
        }

    history = session.repair_all(req.max_rounds)
    return {
        "mode": "all",
        "is_finished": session.is_repair_finished(),
        "snapshots": [
            {
                "round": s.round,
                "phase": s.phase,
                "repaired_png": cpp_image_to_png_b64(s.repaired_image),
                "metrics": metrics_to_dict(s.metrics),
                "inspected_block_summary": block_snapshot_summary(s.inspected_block),
            }
            for s in history
        ],
    }


def build_cytoscape_elements(
    graph: bg.TannerGraph,
    encoder: bg.Encoder,
    colors: List[int],
    blk: Optional[bg.BlockGraphSnapshot],
    initial_damaged_vars: List[int],
    view: str,
    color_mode: str,
) -> Dict[str, Any]:
    n = graph.num_vars()
    m = graph.num_checks()
    parity_set = set(encoder.parity_positions())

    if blk is not None:
        bits = list(blk.bits)
        orig_bits = list(blk.original_bits)
        chk_failed = list(blk.check_failed)
        stopping = set(blk.stopping_set)
        newly_fixed = set(blk.newly_fixed_vars)
    else:
        bits = [0] * n
        orig_bits = [0] * n
        chk_failed = [False] * m
        stopping = set()
        newly_fixed = set()

    if view == "full" or n <= 64:
        selected_vars = list(range(n))
        selected_chks = list(range(m))
    else:
        # Focus view: prioritize damaged, newly-fixed, stopping-set, and initially-damaged vars.
        priority: List[int] = []
        seen = set()
        for candidate_list in (
            list(newly_fixed),
            list(stopping),
            [v for v in range(n) if bits[v] != orig_bits[v]],
            initial_damaged_vars,
        ):
            for v in candidate_list:
                if v not in seen:
                    seen.add(v)
                    priority.append(v)
                if len(priority) >= 40:
                    break
            if len(priority) >= 40:
                break

        # Ensure a readable minimum subgraph even when 0 damage was applied.
        v_cursor = 0
        while len(priority) < min(24, n) and v_cursor < n:
            if v_cursor not in seen:
                seen.add(v_cursor)
                priority.append(v_cursor)
            v_cursor += 1

        selected_vars = sorted(priority[:40])
        chk_set = set()
        for v in selected_vars:
            for c in graph.var_neighbors(v):
                chk_set.add(c)
        selected_chks = sorted(chk_set)[:48]

    chk_allowed = set(selected_chks)
    wp_palette = [
        "#38bdf8",
        "#a855f7",
        "#f59e0b",
        "#10b981",
        "#ec4899",
        "#6366f1",
        "#14b8a6",
        "#f97316",
        "#eab308",
        "#84cc16",
    ]

    nodes = []
    edges = []

    var_spacing = max(36.0, 920.0 / max(1, len(selected_vars)))
    chk_spacing = max(36.0, 920.0 / max(1, len(selected_chks)))
    var_offset = -0.5 * (len(selected_vars) - 1) * var_spacing
    chk_offset = -0.5 * (len(selected_chks) - 1) * chk_spacing

    for idx, c in enumerate(selected_chks):
        failed = chk_failed[c] if c < len(chk_failed) else False
        wp_col = colors[c] if c < len(colors) else 0
        wp_hex = wp_palette[wp_col % len(wp_palette)]
        status = "failed" if failed else "satisfied"
        nodes.append(
            {
                "data": {
                    "id": f"c{c}",
                    "label": f"c{c + 1}",
                    "kind": "check",
                    "status": status,
                    "wp_color": wp_col,
                    "wp_hex": wp_hex,
                    "color_mode": color_mode,
                    "degree": graph.check_degree(c),
                },
                "position": {"x": chk_offset + idx * chk_spacing, "y": -115.0},
            }
        )

    for idx, v in enumerate(selected_vars):
        val = bits[v]
        orig = orig_bits[v]
        if v in stopping:
            status = "stopping"
        elif val == bg.BIT_UNKNOWN:
            status = "missing"
        elif val != orig:
            status = "wrong"
        elif v in newly_fixed:
            status = "fixed"
        else:
            status = "ok"

        bit_str = "?" if val == bg.BIT_UNKNOWN else str(val)
        nodes.append(
            {
                "data": {
                    "id": f"v{v}",
                    "label": f"v{v + 1}:{bit_str}",
                    "kind": "var",
                    "role": "parity" if v in parity_set else "data",
                    "status": status,
                    "bit_value": bit_str,
                    "degree": graph.degree(v),
                },
                "position": {"x": var_offset + idx * var_spacing, "y": 115.0},
            }
        )

        for c in graph.var_neighbors(v):
            if c in chk_allowed:
                edge_active = chk_failed[c] if c < len(chk_failed) else False
                edges.append(
                    {
                        "data": {
                            "id": f"e_{c}_{v}",
                            "source": f"c{c}",
                            "target": f"v{v}",
                            "failed": edge_active,
                        }
                    }
                )

    return {
        "nodes": nodes,
        "edges": edges,
        "shown_vars": len(selected_vars),
        "shown_checks": len(selected_chks),
    }


@app.get("/graph")
def get_graph(
    round: int = -1,
    view: str = "focus",
    color_mode: str = "status",
) -> Dict[str, Any]:
    session = require_session()
    hist = session.history()
    snap_blk: Optional[bg.BlockGraphSnapshot] = None
    phase = "protected"
    actual_round = 0

    if hist:
        idx = (len(hist) - 1) if round < 0 else max(0, min(round, len(hist) - 1))
        snap = hist[idx]
        snap_blk = snap.inspected_block
        phase = snap.phase
        actual_round = snap.round

    elements = build_cytoscape_elements(
        graph=session.graph(),
        encoder=session.encoder(),
        colors=session.check_colors(),
        blk=snap_blk,
        initial_damaged_vars=STATE.get("initial_damaged_vars", []),
        view=view,
        color_mode=color_mode,
    )

    return {
        "round": actual_round,
        "phase": phase,
        "view": view,
        "color_mode": color_mode,
        "graph_summary": session.graph_summary(),
        "inspected_block_summary": block_snapshot_summary(snap_blk) if snap_blk else None,
        "elements": elements,
    }


@app.post("/api/hamming/run")
def run_hamming_sandbox(req: HammingRequest) -> Dict[str, Any]:
    if len(req.data_bits) != 4 or any(b not in (0, 1) for b in req.data_bits):
        raise HTTPException(status_code=422, detail="data_bits must be 4 binary values (0 or 1)")

    g = bg.TannerGraph.hamming74()
    enc = bg.Encoder(g)
    analyzer = bg.GraphAnalyzer(g)
    colors = analyzer.check_colors()

    orig = list(enc.encode(req.data_bits))
    rx = list(orig)
    for v in req.flipped_vars:
        if 0 <= v < 7:
            rx[v] ^= 1
    for v in req.lost_vars:
        if 0 <= v < 7:
            rx[v] = bg.BIT_UNKNOWN

    steps: List[Dict[str, Any]] = []

    def make_step_record(step_num: int, phase_name: str, cur_bits: List[int], fixed: List[int]) -> Dict[str, Any]:
        lost = [i for i, b in enumerate(cur_bits) if b == bg.BIT_UNKNOWN]
        stopping = analyzer.stopping_set(lost) if lost else []
        failed_set = set(g.failed_checks(cur_bits))
        chk_failed = [c in failed_set for c in range(3)]
        blk_snap = _HammingPseudoSnap(cur_bits, orig, chk_failed, stopping, fixed)
        elements = build_cytoscape_elements(
            graph=g,
            encoder=enc,
            colors=colors,
            blk=blk_snap,  # type: ignore[arg-type]
            initial_damaged_vars=[],
            view="full",
            color_mode="status",
        )
        return {
            "step": step_num,
            "phase": phase_name,
            "bits": list(cur_bits),
            "original_bits": orig,
            "fixed_vars": list(fixed),
            "stopping_set": list(stopping),
            "failed_checks": [c for c in range(3) if chk_failed[c]],
            "elements": elements,
        }

    steps.append(make_step_record(0, "damaged", rx, []))

    peeler = bg.PeelingDecoder(g, 20)
    step_idx = 0
    while True:
        changed, rx = peeler.step(rx)
        if not changed:
            break
        step_idx += 1
        steps.append(make_step_record(step_idx, "peeling", rx, list(peeler.last_fixed_vars())))

    if bg.BIT_UNKNOWN not in rx and not g.is_valid_codeword(rx):
        flipper = bg.BitFlipDecoder(g, 15)
        while True:
            changed, rx = flipper.step(rx)
            if not changed:
                break
            step_idx += 1
            steps.append(make_step_record(step_idx, "bitflip", rx, list(flipper.last_fixed_vars())))

    return {
        "girth": analyzer.girth(),
        "connected": analyzer.is_connected(),
        "edges": g.num_edges(),
        "original_codeword": orig,
        "final_bits": rx,
        "fully_recovered": rx == orig,
        "steps": steps,
    }


class _HammingPseudoSnap:
    def __init__(
        self,
        bits: List[int],
        original_bits: List[int],
        check_failed: List[bool],
        stopping_set: List[int],
        newly_fixed_vars: List[int],
    ) -> None:
        self.block_index = 0
        self.round = 0
        self.bits = bits
        self.original_bits = original_bits
        self.check_failed = check_failed
        self.stopping_set = stopping_set
        self.newly_fixed_vars = newly_fixed_vars
