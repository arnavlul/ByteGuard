# ByteGuard

Visual error correction of images using **LDPC codes on bipartite Tanner graphs**, with a standalone C++17 graph/LDPC engine and an offline FastAPI + Cytoscape.js / Chart.js web visualizer.

---

## Architecture

ByteGuard cleanly separates the pure C++17 graph/LDPC algorithms from the optional browser demo:

```text
ByteGuard/
├── cpp/                    # Pure C++17 graph & LDPC engine (zero Python/web dependencies)
│   ├── graph.cpp           # TannerGraph, GraphAnalyzer (girth, stopping sets, Welsh-Powell), PEGBuilder
│   ├── ldpc.cpp            # GF(2) Encoder (Kahn topological sort), Corruptors, Peeling & BitFlip decoders
│   └── session.cpp         # Protect -> Scramble -> Repair image session & telemetry
├── web/                    # Optional Python web interface
│   ├── bindings.cpp        # pybind11 wrapper exposing cpp/session.cpp to Python
│   ├── app.py              # Offline FastAPI service (/protect, /scramble, /repair, /graph, /api/hamming/run)
│   └── static/             # HTML/CSS/JS + bundled offline Cytoscape.js, Chart.js, and sample PNGs
├── build.py                # Zero-CMake compiler wrapper for web/bindings.cpp
├── pyproject.toml
└── LICENSE                 # GNU Lesser General Public License v2.1 (LGPL-2.1)
```

## Algorithms Implemented (C++17)

- **Bipartite Tanner Graph (`TannerGraph`)**: Sparse variable/check adjacency lists and parity-check incidence matrix $H$, including `Hamming(7,4)`.
- **Breadth-First Search (`PEGBuilder`, `GraphAnalyzer`)**: Progressive Edge Growth (PEG) construction maximizing local girth ($\ge 6$), shortest-cycle girth calculation, and full-graph connectivity check.
- **Kahn's Topological Sort (`Encoder`, `PeelingDecoder`)**: Systematic GF(2) upper-triangular reduction with parity DAG topological ordering, plus queue-based degree-1 erasure peeling and stopping-set detection.
- **Welsh–Powell Vertex Coloring (`GraphAnalyzer`, `BitFlipDecoder`)**: Conflict-free check color scheduling for Gallager hard-decision bit-flipping recovery.

---

## Usage

### 1. Set Up the Python Environment

```bash
uv venv .venv --python 3.13
uv pip install --python .venv/Scripts/python.exe pybind11 fastapi "uvicorn[standard]" pillow python-multipart
```

*(On Linux/macOS replace `.venv/Scripts/python.exe` with `.venv/bin/python`, or use `pip install -e .`.)*

### 2. Compile the Python Extension

```bash
# Windows (auto-detects MSVC Build Tools via vswhere)
.venv/Scripts/python.exe build.py

# Linux / macOS (g++ or c++)
.venv/bin/python build.py
```

### 3. Launch the Offline Web Studio

```bash
.venv/Scripts/uvicorn.exe web.app:app --host 127.0.0.1 --port 8000
```

Open **http://127.0.0.1:8000** in your browser:
1. Opens immediately with pre-loaded sample images protected by a `(576, 288)` PEG Tanner graph, or load any image from disk / drag-and-drop.
2. Select **Lost Chunks**, **Bit Flips**, or **Scratch Brush** (paint directly on the original image) and click **Scramble**.
3. Run **Repair (Animate)** or **Step +1** to watch round-by-round reconstruction, accuracy comparison vs. unprotected and 3× redundant copies, and the live Cytoscape.js Tanner graph explorer.
4. Use the **Hamming(7,4) Sandbox** tab to test custom erasures, bit flips, and stopping sets interactively.

---

## License

Licensed under the [GNU Lesser General Public License v2.1 (LGPL-2.1)](LICENSE).
