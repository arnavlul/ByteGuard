#pragma once

#include "graph.cpp"

double clamp01(double x) {
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

// Systematic GF(2) LDPC encoder using upper-triangular elimination and Kahn's sort.
class Encoder {
    int n = 0;
    int m = 0;
    vector<int> infoPos;
    vector<int> parityPos;
    vector<vector<int>> dataDeps;
    vector<vector<int>> parityDeps;
    vector<int> order;

public:
    Encoder(TannerGraph& g) {
        n = g.numVars();
        m = g.numChecks();
        if (n == 0) {
            return;
        }

        // Copy parity-check matrix H from the Tanner graph
        vector<vector<int>> H(m, vector<int>(n, 0));
        for (int c = 0; c < m; c++) {
            for (int v : g.chkNeighbors(c)) {
                H[c][v] = 1;
            }
        }

        // Prefer putting parity bits at the right end of the codeword
        vector<int> colOrder;
        int tailStart = max(0, n - m);
        for (int c = tailStart; c < n; c++) {
            colOrder.push_back(c);
        }
        for (int c = tailStart - 1; c >= 0; c--) {
            colOrder.push_back(c);
        }

        // Step 1: Forward Gaussian elimination over GF(2) (XOR row operations)
        vector<bool> isParity(n, false);
        int pivotRow = 0;
        for (int col : colOrder) {
            if (pivotRow >= m) {
                break;
            }

            // Pick the sparsest row with a 1 in this column
            int bestRow = -1;
            int bestWeight = INT_MAX;
            for (int r = pivotRow; r < m; r++) {
                if (H[r][col] == 1) {
                    int w = 0;
                    for (int j = 0; j < n; j++) {
                        w += H[r][j];
                    }
                    if (w < bestWeight) {
                        bestWeight = w;
                        bestRow = r;
                    }
                }
            }
            if (bestRow == -1) {
                continue;
            }

            swap(H[pivotRow], H[bestRow]);
            parityPos.push_back(col);
            isParity[col] = true;

            for (int r = pivotRow + 1; r < m; r++) {
                if (H[r][col] == 1) {
                    for (int j = 0; j < n; j++) {
                        H[r][j] ^= H[pivotRow][j];
                    }
                }
            }
            pivotRow++;
        }

        // Separate columns into parity positions and info (data) positions
        int rank = (int)parityPos.size();
        vector<int> colToParity(n, -1);
        for (int p = 0; p < rank; p++) {
            colToParity[parityPos[p]] = p;
        }
        for (int v = 0; v < n; v++) {
            if (!isParity[v]) {
                infoPos.push_back(v);
            }
        }

        // Step 2: Build parity dependency DAG (q -> p means parity p needs parity q first)
        dataDeps.resize(rank);
        parityDeps.resize(rank);
        vector<vector<int>> dagAdj(rank);
        vector<int> inDegree(rank, 0);

        for (int p = 0; p < rank; p++) {
            int selfCol = parityPos[p];
            for (int col = 0; col < n; col++) {
                if (col == selfCol || H[p][col] == 0) {
                    continue;
                }
                if (!isParity[col]) {
                    dataDeps[p].push_back(col);
                } else {
                    int q = colToParity[col];
                    parityDeps[p].push_back(q);
                    dagAdj[q].push_back(p);
                    inDegree[p]++;
                }
            }
        }

        // Step 3: Kahn's Topological Sort to get safe parity computation order
        queue<int> q;
        for (int p = 0; p < rank; p++) {
            if (inDegree[p] == 0) {
                q.push(p);
            }
        }
        while (!q.empty()) {
            int u = q.front();
            q.pop();
            order.push_back(u);
            for (int next : dagAdj[u]) {
                inDegree[next]--;
                if (inDegree[next] == 0) {
                    q.push(next);
                }
            }
        }
        if ((int)order.size() != rank) {
            throw logic_error("Parity dependency graph unexpectedly contains a cycle");
        }
    }

    Bits encode(Bits data) {
        if ((int)data.size() != numInfoBits()) {
            throw invalid_argument("Input data length must equal numInfoBits()");
        }
        Bits code(n, 0);
        for (int i = 0; i < (int)infoPos.size(); i++) {
            if (data[i] != 0 && data[i] != 1) {
                throw invalid_argument("Input data bits must be 0 or 1");
            }
            code[infoPos[i]] = data[i];
        }
        for (int p : order) {
            int val = 0;
            for (int dCol : dataDeps[p]) {
                val ^= code[dCol];
            }
            for (int q : parityDeps[p]) {
                val ^= code[parityPos[q]];
            }
            code[parityPos[p]] = val;
        }
        return code;
    }

    Bits extract(Bits code) {
        if ((int)code.size() != n) {
            throw invalid_argument("Codeword length must equal numVars()");
        }
        Bits data;
        for (int col : infoPos) {
            data.push_back(code[col]);
        }
        return data;
    }

    int numVars() { return n; }
    int numChecks() { return m; }
    int numInfoBits() { return (int)infoPos.size(); }
    int numParityBits() { return (int)parityPos.size(); }
    vector<int>& infoPositions() { return infoPos; }
    vector<int>& parityPositions() { return parityPos; }
    vector<int>& topoOrder() { return order; }
};

// Damage models: random bit flips, contiguous lost chunks, and pixel brush scratches.
class BitFlipCorruptor {
    mt19937 rng;

public:
    BitFlipCorruptor(uint32_t seed = 42) : rng(seed) {}

    void apply(Bits& bits, double level) {
        if (bits.empty() || level <= 0.0) {
            return;
        }
        bernoulli_distribution flip(clamp01(level));
        for (int i = 0; i < (int)bits.size(); i++) {
            if (bits[i] != BIT_UNKNOWN && flip(rng)) {
                bits[i] ^= 1;
            }
        }
    }
};

class LostChunkCorruptor {
    mt19937 rng;

public:
    LostChunkCorruptor(uint32_t seed = 42) : rng(seed) {}

    void apply(Bits& bits, double level) {
        if (bits.empty() || level <= 0.0) {
            return;
        }
        double p = clamp01(level);
        size_t n = bits.size();
        size_t target = (size_t)llround(p * (double)n);
        if (target == 0) {
            return;
        }
        if (target >= n) {
            for (size_t i = 0; i < n; i++) {
                bits[i] = BIT_UNKNOWN;
            }
            return;
        }

        size_t chunkMin = min(target, max((size_t)4, target / 24));
        size_t chunkMax = min(target, max(chunkMin, target / 8));
        uniform_int_distribution<size_t> posDist(0, n - 1);
        uniform_int_distribution<size_t> lenDist(chunkMin, chunkMax);

        size_t erased = 0;
        size_t maxAttempts = target * 4 + 64;
        for (size_t attempt = 0; attempt < maxAttempts && erased < target; attempt++) {
            size_t start = posDist(rng);
            size_t len = min(lenDist(rng), target - erased);
            size_t end = min(n, start + len);
            for (size_t i = start; i < end && erased < target; i++) {
                if (bits[i] != BIT_UNKNOWN) {
                    bits[i] = BIT_UNKNOWN;
                    erased++;
                }
            }
        }

        if (erased < target) {
            size_t offset = posDist(rng);
            for (size_t step = 0; step < n && erased < target; step++) {
                size_t i = (offset + step) % n;
                if (bits[i] != BIT_UNKNOWN) {
                    bits[i] = BIT_UNKNOWN;
                    erased++;
                }
            }
        }
    }
};

struct BrushStroke {
    double x = 0.0;
    double y = 0.0;
    double radius = 8.0;
};

class BrushCorruptor {
    mt19937 rng;
    int width = 0;
    int height = 0;
    int channels = 3;
    vector<BrushStroke> strokeList;

public:
    BrushCorruptor(int w, int h, int ch, vector<BrushStroke> s = {}, uint32_t seed = 42)
        : rng(seed), width(w), height(h), channels(ch), strokeList(s) {}

    vector<int> pixelMask(double level) {
        if (width <= 0 || height <= 0) {
            return {};
        }
        vector<int> mask((size_t)width * height, 0);

        if (!strokeList.empty()) {
            for (BrushStroke s : strokeList) {
                double r = max(0.5, s.radius);
                int minX = max(0, (int)floor(s.x - r));
                int maxX = min(width - 1, (int)ceil(s.x + r));
                int minY = max(0, (int)floor(s.y - r));
                int maxY = min(height - 1, (int)ceil(s.y + r));
                for (int py = minY; py <= maxY; py++) {
                    double dy = py - s.y;
                    for (int px = minX; px <= maxX; px++) {
                        double dx = px - s.x;
                        if (dx * dx + dy * dy <= r * r) {
                            mask[(size_t)py * width + px] = 1;
                        }
                    }
                }
            }
            return mask;
        }

        if (level <= 0.0) {
            return mask;
        }

        double p = clamp01(level);
        int minDim = min(width, height);
        double halfBand = max(1.0, p * minDim * 0.55);
        double normX = max(1, width - 1);
        double normY = max(1, height - 1);
        for (int py = 0; py < height; py++) {
            double ny = py / normY;
            for (int px = 0; px < width; px++) {
                double diagDist = abs(px / normX - ny) * minDim * 0.70710678;
                if (diagDist <= halfBand) {
                    mask[(size_t)py * width + px] = 1;
                }
            }
        }
        return mask;
    }

    void apply(Bits& bits, double level) {
        if (bits.empty() || (level <= 0.0 && strokeList.empty())) {
            return;
        }
        vector<int> mask = pixelMask(level);
        if (mask.empty() || channels <= 0) {
            LostChunkCorruptor(rng()).apply(bits, level);
            return;
        }
        size_t bitsPerPixel = (size_t)channels * 8U;
        for (size_t i = 0; i < bits.size(); i++) {
            size_t pix = (i / bitsPerPixel) % mask.size();
            if (mask[pix] != 0) {
                bits[i] = BIT_UNKNOWN;
            }
        }
    }
};

// Queue-based erasure peeling decoder (processes one wave per step).
class PeelingDecoder {
    TannerGraph* g;
    int maxRounds = 100;
    int rounds = 0;
    vector<int> lastFixed;
    queue<int> q;
    vector<int> unknownCount;
    bool initialized = false;

public:
    PeelingDecoder(TannerGraph& graph, int maxR = 100) {
        g = &graph;
        maxRounds = maxR;
    }

    void reset() {
        while (!q.empty()) {
            q.pop();
        }
        unknownCount.clear();
        lastFixed.clear();
        initialized = false;
        rounds = 0;
    }

    bool step(Bits& bits) {
        lastFixed.clear();
        int n = g->numVars();
        int m = g->numChecks();
        if ((int)bits.size() != n) {
            throw invalid_argument("Bit vector length must equal numVars()");
        }

        if (!initialized) {
            unknownCount.assign(m, 0);
            for (int c = 0; c < m; c++) {
                for (int v : g->chkNeighbors(c)) {
                    if (bits[v] == BIT_UNKNOWN) {
                        unknownCount[c]++;
                    }
                }
                if (unknownCount[c] == 1) {
                    q.push(c);
                }
            }
            initialized = true;
        }
        if (q.empty() || rounds >= maxRounds) {
            return false;
        }

        int waveSize = (int)q.size();
        for (int i = 0; i < waveSize; i++) {
            int c = q.front();
            q.pop();

            int target = -1;
            int unknownSeen = 0;
            int parity = 0;
            for (int v : g->chkNeighbors(c)) {
                if (bits[v] == BIT_UNKNOWN) {
                    target = v;
                    unknownSeen++;
                } else {
                    parity ^= (bits[v] & 1);
                }
            }
            if (unknownSeen != 1 || target == -1) {
                continue;
            }

            bits[target] = parity;
            lastFixed.push_back(target);
            for (int c2 : g->varNeighbors(target)) {
                unknownCount[c2]--;
                if (unknownCount[c2] == 1) {
                    q.push(c2);
                }
            }
        }

        rounds++;
        return !lastFixed.empty();
    }

    vector<int> remainingUnknowns(Bits bits) {
        if ((int)bits.size() != g->numVars()) {
            throw invalid_argument("Bit vector length must equal numVars()");
        }
        vector<int> unknowns;
        for (int v = 0; v < g->numVars(); v++) {
            if (bits[v] == BIT_UNKNOWN) {
                unknowns.push_back(v);
            }
        }
        return unknowns;
    }

    vector<int>& lastFixedVars() { return lastFixed; }
};

// Gallager bit-flipping decoder scheduled across Welsh-Powell check color classes.
class BitFlipDecoder {
    TannerGraph* g;
    int maxRounds = 25;
    int rounds = 0;
    vector<int> lastFixed;
    vector<vector<int>> layerGroups;
    set<Bits> visitedStates;

public:
    BitFlipDecoder(TannerGraph& graph, int maxR = 25) {
        g = &graph;
        maxRounds = maxR;
        layerGroups = GraphAnalyzer(graph).layerSchedule();
    }

    BitFlipDecoder(TannerGraph& graph, vector<vector<int>> preLayers, int maxR = 25) {
        g = &graph;
        maxRounds = maxR;
        layerGroups = preLayers;
    }

    void reset() {
        lastFixed.clear();
        visitedStates.clear();
        rounds = 0;
    }

    bool step(Bits& bits) {
        lastFixed.clear();
        int n = g->numVars();
        if ((int)bits.size() != n) {
            throw invalid_argument("Bit vector length must equal numVars()");
        }
        if (rounds >= maxRounds) {
            return false;
        }

        vector<int> votes(n, 0);
        vector<int> knownChecks(n, 0);
        int failedCount = 0;

        for (int l = 0; l < (int)layerGroups.size(); l++) {
            for (int c : layerGroups[l]) {
                int parity = 0;
                bool hasUnknown = false;
                for (int v : g->chkNeighbors(c)) {
                    if (bits[v] == BIT_UNKNOWN) {
                        hasUnknown = true;
                        break;
                    }
                    parity ^= (bits[v] & 1);
                }
                if (hasUnknown) {
                    continue;
                }
                for (int v : g->chkNeighbors(c)) {
                    knownChecks[v]++;
                    if (parity != 0) {
                        votes[v]++;
                    }
                }
                if (parity != 0) {
                    failedCount++;
                }
            }
        }
        if (failedCount == 0) {
            return false;
        }

        int maxVotes = 0;
        vector<int> flipped;
        for (int v = 0; v < n; v++) {
            if (bits[v] == BIT_UNKNOWN || knownChecks[v] == 0) {
                continue;
            }
            // Only consider bits where strictly more than half of checks failed
            if (votes[v] * 2 <= knownChecks[v]) {
                continue;
            }
            if (votes[v] > maxVotes) {
                maxVotes = votes[v];
                flipped.clear();
                flipped.push_back(v);
            } else if (votes[v] == maxVotes) {
                flipped.push_back(v);
            }
        }
        if (flipped.empty()) {
            return false;
        }

        Bits candidate = bits;
        for (int v : flipped) {
            candidate[v] ^= 1;
        }

        // Break 2-state oscillation loops if we are about to revisit an old state
        if (visitedStates.count(candidate) > 0) {
            if (flipped.size() <= 1) {
                return false;
            }
            candidate = bits;
            candidate[flipped[0]] ^= 1;
            flipped.resize(1);
            if (visitedStates.count(candidate) > 0) {
                return false;
            }
        }

        visitedStates.insert(bits);
        bits = candidate;
        lastFixed = flipped;
        rounds++;
        return true;
    }

    vector<int>& lastFixedVars() { return lastFixed; }
    vector<vector<int>>& layers() { return layerGroups; }
};
