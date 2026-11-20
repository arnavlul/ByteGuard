#pragma once

#include "ldpc.cpp"

// Image/telemetry structs and end-to-end Protect -> Scramble -> Repair session.
struct Image {
    int width = 0;
    int height = 0;
    int channels = 3;
    vector<uint8_t> pixels;
};

struct Metrics {
    double protectedCorrectPct = 100.0;
    double unprotectedCorrectPct = 100.0;
    double damagedProtectedPct = 100.0;
    double tripleCopyCorrectPct = 100.0;
    double codeRate = 0.0;
    double ldpcOverheadRatio = 1.0;
    double tripleCopyOverheadRatio = 3.0;
    size_t originalBytes = 0;
    size_t protectedBytes = 0;
    size_t tripleCopyBytes = 0;
    size_t totalDataBits = 0;
    size_t correctDataBits = 0;
    size_t remainingUnknownBits = 0;
    int failedChecks = 0;
    int totalChecks = 0;
    int totalBlocks = 0;
    int fullyRepairedBlocks = 0;
    int stoppingSetBitsTotal = 0;
    int round = 0;
};

struct BlockGraphSnapshot {
    int blockIndex = 0;
    int round = 0;
    Bits bits;
    Bits originalBits;
    vector<bool> checkFailed;
    vector<int> stoppingSet;
    vector<int> newlyFixedVars;
};

struct Snapshot {
    int round = 0;
    string phase;
    Image repairedImage;
    Metrics metrics;
    BlockGraphSnapshot inspectedBlock;
};

string normalizeMode(string s) {
    for (char& ch : s) {
        ch = (char)tolower((unsigned char)ch);
    }
    if (s == "bitflip" || s == "bit_flip" || s == "flip") {
        return "bitflip";
    }
    if (s == "lostchunk" || s == "lost_chunk" || s == "chunk" || s == "erasure") {
        return "lostchunk";
    }
    if (s == "brush" || s == "scratch") {
        return "brush";
    }
    throw invalid_argument("Unsupported scramble mode: " + s);
}

void applyDamage(string normMode, Bits& bits, double level, uint32_t seed,
                 Image& img, vector<BrushStroke>& strokes) {
    if (normMode == "bitflip") {
        BitFlipCorruptor(seed).apply(bits, level);
    } else if (normMode == "lostchunk") {
        LostChunkCorruptor(seed).apply(bits, level);
    } else {
        BrushCorruptor(img.width, img.height, img.channels, strokes, seed).apply(bits, level);
    }
}

bool blockHasUnknown(Bits& b) {
    return find(b.begin(), b.end(), BIT_UNKNOWN) != b.end();
}

bool anyCheckHasSingleUnknown(TannerGraph& g, Bits& b) {
    for (int c = 0; c < g.numChecks(); ++c) {
        int unk = 0;
        for (int v : g.chkNeighbors(c)) {
            if (b[v] == BIT_UNKNOWN && ++unk > 1) {
                break;
            }
        }
        if (unk == 1) {
            return true;
        }
    }
    return false;
}

vector<int> lostIndices(Bits& b) {
    vector<int> out;
    for (int v = 0; v < (int)b.size(); ++v) {
        if (b[v] == BIT_UNKNOWN) {
            out.push_back(v);
        }
    }
    return out;
}

class Session {
    Image image_;
    TannerGraph graph_;
    Encoder encoder_;

    vector<Bits> originalBlocks_;
    vector<Bits> workingBlocks_;
    vector<PeelingDecoder> peelingDecoders_;
    vector<BitFlipDecoder> bitFlipDecoders_;
    vector<Snapshot> history_;

    Image unprotectedDamagedImage_;
    double unprotectedCorrectPct_ = 100.0;
    double damagedProtectedPct_ = 100.0;
    double tripleCopyCorrectPct_ = 100.0;

    int totalBlocks_ = 0;
    int inspectedBlockIdx_ = 0;
    int repairRound_ = 0;
    bool protected_ = false;
    bool peelingDone_ = false;
    bool bitFlipDone_ = false;

    Bits extractRawImageBits() {
        Bits raw(image_.pixels.size() * 8U, 0);
        for (size_t i = 0; i < image_.pixels.size(); ++i) {
            for (int bit = 0; bit < 8; ++bit) {
                raw[i * 8U + bit] = (image_.pixels[i] >> (7 - bit)) & 1;
            }
        }
        return raw;
    }

    Image bitsToImage(Bits& rawBits) {
        Image out{image_.width, image_.height, image_.channels,
                  vector<uint8_t>(image_.pixels.size(), 0)};
        size_t limit = min(rawBits.size(), out.pixels.size() * 8U);
        for (size_t i = 0; i < limit; ++i) {
            if (rawBits[i] == 1) {
                out.pixels[i / 8U] |= (uint8_t)(1U << (7 - (int)(i % 8U)));
            }
        }
        return out;
    }

    Image reconstructImage(vector<Bits>& blocks) {
        size_t totalBits = image_.pixels.size() * 8U;
        Bits raw(totalBits, 0);
        auto& infoPos = encoder_.infoPositions();
        for (size_t t = 0; t < totalBits; ++t) {
            raw[t] = blocks[t % totalBlocks_][infoPos[t / totalBlocks_]];
        }
        return bitsToImage(raw);
    }

    double computeCorrectPct(Bits& rawA, Bits& rawB) {
        if (rawA.empty() || rawA.size() != rawB.size()) {
            return 0.0;
        }
        size_t matches = 0;
        for (size_t i = 0; i < rawA.size(); ++i) {
            if (rawA[i] != BIT_UNKNOWN && rawA[i] == rawB[i]) {
                ++matches;
            }
        }
        return (100.0 * (double)matches) / (double)rawA.size();
    }

    // Packs interleaved data bits followed by interleaved parity bits across blocks.
    Bits packStream() {
        size_t n = (size_t)graph_.numVars();
        size_t k = (size_t)encoder_.numInfoBits();
        int r = encoder_.numParityBits();
        auto& infoPos = encoder_.infoPositions();
        auto& parityPos = encoder_.parityPositions();
        size_t dataSpan = (size_t)totalBlocks_ * k;

        Bits stream((size_t)totalBlocks_ * n, 0);
        for (size_t t = 0; t < dataSpan; ++t) {
            stream[t] = originalBlocks_[t % totalBlocks_][infoPos[t / totalBlocks_]];
        }
        for (int p = 0; p < r; ++p) {
            for (int b = 0; b < totalBlocks_; ++b) {
                stream[dataSpan + (size_t)p * totalBlocks_ + b] =
                    originalBlocks_[b][parityPos[p]];
            }
        }
        return stream;
    }

    void unpackStream(Bits& stream) {
        size_t k = (size_t)encoder_.numInfoBits();
        int r = encoder_.numParityBits();
        auto& infoPos = encoder_.infoPositions();
        auto& parityPos = encoder_.parityPositions();
        size_t dataSpan = (size_t)totalBlocks_ * k;

        for (size_t t = 0; t < dataSpan; ++t) {
            workingBlocks_[t % totalBlocks_][infoPos[t / totalBlocks_]] = stream[t];
        }
        for (int p = 0; p < r; ++p) {
            for (int b = 0; b < totalBlocks_; ++b) {
                workingBlocks_[b][parityPos[p]] =
                    stream[dataSpan + (size_t)p * totalBlocks_ + b];
            }
        }
    }

    double simulateTripleCopy(Bits& rawOriginal, string normMode, double level,
                              uint32_t seed, vector<BrushStroke>& strokes) {
        if (rawOriginal.empty()) {
            return 100.0;
        }
        size_t recovered = 0;
        if (normMode == "brush") {
            auto mask = BrushCorruptor(
                image_.width, image_.height, image_.channels, strokes, seed).pixelMask(level);
            size_t totalPixels = mask.size();
            size_t bitsPerPixel = (size_t)image_.channels * 8U;
            for (size_t i = 0; i < rawOriginal.size(); ++i) {
                size_t pix = (i / bitsPerPixel) % totalPixels;
                bool c0 = mask[pix] == 0;
                bool c1 = totalPixels < 3U ? c0 : mask[(pix + totalPixels / 3U) % totalPixels] == 0;
                bool c2 = totalPixels < 3U ? c0 : mask[(pix + 2U * totalPixels / 3U) % totalPixels] == 0;
                recovered += (c0 || c1 || c2);
            }
        } else {
            vector<Bits> copies(3, rawOriginal);
            for (int c = 0; c < 3; ++c) {
                applyDamage(normMode, copies[c], level, seed + (uint32_t)((c + 1) * 101), image_, strokes);
            }
            for (size_t i = 0; i < rawOriginal.size(); ++i) {
                if (normMode == "bitflip") {
                    int maj = (copies[0][i] + copies[1][i] + copies[2][i] >= 2) ? 1 : 0;
                    recovered += (maj == rawOriginal[i]);
                } else {
                    recovered += (copies[0][i] != BIT_UNKNOWN ||
                                  copies[1][i] != BIT_UNKNOWN ||
                                  copies[2][i] != BIT_UNKNOWN);
                }
            }
        }
        return (100.0 * (double)recovered) / (double)rawOriginal.size();
    }

    void updateRepairFlags() {
        bool anyUnk = false, morePeelable = false, anyFail = false;
        for (auto& workB : workingBlocks_) {
            if (blockHasUnknown(workB)) {
                anyUnk = true;
                if (anyCheckHasSingleUnknown(graph_, workB)) {
                    morePeelable = true;
                }
            } else if (!graph_.isValidCodeword(workB)) {
                anyFail = true;
            }
        }
        peelingDone_ = !morePeelable;
        bitFlipDone_ = (!anyUnk && !anyFail);
    }

    Metrics computeCurrentMetrics() {
        Metrics m;
        int n = graph_.numVars();
        auto& infoPos = encoder_.infoPositions();
        size_t totalImageBits = image_.pixels.size() * 8U;

        m.originalBytes = image_.pixels.size();
        m.protectedBytes = ((size_t)totalBlocks_ * n + 7U) / 8U;
        m.tripleCopyBytes = m.originalBytes * 3U;
        m.codeRate = (n > 0) ? ((double)encoder_.numInfoBits() / n) : 0.0;
        m.ldpcOverheadRatio = m.originalBytes > 0
            ? ((double)m.protectedBytes / (double)m.originalBytes)
            : 1.0;
        m.tripleCopyOverheadRatio = 3.0;
        m.totalDataBits = totalImageBits;
        m.totalBlocks = totalBlocks_;
        m.totalChecks = totalBlocks_ * graph_.numChecks();
        m.round = repairRound_;

        for (size_t t = 0; t < totalImageBits; ++t) {
            size_t b = t % totalBlocks_;
            int v = infoPos[t / totalBlocks_];
            int cur = workingBlocks_[b][v];
            if (cur == BIT_UNKNOWN) {
                ++m.remainingUnknownBits;
            } else if (cur == originalBlocks_[b][v]) {
                ++m.correctDataBits;
            }
        }

        m.protectedCorrectPct = totalImageBits > 0
            ? (100.0 * (double)m.correctDataBits) / (double)totalImageBits
            : 100.0;
        m.unprotectedCorrectPct = unprotectedCorrectPct_;
        m.damagedProtectedPct = history_.empty() ? m.protectedCorrectPct : damagedProtectedPct_;
        m.tripleCopyCorrectPct = tripleCopyCorrectPct_;

        GraphAnalyzer analyzer(graph_);
        for (int b = 0; b < totalBlocks_; ++b) {
            m.fullyRepairedBlocks += (workingBlocks_[b] == originalBlocks_[b]);
            m.failedChecks += (int)graph_.failedChecks(workingBlocks_[b]).size();
            auto lost = lostIndices(workingBlocks_[b]);
            if (!lost.empty()) {
                m.stoppingSetBitsTotal += (int)analyzer.stoppingSet(lost).size();
            }
        }
        return m;
    }

    BlockGraphSnapshot captureBlockSnapshot(int blockIdx, int round, vector<int>& newlyFixed) {
        BlockGraphSnapshot snap;
        if (totalBlocks_ <= 0 || workingBlocks_.empty()) {
            return snap;
        }
        int safeIdx = clamp(blockIdx, 0, totalBlocks_ - 1);
        snap.blockIndex = safeIdx;
        snap.round = round;
        snap.bits = workingBlocks_[safeIdx];
        snap.originalBits = originalBlocks_[safeIdx];
        snap.newlyFixedVars = newlyFixed;

        snap.checkFailed.resize(graph_.numChecks(), false);
        for (int c = 0; c < graph_.numChecks(); ++c) {
            snap.checkFailed[c] = !graph_.isCheckSatisfied(c, snap.bits);
        }
        auto lost = lostIndices(snap.bits);
        if (!lost.empty()) {
            snap.stoppingSet = GraphAnalyzer(graph_).stoppingSet(lost);
        }
        return snap;
    }

    Snapshot makeSnapshot(string phase, vector<int> fixed = {}) {
        Snapshot snap;
        snap.round = repairRound_;
        snap.phase = phase;
        snap.repairedImage = (phase == "protected") ? image_ : reconstructImage(workingBlocks_);
        snap.metrics = computeCurrentMetrics();
        snap.inspectedBlock = captureBlockSnapshot(inspectedBlockIdx_, repairRound_, fixed);
        return snap;
    }

public:
    Session(Image img, int n = 576, int m = 288, uint32_t graphSeed = 42)
        : Session(img, PEGBuilder((int)graphSeed).build(n, m)) {}

    Session(Image img, TannerGraph g)
        : image_(img), graph_(g), encoder_(graph_), unprotectedDamagedImage_(img) {
        if (image_.width <= 0 || image_.height <= 0 || image_.channels <= 0) {
            throw invalid_argument("Image dimensions and channels must be positive");
        }
        size_t expectedBytes = (size_t)image_.width * image_.height * image_.channels;
        if (image_.pixels.size() != expectedBytes) {
            throw invalid_argument("Image pixel buffer size does not match width * height * channels");
        }
        if (encoder_.numInfoBits() <= 0) {
            throw invalid_argument("TannerGraph has zero info bits");
        }
    }

    static Session withGraph(Image img, TannerGraph g) {
        return Session(img, g);
    }

    void protect() {
        Bits raw = extractRawImageBits();
        size_t k = (size_t)encoder_.numInfoBits();
        totalBlocks_ = max(1, (int)((raw.size() + k - 1U) / k));

        vector<Bits> dataBlocks(totalBlocks_, Bits(k, 0));
        for (size_t t = 0; t < raw.size(); ++t) {
            dataBlocks[t % totalBlocks_][t / totalBlocks_] = raw[t];
        }

        originalBlocks_.clear();
        originalBlocks_.reserve(totalBlocks_);
        for (int b = 0; b < totalBlocks_; ++b) {
            originalBlocks_.push_back(encoder_.encode(dataBlocks[b]));
        }

        workingBlocks_ = originalBlocks_;
        unprotectedDamagedImage_ = image_;
        unprotectedCorrectPct_ = 100.0;
        damagedProtectedPct_ = 100.0;
        tripleCopyCorrectPct_ = 100.0;
        peelingDecoders_.clear();
        bitFlipDecoders_.clear();
        history_.clear();
        inspectedBlockIdx_ = 0;
        repairRound_ = 0;
        protected_ = true;
        peelingDone_ = true;
        bitFlipDone_ = true;
    }

    // Applies channel/storage damage to protected, unprotected, and 3x-copy streams.
    Snapshot scramble(string mode, double level, uint32_t seed = 42,
                      vector<BrushStroke> strokes = {}) {
        if (!protected_) {
            protect();
        }
        string normMode = normalizeMode(mode);
        double clampedLevel = clamp(level, 0.0, 1.0);

        workingBlocks_ = originalBlocks_;
        history_.clear();
        repairRound_ = 0;

        Bits stream = packStream();
        applyDamage(normMode, stream, clampedLevel, seed, image_, strokes);
        unpackStream(stream);

        Bits rawOriginal = extractRawImageBits();
        Bits unprotBits = rawOriginal;
        applyDamage(normMode, unprotBits, clampedLevel, seed, image_, strokes);
        unprotectedDamagedImage_ = bitsToImage(unprotBits);
        unprotectedCorrectPct_ = computeCorrectPct(rawOriginal, unprotBits);
        tripleCopyCorrectPct_ = simulateTripleCopy(rawOriginal, normMode, clampedLevel, seed, strokes);

        int bestBlock = 0;
        int maxErrors = -1;
        for (int b = 0; b < totalBlocks_; ++b) {
            int errs = 0;
            for (int v = 0; v < graph_.numVars(); ++v) {
                errs += (workingBlocks_[b][v] != originalBlocks_[b][v]);
            }
            if (errs > maxErrors) {
                maxErrors = errs;
                bestBlock = b;
            }
        }
        inspectedBlockIdx_ = (maxErrors > 0) ? bestBlock : 0;

        auto layers = GraphAnalyzer(graph_).layerSchedule();
        peelingDecoders_.clear();
        bitFlipDecoders_.clear();
        peelingDecoders_.reserve(totalBlocks_);
        bitFlipDecoders_.reserve(totalBlocks_);
        for (int b = 0; b < totalBlocks_; ++b) {
            peelingDecoders_.emplace_back(graph_, 100);
            bitFlipDecoders_.emplace_back(graph_, layers, 25);
        }

        updateRepairFlags();
        Snapshot snap0 = makeSnapshot("damaged");
        damagedProtectedPct_ = snap0.metrics.protectedCorrectPct;
        history_.push_back(snap0);
        return snap0;
    }

    // Advances decoding by one round (peeling waves first, then bit-flipping).
    Snapshot repairStep() {
        if (!protected_) {
            protect();
        }
        if (history_.empty()) {
            return makeSnapshot("protected");
        }
        if (isRepairFinished()) {
            return history_.back();
        }

        vector<int> inspectedFixed;
        if (!peelingDone_) {
            bool peeledAny = false;
            for (int b = 0; b < totalBlocks_; ++b) {
                if (blockHasUnknown(workingBlocks_[b]) && peelingDecoders_[b].step(workingBlocks_[b])) {
                    peeledAny = true;
                    if (b == inspectedBlockIdx_) {
                        inspectedFixed = peelingDecoders_[b].lastFixedVars();
                    }
                }
            }
            if (peeledAny) {
                ++repairRound_;
                updateRepairFlags();
                history_.push_back(makeSnapshot("peeling", inspectedFixed));
                return history_.back();
            }
            peelingDone_ = true;
        }

        if (peelingDone_ && !bitFlipDone_) {
            bool flippedAny = false;
            for (int b = 0; b < totalBlocks_; ++b) {
                auto& workB = workingBlocks_[b];
                if (blockHasUnknown(workB) || graph_.isValidCodeword(workB)) {
                    continue;
                }
                if (bitFlipDecoders_[b].step(workB)) {
                    flippedAny = true;
                    if (b == inspectedBlockIdx_) {
                        inspectedFixed = bitFlipDecoders_[b].lastFixedVars();
                    }
                }
            }
            if (!flippedAny) {
                bitFlipDone_ = true;
                return history_.back();
            }
            ++repairRound_;
            updateRepairFlags();
            history_.push_back(makeSnapshot("bitflip", inspectedFixed));
            return history_.back();
        }

        return history_.back();
    }

    vector<Snapshot> repairAll(int maxRounds = 25) {
        if (!protected_) {
            protect();
        }
        while (!history_.empty() && !isRepairFinished() && repairRound_ < maxRounds) {
            size_t prevLen = history_.size();
            repairStep();
            if (history_.size() == prevLen) {
                break;
            }
        }
        return history_;
    }

    Metrics metrics() {
        if (!history_.empty()) {
            return history_.back().metrics;
        }
        if (protected_) {
            return computeCurrentMetrics();
        }
        Metrics empty;
        empty.originalBytes = image_.pixels.size();
        empty.totalDataBits = image_.pixels.size() * 8U;
        return empty;
    }

    vector<Snapshot>& history() { return history_; }
    TannerGraph& graph() { return graph_; }
    Encoder& encoder() { return encoder_; }
    Image& originalImage() { return image_; }
    Image& unprotectedDamagedImage() { return unprotectedDamagedImage_; }
    int totalBlocks() { return totalBlocks_; }
    int inspectedBlockIdx() { return inspectedBlockIdx_; }

    void setInspectedBlock(int blockIdx) {
        inspectedBlockIdx_ = (totalBlocks_ <= 0) ? 0 : clamp(blockIdx, 0, totalBlocks_ - 1);
    }

    bool isRepairFinished() { return peelingDone_ && bitFlipDone_; }
    int graphGirth() { return GraphAnalyzer(graph_).girth(); }
    bool graphIsConnected() { return GraphAnalyzer(graph_).isConnected(); }
    vector<int> checkColors() { return GraphAnalyzer(graph_).checkColors(); }
    vector<vector<int>> layerSchedule() { return GraphAnalyzer(graph_).layerSchedule(); }
};
