/* ByteGuard Browser Studio (100% offline using local Cytoscape.js & Chart.js) */

const state = {
  samples: [],
  selectedSample: "taj_mahal.png",
  uploadedFile: null,
  imageWidth: 256,
  imageHeight: 168,
  originalDataUrl: "",
  damageMode: "lostchunk",
  strokes: [],
  isDrawing: false,
  brushRadius: 7,
  graphView: "focus",
  graphColorMode: "status",
  snapshots: [],
  currentRoundIdx: 0,
  playTimer: null,
  chart: null,
  cy: null,
  lastBuiltConfig: null,
  hamming: {
    dataBits: [1, 0, 0, 0],
    varStates: ["lost", "ok", "lost", "ok", "lost", "ok", "ok"], // TC-06 default
    steps: [],
    stepIdx: 0,
  },
};

function $(id) {
  return document.getElementById(id);
}

function setButtonState(btnId, { busy = false, stateClass = "", text = null } = {}) {
  const btn = $(btnId);
  if (!btn) return;
  btn.classList.remove("is-busy", "is-done", "is-dirty", "is-partial");
  if (busy) btn.classList.add("is-busy");
  if (stateClass) btn.classList.add(stateClass);
  const spinner = btn.querySelector(".btn-spinner");
  if (spinner) spinner.classList.toggle("hidden", !busy);
  const textEl = btn.querySelector(".btn-text");
  if (textEl && text !== null) {
    textEl.textContent = text;
  } else if (!textEl && text !== null) {
    btn.textContent = text;
  }
  btn.disabled = busy;
}

function setStatusLine(lineId, text, tone = "") {
  const el = $(lineId);
  if (!el) return;
  el.textContent = text;
  el.classList.remove("status-ok", "status-pending", "status-warn");
  if (tone) el.classList.add(tone);
}

function currentCodeConfig() {
  return {
    n: parseInt($("blockSizeSelect").value, 10),
    m: getSelectedChecks(),
    seed: parseInt($("seedInput").value || "42", 10),
  };
}

function markProtectDirtyIfChanged() {
  if (!state.lastBuiltConfig) return;
  const cur = currentCodeConfig();
  const prev = state.lastBuiltConfig;
  const changed = cur.n !== prev.n || cur.m !== prev.m || cur.seed !== prev.seed;
  if (changed) {
    setButtonState("btnProtect", {
      busy: false,
      stateClass: "is-dirty",
      text: "Rebuild Graph & Protect (Changes Pending)",
    });
    setStatusLine(
      "protectStatusLine",
      "Settings changed — click Protect to rebuild Tanner graph.",
      "status-pending"
    );
  } else {
    setButtonState("btnProtect", {
      busy: false,
      stateClass: "is-done",
      text: "✓ Graph Built & Protected",
    });
    setStatusLine("protectStatusLine", prev.summaryText, "status-ok");
  }
}

function resetScrambleButton() {
  setButtonState("btnScramble", {
    busy: false,
    stateClass: "",
    text: "Scramble (Inject Damage)",
  });
  setStatusLine("scrambleStatusLine", "Ready to inject damage.", "");
}

function resetRepairButtons(enabled) {
  setButtonState("btnRepairAll", {
    busy: false,
    stateClass: "",
    text: "Repair (Animate)",
  });
  setButtonState("btnRepairStep", {
    busy: false,
    stateClass: "",
    text: "Step +1",
  });
  $("btnRepairAll").disabled = !enabled;
  $("btnRepairStep").disabled = !enabled;
}

function getSelectedChecks() {
  const n = parseInt($("blockSizeSelect").value, 10);
  if (n === 7) return 3;
  const ratio = parseFloat($("protectionSlider").value);
  return Math.max(1, Math.min(n - 1, Math.round(n * ratio)));
}

function updateProtectionLabel() {
  const n = parseInt($("blockSizeSelect").value, 10);
  const slider = $("protectionSlider");
  if (n === 7) {
    slider.disabled = true;
    $("protectionLabel").textContent = "0.43 (m = 3, Hamming)";
  } else {
    slider.disabled = false;
    const ratio = parseFloat(slider.value);
    const m = getSelectedChecks();
    $("protectionLabel").textContent = `${ratio.toFixed(2)} (m = ${m})`;
  }
}

function updateDamageLabel() {
  const val = parseFloat($("damageSlider").value);
  $("damageLabel").textContent = `${val.toFixed(1)}%`;
}

function updateLiveRecommendation() {
  const n = parseInt($("blockSizeSelect").value, 10);
  const m = getSelectedChecks();
  const ratio = m / Math.max(1, n);
  const level = state.damageMode === "brush"
    ? 0.1
    : parseFloat($("damageSlider").value) / 100.0;

  if (state.damageMode === "bitflip") {
    const safeLimit = 0.015 * (ratio / 0.5);
    if (level > safeLimit) {
      setRecommendation({
        status: "warning",
        message:
          `Bit-flip noise (${(level * 100).toFixed(1)}%) exceeds hard-decision limit ` +
          `(~${(safeLimit * 100).toFixed(1)}% at m/n=${ratio.toFixed(2)}; Shannon limit at R=1/2 is 11%). ` +
          `Raise Protection Level (m/n) or keep Bit Flips around 0.5%–2.5% for full recovery.`,
      });
    } else {
      setRecommendation({
        status: "ok",
        message: `Protection m/n=${ratio.toFixed(2)} is suited for ${(level * 100).toFixed(1)}% bit-flip noise.`,
      });
    }
    return;
  }

  if (state.damageMode === "lostchunk" && level > ratio * 0.78) {
    setRecommendation({
      status: "warning",
      message:
        `Erasure level (${(level * 100).toFixed(0)}%) nears code limit (m/n=${ratio.toFixed(2)}); ` +
        `stopping sets may block full repair. Raise Protection Level (m/n) to recover more.`,
    });
    return;
  }

  setRecommendation({
    status: "ok",
    message: `Protection m/n=${ratio.toFixed(2)} is well-suited for ${
      state.damageMode === "brush" ? "brush scratch" : `${(level * 100).toFixed(1)}% lost-chunk`
    } damage.`,
  });
}

function initChart() {
  const ctx = $("accuracyChart").getContext("2d");
  state.chart = new Chart(ctx, {
    type: "bar",
    data: {
      labels: [
        "Damaged (No Protection)",
        "ByteGuard LDPC (Repaired)",
        "3x Copies (Reference)",
      ],
      datasets: [
        {
          label: "Correct Bits (%)",
          data: [100, 100, 100],
          backgroundColor: ["#ef4444", "#10b981", "#38bdf8"],
          borderRadius: 6,
          barPercentage: 0.52,
        },
      ],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: { duration: 220 },
      scales: {
        y: {
          min: 0,
          max: 100,
          grid: { color: "rgba(148, 163, 184, 0.14)" },
          ticks: { color: "#cbd5e1", callback: (v) => `${v}%` },
        },
        x: {
          grid: { display: false },
          ticks: { color: "#cbd5e1", font: { size: 11 } },
        },
      },
      plugins: {
        legend: { display: false },
        tooltip: {
          callbacks: {
            label: (ctxItem) => ` ${Number(ctxItem.raw).toFixed(2)}% correct`,
          },
        },
      },
    },
  });
}

function initCytoscape() {
  state.cy = cytoscape({
    container: $("cyContainer"),
    elements: [],
    wheelSensitivity: 0.25,
    style: [
      {
        selector: 'node[kind="check"]',
        style: {
          shape: "round-rectangle",
          width: 32,
          height: 24,
          label: "data(label)",
          "font-size": 10,
          color: "#f8fafc",
          "text-valign": "center",
          "text-halign": "center",
          "background-color": "#16a34a",
          "border-width": 1.5,
          "border-color": "#86efac",
        },
      },
      {
        selector: 'node[kind="check"][status="failed"]',
        style: {
          "background-color": "#dc2626",
          "border-color": "#fca5a5",
        },
      },
      {
        selector: 'node[kind="check"][color_mode="welsh_powell"]',
        style: {
          "background-color": "data(wp_hex)",
          "border-color": "#f8fafc",
        },
      },
      {
        selector: 'node[kind="var"]',
        style: {
          shape: "ellipse",
          width: 34,
          height: 34,
          label: "data(label)",
          "font-size": 9.5,
          color: "#06101f",
          "font-weight": 700,
          "text-valign": "center",
          "text-halign": "center",
          "background-color": "#38bdf8",
          "border-width": 1.5,
          "border-color": "#bae6fd",
        },
      },
      {
        selector: 'node[kind="var"][status="fixed"]',
        style: {
          "background-color": "#10b981",
          "border-color": "#a7f3d0",
          "border-width": 2.5,
        },
      },
      {
        selector: 'node[kind="var"][status="missing"]',
        style: {
          "background-color": "#f59e0b",
          "border-color": "#fde68a",
        },
      },
      {
        selector: 'node[kind="var"][status="wrong"]',
        style: {
          "background-color": "#ef4444",
          "border-color": "#fecaca",
          color: "#ffffff",
        },
      },
      {
        selector: 'node[kind="var"][status="stopping"]',
        style: {
          "background-color": "#a855f7",
          "border-color": "#e9d5ff",
          color: "#ffffff",
          "border-width": 2.5,
        },
      },
      {
        selector: "edge",
        style: {
          width: 1.4,
          "line-color": "#334155",
          opacity: 0.72,
          "curve-style": "straight",
        },
      },
      {
        selector: "edge[?failed]",
        style: {
          width: 2.0,
          "line-color": "#ef4444",
          opacity: 0.9,
        },
      },
    ],
  });
}

function renderCytoscape(elements) {
  if (!state.cy) return;
  state.cy.elements().remove();
  state.cy.add(elements.nodes.concat(elements.edges));
  state.cy.layout({ name: "preset", padding: 24, fit: true }).run();
}

function drawOriginalCanvas() {
  const canvas = $("originalCanvas");
  const ctx = canvas.getContext("2d");
  if (!state.originalDataUrl) return;

  const img = new Image();
  img.onload = () => {
    canvas.width = img.width;
    canvas.height = img.height;
    state.imageWidth = img.width;
    state.imageHeight = img.height;
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.drawImage(img, 0, 0);

    if (state.strokes.length > 0 && state.damageMode === "brush") {
      ctx.save();
      ctx.fillStyle = "rgba(239, 68, 68, 0.55)";
      for (const s of state.strokes) {
        ctx.beginPath();
        ctx.arc(s.x, s.y, s.radius, 0, Math.PI * 2);
        ctx.fill();
      }
      ctx.restore();
    }
  };
  img.src = state.originalDataUrl;
}

function setRecommendation(rec) {
  const box = $("recommendationBox");
  if (!rec) return;
  box.textContent = rec.message;
  box.className = `callout ${rec.status === "warning" ? "callout-warn" : "callout-ok"}`;
}

function updateMetricsUI(m, phase) {
  if (!m) return;

  if (state.chart) {
    state.chart.data.datasets[0].data = [
      m.unprotected_correct_pct,
      m.protected_correct_pct,
      m.triple_copy_correct_pct,
    ];
    state.chart.update();
  }

  $("badgeDamagedPct").textContent = `${m.unprotected_correct_pct.toFixed(1)}%`;
  $("badgeRepairedPct").textContent = `${m.protected_correct_pct.toFixed(1)}%`;

  $("correctBitsFraction").textContent = `${m.correct_data_bits.toLocaleString()} / ${m.total_data_bits.toLocaleString()} bits`;

  $("ovRawLabel").textContent = `1.00× (${m.original_bytes.toLocaleString()} B)`;
  $("ovLdpcLabel").textContent = `${m.ldpc_overhead_ratio.toFixed(2)}× (${m.protected_bytes.toLocaleString()} B)`;
  $("ovTripleLabel").textContent = `3.00× (${m.triple_copy_bytes.toLocaleString()} B)`;
  const ldpcPctWidth = Math.min(100, Math.max(8, (m.ldpc_overhead_ratio / 3.0) * 100));
  $("ovLdpcBar").style.width = `${ldpcPctWidth.toFixed(1)}%`;

  $("kpiFailedChecks").textContent = `${m.failed_checks} / ${m.total_checks}`;
  $("kpiMissingBits").textContent = `${m.remaining_unknown_bits.toLocaleString()}`;
  $("kpiBlocksRepaired").textContent = `${m.fully_repaired_blocks} / ${m.total_blocks}`;
  $("kpiStoppingBits").textContent = `${m.stopping_set_bits_total}`;

  $("badgeCodeRate").textContent = `Rate R = ${m.code_rate.toFixed(2)}`;
  $("badgeSessionPhase").textContent = `Phase: ${phase}`;
}

function updateTelemetryStrip(summary, blockIdx) {
  if (!summary) return;
  $("telGirth").textContent = summary.girth === 0 ? "Acyclic" : `${summary.girth}`;
  $("telConnected").textContent = summary.connected ? "Yes" : "No";
  $("telDims").textContent = `${summary.n} / ${summary.m} (k=${summary.k})`;
  $("telEdges").textContent = `${summary.edges}`;
  $("telColors").textContent = `${summary.num_colors}`;
  $("telBlock").textContent = blockIdx >= 0 ? `#${blockIdx + 1} / ${summary.total_blocks}` : `— / ${summary.total_blocks}`;
  $("badgeGraphStatus").textContent = `PEG Girth ${summary.girth} · ${summary.connected ? "Connected" : "Split"}`;
}

async function refreshGraph() {
  if (state.graphView === "hamming") {
    $("hammingControls").classList.remove("hidden");
    $("graphSubtitle").textContent = "Interactive Figure 1 & 6 Hamming(7,4) sandbox driven by C++ PeelingDecoder & BitFlipDecoder.";
    await runHammingSandbox();
    return;
  }

  $("hammingControls").classList.add("hidden");
  $("graphSubtitle").textContent =
    state.graphColorMode === "welsh_powell"
      ? "Check nodes colored by Welsh-Powell vertex coloring (same color = conflict-free parallel checks)."
      : "Inspecting the active damaged block's bipartite Tanner graph (checks at top, bits at bottom).";

  const snap = state.snapshots[state.currentRoundIdx];
  const r = snap ? snap.round : -1;
  const resp = await fetch(`/graph?round=${r}&view=${state.graphView}&color_mode=${state.graphColorMode}`);
  if (!resp.ok) return;
  const data = await resp.json();
  renderCytoscape(data.elements);
  const blkIdx = data.inspected_block_summary ? data.inspected_block_summary.block_index : 0;
  updateTelemetryStrip(data.graph_summary, blkIdx);
}

function displaySnapshot(idx) {
  if (!state.snapshots.length) return;
  const clamped = Math.max(0, Math.min(idx, state.snapshots.length - 1));
  state.currentRoundIdx = clamped;
  const snap = state.snapshots[clamped];

  $("imgRepaired").src = snap.repaired_png;
  $("repairedPhaseFoot").textContent = `Round ${snap.round} (${snap.phase})`;
  $("roundSlider").value = String(clamped);
  $("roundReadout").textContent = `Round ${snap.round} / ${state.snapshots[state.snapshots.length - 1].round}`;
  updateMetricsUI(snap.metrics, snap.phase);
  refreshGraph();
}

function stopAnimation() {
  if (state.playTimer) {
    clearInterval(state.playTimer);
    state.playTimer = null;
  }
  $("btnPlayPause").textContent = "Play";
}

function startAnimation() {
  stopAnimation();
  if (state.snapshots.length <= 1) return;
  if (state.currentRoundIdx >= state.snapshots.length - 1) {
    displaySnapshot(0);
  }
  $("btnPlayPause").textContent = "Pause";
  state.playTimer = setInterval(() => {
    if (state.currentRoundIdx < state.snapshots.length - 1) {
      displaySnapshot(state.currentRoundIdx + 1);
    } else {
      stopAnimation();
    }
  }, 420);
}

async function runProtect() {
  stopAnimation();
  const cfg = currentCodeConfig();
  const maxDim = $("autoFitCheckbox").checked ? 320 : 0;

  setButtonState("btnProtect", {
    busy: true,
    text: "Building PEG Graph & Encoding...",
  });
  setStatusLine(
    "protectStatusLine",
    `Constructing PEG(${cfg.n}, ${cfg.m}) & encoding...`,
    "status-pending"
  );

  const fd = new FormData();
  if (state.uploadedFile) {
    fd.append("image_file", state.uploadedFile);
  } else {
    fd.append("sample_name", state.selectedSample);
  }
  fd.append("block_size", String(cfg.n));
  fd.append("protection_checks", String(cfg.m));
  fd.append("seed", String(cfg.seed));
  fd.append("max_dimension", String(maxDim));

  const t0 = performance.now();
  try {
    const resp = await fetch("/protect", { method: "POST", body: fd });
    if (!resp.ok) {
      const err = await resp.json();
      setButtonState("btnProtect", {
        busy: false,
        stateClass: "is-dirty",
        text: "Protect (Retry)",
      });
      setStatusLine("protectStatusLine", err.detail || "Protect failed", "status-warn");
      return;
    }
    const data = await resp.json();
    const elapsedMs = Math.round(performance.now() - t0);

    state.originalDataUrl = data.original_png;
    state.imageWidth = data.width;
    state.imageHeight = data.height;
    state.strokes = [];
    $("strokeCountLabel").textContent = "0";
    drawOriginalCanvas();

    const resizeInfo = data.was_resized
      ? `, scaled from ${data.source_width} × ${data.source_height}`
      : "";
    $("imageMetaLine").textContent = `Loaded: ${data.sample_name} (${data.width} × ${data.height} RGB${resizeInfo})`;
    $("imgDamaged").src = data.original_png;
    $("imgRepaired").src = data.original_png;
    $("repairedPhaseFoot").textContent = "Encoded & protected (awaiting scramble)";

    state.snapshots = [];
    state.currentRoundIdx = 0;
    $("roundSlider").min = "0";
    $("roundSlider").max = "0";
    $("roundSlider").value = "0";
    $("roundSlider").disabled = true;
    $("btnPlayPause").disabled = true;
    $("btnPrevRound").disabled = true;
    $("btnNextRound").disabled = true;
    $("roundReadout").textContent = "Round 0 / 0";

    const gs = data.graph_summary;
    const summaryText = `Built (${gs.n}, ${gs.m}) · Girth ${gs.girth} · ${gs.total_blocks} blocks (${elapsedMs} ms)`;
    state.lastBuiltConfig = {
      n: cfg.n,
      m: cfg.m,
      seed: cfg.seed,
      summaryText,
    };

    setButtonState("btnProtect", {
      busy: false,
      stateClass: "is-done",
      text: "✓ Graph Built & Protected",
    });
    setStatusLine("protectStatusLine", summaryText, "status-ok");
    resetScrambleButton();
    resetRepairButtons(false);
    setStatusLine(
      "repairHintLine",
      "Scramble an image first, then watch Peeling / Bit-Flipping round by round.",
      ""
    );

    updateMetricsUI(data.metrics, "protected");
    updateTelemetryStrip(gs, -1);
    updateLiveRecommendation();
    await refreshGraph();
  } catch (err) {
    setButtonState("btnProtect", {
      busy: false,
      stateClass: "is-dirty",
      text: "Protect (Build Graph & Encode)",
    });
    setStatusLine("protectStatusLine", `Error: ${err.message}`, "status-warn");
  }
}

async function runScramble() {
  stopAnimation();
  const levelPct = parseFloat($("damageSlider").value);
  const payload = {
    mode: state.damageMode,
    level: state.damageMode === "brush" && state.strokes.length > 0 ? 0.0 : levelPct / 100.0,
    seed: parseInt($("seedInput").value || "42", 10),
    strokes: state.damageMode === "brush" ? state.strokes : [],
  };

  setButtonState("btnScramble", {
    busy: true,
    text: "Injecting Damage...",
  });
  setStatusLine("scrambleStatusLine", `Applying ${state.damageMode} corruption...`, "status-pending");

  const t0 = performance.now();
  try {
    const resp = await fetch("/scramble", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    if (!resp.ok) {
      const err = await resp.json();
      setButtonState("btnScramble", {
        busy: false,
        text: "Scramble (Inject Damage)",
      });
      setStatusLine("scrambleStatusLine", err.detail || "Scramble failed", "status-warn");
      return;
    }
    const data = await resp.json();
    const elapsedMs = Math.round(performance.now() - t0);

    $("imgDamaged").src = data.unprotected_damaged_png;

    state.snapshots = [
      {
        round: data.round,
        phase: data.phase,
        repaired_png: data.protected_damaged_png,
        metrics: data.metrics,
        inspected_block_summary: data.inspected_block_summary,
      },
    ];

    $("roundSlider").min = "0";
    $("roundSlider").max = "0";
    $("roundSlider").value = "0";
    $("roundSlider").disabled = true;
    $("btnPlayPause").disabled = true;
    $("btnPrevRound").disabled = true;
    $("btnNextRound").disabled = true;
    resetRepairButtons(true);

    const pctText = data.metrics.protected_correct_pct.toFixed(1);
    setButtonState("btnScramble", {
      busy: false,
      stateClass: "is-done",
      text: `✓ Scrambled (${pctText}% intact)`,
    });
    setStatusLine("scrambleStatusLine", `Damaged in ${elapsedMs} ms · Ready to repair.`, "status-ok");

    setRecommendation(data.recommendation);
    setStatusLine(
      "repairHintLine",
      "Click 'Repair (Animate)' or 'Step +1' to watch recovery round by round.",
      ""
    );
    displaySnapshot(0);
  } catch (err) {
    setButtonState("btnScramble", {
      busy: false,
      text: "Scramble (Inject Damage)",
    });
    setStatusLine("scrambleStatusLine", `Error: ${err.message}`, "status-warn");
  }
}

async function runRepair(stepOnly) {
  stopAnimation();
  const activeBtnId = stepOnly ? "btnRepairStep" : "btnRepairAll";
  const otherBtnId = stepOnly ? "btnRepairAll" : "btnRepairStep";
  setButtonState(activeBtnId, {
    busy: true,
    text: stepOnly ? "Stepping..." : "Decoding...",
  });
  $(otherBtnId).disabled = true;
  setStatusLine("repairHintLine", "Running C++ Tanner-graph decoder...", "status-pending");

  const t0 = performance.now();
  try {
    const resp = await fetch("/repair", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ step_only: stepOnly, max_rounds: 25 }),
    });
    if (!resp.ok) {
      const err = await resp.json();
      resetRepairButtons(true);
      setStatusLine("repairHintLine", err.detail || "Repair failed", "status-warn");
      return;
    }
    const data = await resp.json();
    const elapsedMs = Math.round(performance.now() - t0);

    if (stepOnly) {
      const newSnap = data.snapshots[0];
      if (!state.snapshots.length || newSnap.round > state.snapshots[state.snapshots.length - 1].round) {
        state.snapshots.push(newSnap);
      }
      const maxIdx = state.snapshots.length - 1;
      $("roundSlider").max = String(maxIdx);
      $("roundSlider").disabled = maxIdx === 0;
      $("btnPlayPause").disabled = maxIdx === 0;
      $("btnPrevRound").disabled = maxIdx === 0;
      $("btnNextRound").disabled = maxIdx === 0;
      displaySnapshot(maxIdx);
    } else {
      state.snapshots = data.snapshots;
      const maxIdx = Math.max(0, state.snapshots.length - 1);
      $("roundSlider").max = String(maxIdx);
      $("roundSlider").disabled = maxIdx === 0;
      $("btnPlayPause").disabled = maxIdx === 0;
      $("btnPrevRound").disabled = maxIdx === 0;
      $("btnNextRound").disabled = maxIdx === 0;
      displaySnapshot(0);
      if (maxIdx > 0) {
        startAnimation();
      }
    }

    const finalSnap = state.snapshots[state.snapshots.length - 1];
    const finalPct = finalSnap.metrics.protected_correct_pct;
    const isFull = finalPct >= 99.999;

    if (data.is_finished) {
      setButtonState("btnRepairStep", {
        busy: false,
        stateClass: isFull ? "is-done" : "is-partial",
        text: isFull ? "✓ Done" : "Stopped",
      });
      $("btnRepairStep").disabled = true;
      setButtonState("btnRepairAll", {
        busy: false,
        stateClass: isFull ? "is-done" : "is-partial",
        text: isFull
          ? `✓ Repaired 100% (${finalSnap.round} rds)`
          : `⚠ Partial ${finalPct.toFixed(1)}% (Limit)`,
      });
      setStatusLine(
        "repairHintLine",
        isFull
          ? `Full recovery in ${finalSnap.round} rounds (${elapsedMs} ms).`
          : `Stopped at ${finalPct.toFixed(2)}% (${finalSnap.metrics.stopping_set_bits_total} stopping-set bits).`,
        isFull ? "status-ok" : "status-warn"
      );
    } else {
      setButtonState("btnRepairStep", {
        busy: false,
        text: "Step +1",
      });
      setButtonState("btnRepairAll", {
        busy: false,
        text: "Repair (Animate)",
      });
      setStatusLine(
        "repairHintLine",
        `Round ${finalSnap.round}: ${finalPct.toFixed(2)}% correct — step again or animate.`,
        "status-pending"
      );
    }
  } catch (err) {
    resetRepairButtons(true);
    setStatusLine("repairHintLine", `Error: ${err.message}`, "status-warn");
  }
}

/* Hamming(7,4) Interactive Sandbox */
function renderHammingBitPills() {
  const bar = $("hammingBitsBar");
  bar.innerHTML = "";
  state.hamming.varStates.forEach((st, idx) => {
    const btn = document.createElement("button");
    const role = idx < 4 ? "data" : "parity";
    btn.className = `bit-pill ${st === "lost" ? "lost" : st === "flipped" ? "flipped" : ""}`;
    const labelState = st === "lost" ? "MISSING (?)" : st === "flipped" ? "FLIPPED" : "INTACT";
    btn.textContent = `v${idx + 1} (${role}): ${labelState}`;
    btn.addEventListener("click", () => {
      const order = ["ok", "lost", "flipped"];
      const next = order[(order.indexOf(st) + 1) % order.length];
      state.hamming.varStates[idx] = next;
      runHammingSandbox();
    });
    bar.appendChild(btn);
  });
}

function showHammingStep(idx) {
  const steps = state.hamming.steps;
  if (!steps.length) return;
  const clamped = Math.max(0, Math.min(idx, steps.length - 1));
  state.hamming.stepIdx = clamped;
  const st = steps[clamped];

  renderCytoscape(st.elements);
  $("hammingStepLabel").textContent = `Step ${clamped} / ${steps.length - 1}`;
  if (clamped === 0) {
    $("hammingExplainLine").textContent =
      `Initial damaged codeword [${st.bits.map((b) => (b === -1 ? "?" : b)).join(", ")}] | Failed checks: ${
        st.failed_checks.map((c) => `c${c + 1}`).join(", ") || "none"
      }`;
  } else {
    const fixedNames = st.fixed_vars.map((v) => `v${v + 1}`).join(", ");
    $("hammingExplainLine").textContent =
      `Step ${clamped} (${st.phase}): resolved ${fixedNames || "none"} -> [${st.bits
        .map((b) => (b === -1 ? "?" : b))
        .join(", ")}]`;
  }
  if (clamped === steps.length - 1 && st.stopping_set.length > 0) {
    $("hammingExplainLine").textContent += ` | STOPPED: stopping set {${st.stopping_set
      .map((v) => `v${v + 1}`)
      .join(", ")}}`;
  }
}

async function runHammingSandbox() {
  renderHammingBitPills();
  const lost = [];
  const flipped = [];
  state.hamming.varStates.forEach((st, i) => {
    if (st === "lost") lost.push(i);
    if (st === "flipped") flipped.push(i);
  });

  const resp = await fetch("/api/hamming/run", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      data_bits: state.hamming.dataBits,
      lost_vars: lost,
      flipped_vars: flipped,
    }),
  });
  if (!resp.ok) return;
  const data = await resp.json();
  state.hamming.steps = data.steps;
  $("telGirth").textContent = `${data.girth}`;
  $("telConnected").textContent = data.connected ? "Yes" : "No";
  $("telDims").textContent = "7 / 3 (k=4)";
  $("telEdges").textContent = `${data.edges}`;
  $("telColors").textContent = "3";
  $("telBlock").textContent = "Hamming(7,4)";
  showHammingStep(0);
}

/* Brush Canvas Events */
function addCanvasPoint(ev) {
  const canvas = $("originalCanvas");
  const rect = canvas.getBoundingClientRect();
  if (!rect.width || !rect.height) return;
  const x = ((ev.clientX - rect.left) / rect.width) * canvas.width;
  const y = ((ev.clientY - rect.top) / rect.height) * canvas.height;
  const r = state.brushRadius * Math.max(1.0, canvas.width / 128.0);
  state.strokes.push({ x, y, radius: r });
  $("strokeCountLabel").textContent = String(state.strokes.length);
  drawOriginalCanvas();
  resetScrambleButton();
}

function handlePickedImageFile(file) {
  if (!file) return;
  state.uploadedFile = file;
  document.querySelectorAll(".sample-card").forEach((c) => c.classList.remove("active"));
  runProtect();
}

function setupEvents() {
  $("blockSizeSelect").addEventListener("change", () => {
    updateProtectionLabel();
    markProtectDirtyIfChanged();
    updateLiveRecommendation();
  });
  $("protectionSlider").addEventListener("input", () => {
    updateProtectionLabel();
    markProtectDirtyIfChanged();
    updateLiveRecommendation();
  });
  $("seedInput").addEventListener("input", () => {
    markProtectDirtyIfChanged();
    resetScrambleButton();
  });
  $("damageSlider").addEventListener("input", () => {
    updateDamageLabel();
    resetScrambleButton();
    updateLiveRecommendation();
  });

  $("brushRadiusSlider").addEventListener("input", () => {
    state.brushRadius = parseInt($("brushRadiusSlider").value, 10);
    $("brushRadiusLabel").textContent = `${state.brushRadius} px`;
  });

  $("btnClearBrush").addEventListener("click", () => {
    state.strokes = [];
    $("strokeCountLabel").textContent = "0";
    drawOriginalCanvas();
    resetScrambleButton();
  });

  document.querySelectorAll(".mode-tab").forEach((btn) => {
    btn.addEventListener("click", () => {
      document.querySelectorAll(".mode-tab").forEach((b) => b.classList.remove("active"));
      btn.classList.add("active");
      state.damageMode = btn.dataset.mode;

      if (state.damageMode === "brush") {
        $("brushControls").classList.remove("hidden");
        $("damageLevelRow").classList.add("hidden");
        $("originalCanvasWrap").classList.add("brush-active");
        $("brushFootHint").textContent = "Draw scratch directly on image (or drop a new file)";
      } else {
        $("brushControls").classList.add("hidden");
        $("damageLevelRow").classList.remove("hidden");
        $("originalCanvasWrap").classList.remove("brush-active");
        $("brushFootHint").textContent = "Source image (drop file here to load)";
        const slider = $("damageSlider");
        if (state.damageMode === "bitflip") {
          slider.min = "0";
          slider.max = "10";
          slider.step = "0.5";
          if (parseFloat(slider.value) > 10 || parseFloat(slider.value) === 20) {
            slider.value = "1.5";
          } else if (parseFloat(slider.value) > 4) {
            slider.value = "1.5";
          }
          updateDamageLabel();
        } else if (state.damageMode === "lostchunk") {
          slider.min = "0";
          slider.max = "60";
          slider.step = "1";
          if (parseFloat(slider.value) < 5) {
            slider.value = "20";
          }
          updateDamageLabel();
        }
      }
      resetScrambleButton();
      updateLiveRecommendation();
      drawOriginalCanvas();
    });
  });

  $("btnOpenFile").addEventListener("click", () => $("imageUpload").click());
  $("btnOpenFileTop").addEventListener("click", () => $("imageUpload").click());
  $("imageUpload").addEventListener("change", (ev) => {
    const file = ev.target.files && ev.target.files[0];
    ev.target.value = "";
    handlePickedImageFile(file);
  });

  const dropWrap = $("originalCanvasWrap");
  ["dragenter", "dragover"].forEach((evt) => {
    dropWrap.addEventListener(evt, (ev) => {
      ev.preventDefault();
      dropWrap.classList.add("drop-active");
    });
  });
  ["dragleave", "drop"].forEach((evt) => {
    dropWrap.addEventListener(evt, (ev) => {
      ev.preventDefault();
      dropWrap.classList.remove("drop-active");
    });
  });
  dropWrap.addEventListener("drop", (ev) => {
    const file = ev.dataTransfer && ev.dataTransfer.files && ev.dataTransfer.files[0];
    if (file && file.type.startsWith("image/")) {
      handlePickedImageFile(file);
    }
  });

  $("btnProtect").addEventListener("click", runProtect);
  $("btnScramble").addEventListener("click", runScramble);
  $("btnRepairAll").addEventListener("click", () => runRepair(false));
  $("btnRepairStep").addEventListener("click", () => runRepair(true));

  $("btnPlayPause").addEventListener("click", () => {
    if (state.playTimer) stopAnimation();
    else startAnimation();
  });
  $("btnPrevRound").addEventListener("click", () => {
    stopAnimation();
    displaySnapshot(state.currentRoundIdx - 1);
  });
  $("btnNextRound").addEventListener("click", () => {
    stopAnimation();
    displaySnapshot(state.currentRoundIdx + 1);
  });
  $("roundSlider").addEventListener("input", () => {
    stopAnimation();
    displaySnapshot(parseInt($("roundSlider").value, 10));
  });

  document.querySelectorAll(".gv-tab").forEach((btn) => {
    btn.addEventListener("click", () => {
      document.querySelectorAll(".gv-tab").forEach((b) => b.classList.remove("active"));
      btn.classList.add("active");
      state.graphView = btn.dataset.view;
      state.graphColorMode = btn.dataset.color || "status";
      refreshGraph();
    });
  });

  $("btnHammingTC06").addEventListener("click", () => {
    state.hamming.varStates = ["lost", "ok", "lost", "ok", "lost", "ok", "ok"];
    runHammingSandbox();
  });
  $("btnHammingTC07").addEventListener("click", () => {
    state.hamming.varStates = ["lost", "lost", "lost", "ok", "ok", "ok", "ok"];
    runHammingSandbox();
  });
  $("btnHammingTC08").addEventListener("click", () => {
    state.hamming.varStates = ["ok", "ok", "ok", "flipped", "ok", "ok", "ok"];
    runHammingSandbox();
  });
  $("btnHammingPrev").addEventListener("click", () => showHammingStep(state.hamming.stepIdx - 1));
  $("btnHammingNext").addEventListener("click", () => showHammingStep(state.hamming.stepIdx + 1));

  const canvas = $("originalCanvas");
  canvas.addEventListener("pointerdown", (ev) => {
    if (state.damageMode !== "brush") return;
    state.isDrawing = true;
    canvas.setPointerCapture(ev.pointerId);
    addCanvasPoint(ev);
  });
  canvas.addEventListener("pointermove", (ev) => {
    if (!state.isDrawing || state.damageMode !== "brush") return;
    addCanvasPoint(ev);
  });
  window.addEventListener("pointerup", () => {
    state.isDrawing = false;
  });
}

async function loadSamples() {
  const resp = await fetch("/api/samples");
  const data = await resp.json();
  state.samples = data.samples || [];
  const grid = $("sampleList");
  grid.innerHTML = "";
  state.samples.forEach((s) => {
    const card = document.createElement("button");
    card.type = "button";
    card.className = `sample-card ${s.name === state.selectedSample ? "active" : ""}`;
    const shortLabel = s.label.replace(/\s*\(.*?\)/g, "");
    card.innerHTML = `<img src="${s.url}" alt="${s.label}"><span>${shortLabel}</span>`;
    card.addEventListener("click", () => {
      state.uploadedFile = null;
      state.selectedSample = s.name;
      document.querySelectorAll(".sample-card").forEach((c) => c.classList.remove("active"));
      card.classList.add("active");
      runProtect();
    });
    grid.appendChild(card);
  });
}

window.addEventListener("DOMContentLoaded", async () => {
  updateProtectionLabel();
  updateDamageLabel();
  initChart();
  initCytoscape();
  setupEvents();
  await loadSamples();
  await runProtect();
});
