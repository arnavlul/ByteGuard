#include "session.cpp"

void require(bool cond, string msg) {
    if (!cond) {
        throw runtime_error(msg);
    }
}

Image makeSyntheticImage(int w, int h, uint32_t seed) {
    Image img{w, h, 3, vector<uint8_t>((size_t)w * h * 3U)};
    mt19937 rng(seed);
    for (uint8_t& px : img.pixels) {
        px = (uint8_t)(rng() & 0xFFU);
    }
    return img;
}

void testTC01() {
    TannerGraph g = TannerGraph::hamming74();
    require(g.numVars() == 7 && g.numChecks() == 3, "Expected 7 vars and 3 checks");
    require(g.numVars() + g.numChecks() == 10, "Expected 10 total nodes");
    require(g.numEdges() == 12, "Expected 12 edges");

    Matrix H = g.incidenceMatrix();
    require(H.size() == 3 && H[0].size() == 7, "Unexpected H dimensions");
    for (int c = 0; c < 3; ++c) {
        int rowSum = 0;
        for (int v = 0; v < 7; ++v) {
            rowSum += H[c][v];
        }
        require(rowSum == g.checkDegree(c), "Check degree mismatch with H row sum");
    }
    for (int v = 0; v < 7; ++v) {
        int colSum = 0;
        for (int c = 0; c < 3; ++c) {
            colSum += H[c][v];
        }
        require(colSum == g.degree(v), "Variable degree mismatch with H col sum");
    }
}

void testTC02() {
    TannerGraph g = TannerGraph::hamming74();
    GraphAnalyzer analyzer(g);
    require(analyzer.girth() == 4, "Hamming(7,4) girth must be 4");
    require(analyzer.isConnected(), "Hamming(7,4) Tanner graph must be connected");
    require(!analyzer.layerSchedule().empty(), "Welsh-Powell layers must not be empty");
}

void testTC03() {
    vector<pair<int, int>> configs = {{576, 288}, {288, 144}, {576, 192}, {576, 360}};
    PEGBuilder builder(42);
    for (auto& cfg : configs) {
        int n = cfg.first, m = cfg.second;
        TannerGraph g = builder.build(n, m);
        for (int v = 0; v < n; ++v) {
            require(g.degree(v) == 3, "Variable degree must be 3");
        }
        int minC = n, maxC = 0;
        for (int c = 0; c < m; ++c) {
            minC = min(minC, g.checkDegree(c));
            maxC = max(maxC, g.checkDegree(c));
        }
        require(maxC - minC <= 2, "Check degrees must remain near-uniform");
        GraphAnalyzer analyzer(g);
        require(analyzer.girth() >= 6, "PEG code girth must be >= 6");
        require(analyzer.isConnected(), "PEG code must be connected");
    }
}

void testTC04() {
    TannerGraph g = PEGBuilder(42).build(576, 288);
    Encoder enc(g);
    require(enc.numInfoBits() + enc.numParityBits() == 576, "Info + parity must equal n");

    mt19937 rng(2026);
    uniform_int_distribution<int> bitDist(0, 1);
    for (int trial = 0; trial < 1000; ++trial) {
        Bits data(enc.numInfoBits());
        for (int& b : data) {
            b = bitDist(rng);
        }
        Bits code = enc.encode(data);
        require(g.isValidCodeword(code), "Encoded block failed parity checks");
        require(enc.extract(code) == data, "Systematic data bits modified by encoder");
    }
}

void testTC05() {
    for (auto& sz : vector<pair<int, int>>{{1, 1}, {17, 19}, {37, 53}}) {
        int w = sz.first, h = sz.second;
        Image img = makeSyntheticImage(w, h, 7);
        Session s(img, 576, 288, 42);
        s.protect();
        s.scramble("lostchunk", 0.15, 11);
        auto hist = s.repairAll();
        require(!hist.empty(), "Repair history empty");
        auto& last = hist.back();
        require(last.repairedImage.width == w && last.repairedImage.height == h,
                "Repaired image dimensions changed");
        require(last.repairedImage.pixels == img.pixels,
                "Odd-size image failed exact byte-for-byte repair");
        require(last.metrics.protectedCorrectPct == 100.0,
                "Expected 100% accuracy on odd-size image");
    }
}

// Worked Hamming(7,4) peeling example.
void testTC06() {
    TannerGraph h = TannerGraph::hamming74();
    Bits orig = {1, 0, 0, 0, 1, 1, 0};
    require(h.isValidCodeword(orig), "Figure 6 codeword must be valid");
    Bits rx = orig;
    rx[0] = BIT_UNKNOWN;
    rx[2] = BIT_UNKNOWN;
    rx[4] = BIT_UNKNOWN;

    PeelingDecoder dec(h);
    int steps = 0;
    while (dec.step(rx)) {
        ++steps;
        require(dec.lastFixedVars().size() == 1, "Expected 1 bit peeled per step in Figure 6");
    }
    require(steps == 3, "Expected exactly 3 peeling steps");
    require(rx == orig, "Peeling decoder failed to recover Figure 6 codeword");
}

void testTC07() {
    TannerGraph h = TannerGraph::hamming74();
    Bits rx = {BIT_UNKNOWN, BIT_UNKNOWN, BIT_UNKNOWN, 0, 1, 1, 0};
    PeelingDecoder dec(h);
    require(!dec.step(rx), "Peeling must stop immediately on stopping set {v1, v2, v3}");
    vector<int> expected = {0, 1, 2};
    require(dec.remainingUnknowns(rx) == expected, "Unexpected remaining unknowns");
    require(GraphAnalyzer(h).stoppingSet(dec.remainingUnknowns(rx)) == expected,
            "Unexpected stopping set report");
}

void testTC08() {
    TannerGraph g = PEGBuilder(42).build(576, 288);
    Encoder enc(g);
    Bits data(enc.numInfoBits(), 0);
    for (size_t i = 0; i < data.size(); i += 3) {
        data[i] = 1;
    }
    Bits orig = enc.encode(data);

    BitFlipDecoder dec(g);
    for (int v : {0, 41, 150, 287, 420, 575}) {
        dec.reset();
        Bits rx = orig;
        rx[v] ^= 1;
        require(dec.step(rx), "Expected bit-flip step on single error");
        require(rx == orig, "Single flipped bit not fixed in round 1");
        require(!dec.step(rx), "Expected convergence after 1 round");
    }
}

void testTC09() {
    TannerGraph g = PEGBuilder(42).build(576, 288);
    Encoder enc(g);
    Bits orig = enc.encode(Bits(enc.numInfoBits(), 1));

    int u = 0;
    int sharedCheck = g.varNeighbors(u)[0];
    int w = -1;
    for (int cand : g.chkNeighbors(sharedCheck)) {
        if (cand != u) {
            w = cand;
            break;
        }
    }
    require(w != -1, "Failed to find neighbor sharing a check");

    Bits rx = orig;
    rx[u] ^= 1;
    rx[w] ^= 1;
    require(g.failedChecks(rx).size() == 4, "Two flipped bits sharing 1 check must still fail 4 checks");

    BitFlipDecoder dec(g);
    while (dec.step(rx)) {
    }
    require(rx == orig, "Failed to recover two flipped bits sharing a check");
}

void testTC10() {
    Image img = makeSyntheticImage(32, 32, 55);
    Session s1(img, 576, 288, 42);
    s1.protect();

    Snapshot zeroSnap = s1.scramble("bitflip", 0.0, 99);
    require(zeroSnap.metrics.protectedCorrectPct == 100.0, "Level 0 should cause 0% damage");
    require(zeroSnap.metrics.unprotectedCorrectPct == 100.0, "Level 0 unprotected must be 100%");
    auto zeroHist = s1.repairAll();
    require(zeroHist.back().repairedImage.pixels == img.pixels,
            "Level 0 repair altered image bytes");

    Snapshot runA = s1.scramble("lostchunk", 0.25, 777);
    Session s2(img, 576, 288, 42);
    s2.protect();
    Snapshot runB = s2.scramble("lostchunk", 0.25, 777);
    require(runA.repairedImage.pixels == runB.repairedImage.pixels,
            "Same seed must produce identical protected damage");
    require(s1.unprotectedDamagedImage().pixels == s2.unprotectedDamagedImage().pixels,
            "Same seed must produce identical unprotected damage");
}

void testTC11() {
    Image img = makeSyntheticImage(48, 48, 2026);
    Session s(img, 576, 288, 42);
    s.protect();

    s.scramble("bitflip", 0.015, 42);
    auto bfHist = s.repairAll();
    require(bfHist.back().metrics.protectedCorrectPct == 100.0,
            "Expected 100% recovery on 1.5% bit flips");

    s.scramble("lostchunk", 0.20, 42);
    auto lcLowHist = s.repairAll();
    require(lcLowHist.back().metrics.protectedCorrectPct == 100.0,
            "Expected 100% recovery on 20% lost chunks");

    s.scramble("lostchunk", 0.55, 42);
    auto lcHighHist = s.repairAll();
    require(lcHighHist.back().metrics.protectedCorrectPct >
                lcHighHist.front().metrics.protectedCorrectPct,
            "Expected partial recovery on 55% lost chunks");
    require(lcHighHist.back().metrics.protectedCorrectPct < 100.0,
            "55% lost chunks exceeds rate-1/2 capacity and must be partial");
    require(lcHighHist.back().metrics.stoppingSetBitsTotal > 0,
            "Expected stopping set bits to be reported at 55% loss");

    s.scramble("brush", 0.0, 42, {{24.0, 24.0, 6.0}, {28.0, 24.0, 6.0}});
    auto brushHist = s.repairAll();
    require(brushHist.back().metrics.protectedCorrectPct == 100.0,
            "Expected 100% recovery on small scratch brush");
}

// Runs C++ engine test cases TC-01 through TC-11.
int main() {
    struct Case {
        const char* id;
        const char* desc;
        void (*fn)();
    };

    vector<Case> suite = {
        {"TC-01", "TannerGraph Hamming(7,4) nodes/edges/degrees", testTC01},
        {"TC-02", "GraphAnalyzer girth=4 & connectivity", testTC02},
        {"TC-03", "PEGBuilder girth>=6, connectivity, degrees across (n, m)", testTC03},
        {"TC-04", "Encoder 1,000 random PEG blocks pass all checks", testTC04},
        {"TC-05", "Session handles odd image sizes with padding & restoration", testTC05},
        {"TC-06", "PeelingDecoder recovers Hamming(7,4) {v1,v3,v5} in 3 steps", testTC06},
        {"TC-07", "PeelingDecoder halts on stopping set {v1,v2,v3}", testTC07},
        {"TC-08", "BitFlipDecoder fixes single flipped bit in 1 round", testTC08},
        {"TC-09", "BitFlipDecoder detects & fixes 2 flipped bits sharing a check", testTC09},
        {"TC-10", "Session level=0 no-op & seed determinism", testTC10},
        {"TC-11", "End-to-end sweep: full low-damage & partial limit w/ stopping sets", testTC11},
    };

    int failed = 0;
    for (auto& tc : suite) {
        try {
            tc.fn();
            cout << "[PASS] " << tc.id << " - " << tc.desc << "\n";
        } catch (exception& ex) {
            ++failed;
            cerr << "[FAIL] " << tc.id << " - " << tc.desc << ": " << ex.what() << "\n";
        }
    }

    if (failed == 0) {
        cout << "\nAll " << suite.size() << " ByteGuard C++ test cases passed.\n";
    } else {
        cerr << "\n" << failed << " test case(s) FAILED.\n";
    }
    return failed == 0 ? 0 : 1;
}
