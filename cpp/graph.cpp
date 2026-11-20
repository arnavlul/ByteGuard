#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

// Core graph types, bipartite Tanner graph, analyzer, and PEG builder.
const int BIT_UNKNOWN = -1;

using Bits = vector<int>;
using Matrix = vector<vector<int>>;

class TannerGraph {
    int n = 0;
    int m = 0;
    vector<vector<int>> varAdj;
    vector<vector<int>> chkAdj;

public:
    TannerGraph(int vars = 0, int checks = 0) : n(vars), m(checks) {
        if (n < 0 || m < 0) {
            throw invalid_argument("TannerGraph dimensions must be non-negative");
        }
        varAdj.resize(n);
        chkAdj.resize(m);
    }

    static TannerGraph fromIncidenceMatrix(Matrix H) {
        if (H.empty()) {
            return TannerGraph(0, 0);
        }
        int rows = (int)H.size();
        int cols = (int)H[0].size();
        TannerGraph g(cols, rows);
        for (int c = 0; c < rows; ++c) {
            if ((int)H[c].size() != cols) {
                throw invalid_argument("Incidence matrix rows must all have the same width");
            }
            for (int v = 0; v < cols; ++v) {
                if (H[c][v] != 0) {
                    g.addEdge(v, c);
                }
            }
        }
        return g;
    }

    static TannerGraph hamming74() {
        return fromIncidenceMatrix({
            {1, 1, 0, 1, 1, 0, 0},
            {1, 0, 1, 1, 0, 1, 0},
            {0, 1, 1, 1, 0, 0, 1},
        });
    }

    void addEdge(int v, int c) {
        auto& vn = varAdj.at(v);
        auto& cn = chkAdj.at(c);
        if (find(vn.begin(), vn.end(), c) == vn.end()) {
            vn.push_back(c);
            cn.push_back(v);
        }
    }

    Matrix incidenceMatrix() {
        Matrix H(m, vector<int>(n, 0));
        for (int c = 0; c < m; ++c) {
            for (int v : chkAdj[c]) {
                H[c][v] = 1;
            }
        }
        return H;
    }

    int degree(int v) { return (int)varAdj.at(v).size(); }
    int checkDegree(int c) { return (int)chkAdj.at(c).size(); }
    int numVars() { return n; }
    int numChecks() { return m; }

    int numEdges() {
        int total = 0;
        for (auto& nbrs : varAdj) {
            total += (int)nbrs.size();
        }
        return total;
    }

    vector<int>& varNeighbors(int v) { return varAdj.at(v); }
    vector<int>& chkNeighbors(int c) { return chkAdj.at(c); }

    bool isCheckSatisfied(int c, const Bits& bits) {
        auto& nbrs = chkAdj.at(c);
        if ((int)bits.size() != n) {
            throw invalid_argument("Bit vector length must equal numVars()");
        }
        int parity = 0;
        for (int v : nbrs) {
            if (bits[v] == BIT_UNKNOWN) {
                return false;
            }
            parity ^= (bits[v] & 1);
        }
        return parity == 0;
    }

    vector<int> failedChecks(const Bits& bits) {
        vector<int> failed;
        for (int c = 0; c < m; ++c) {
            if (!isCheckSatisfied(c, bits)) {
                failed.push_back(c);
            }
        }
        return failed;
    }

    bool isValidCodeword(const Bits& bits) {
        if ((int)bits.size() != n) {
            return false;
        }
        for (int c = 0; c < m; ++c) {
            if (!isCheckSatisfied(c, bits)) {
                return false;
            }
        }
        return true;
    }
};

// Graph analysis: BFS girth/connectivity, stopping sets, and Welsh-Powell coloring.
class GraphAnalyzer {
    TannerGraph* g;

    template <typename Fn>
    void forEachNeighbor(int u, Fn fn) {
        int n = g->numVars();
        if (u < n) {
            for (int c : g->varNeighbors(u)) {
                fn(n + c);
            }
        } else {
            for (int v : g->chkNeighbors(u - n)) {
                fn(v);
            }
        }
    }

public:
    GraphAnalyzer(TannerGraph& graph) : g(&graph) {}

    int girth() {
        int n = g->numVars();
        int m = g->numChecks();
        int total = n + m;
        if (n == 0 || m == 0) {
            return 0;
        }

        int INF = numeric_limits<int>::max();
        int best = INF;
        vector<int> dist(total);
        vector<int> parent(total);

        for (int startVar = 0; startVar < n; ++startVar) {
            fill(dist.begin(), dist.end(), -1);
            fill(parent.begin(), parent.end(), -1);
            queue<int> q;
            dist[startVar] = 0;
            q.push(startVar);

            while (!q.empty()) {
                int u = q.front();
                q.pop();
                if (best != INF && 2 * dist[u] + 2 >= best) {
                    break;
                }
                forEachNeighbor(u, [&](int w) {
                    if (dist[w] == -1) {
                        dist[w] = dist[u] + 1;
                        parent[w] = u;
                        q.push(w);
                    } else if (w != parent[u]) {
                        best = min(best, dist[u] + dist[w] + 1);
                    }
                });
                if (best == 4) {
                    return 4;
                }
            }
        }
        return (best == INF) ? 0 : best;
    }

    bool isConnected() {
        int total = g->numVars() + g->numChecks();
        if (total <= 1) {
            return true;
        }
        vector<int> visited(total, 0);
        queue<int> q;
        visited[0] = 1;
        q.push(0);
        int count = 0;

        while (!q.empty()) {
            int u = q.front();
            q.pop();
            ++count;
            forEachNeighbor(u, [&](int w) {
                if (!visited[w]) {
                    visited[w] = 1;
                    q.push(w);
                }
            });
        }
        return count == total;
    }

    // Iterative degree-1 peeling returning the remaining unrecoverable stopping set.
    vector<int> stoppingSet(vector<int> lostVars) {
        int n = g->numVars();
        int m = g->numChecks();
        vector<int> isLost(n, 0);
        for (int v : lostVars) {
            if (v >= 0 && v < n) {
                isLost[v] = 1;
            }
        }

        vector<int> unknownCount(m, 0);
        queue<int> q;
        for (int c = 0; c < m; ++c) {
            for (int v : g->chkNeighbors(c)) {
                unknownCount[c] += isLost[v];
            }
            if (unknownCount[c] == 1) {
                q.push(c);
            }
        }

        while (!q.empty()) {
            int c = q.front();
            q.pop();
            if (unknownCount[c] != 1) {
                continue;
            }
            int target = -1;
            for (int v : g->chkNeighbors(c)) {
                if (isLost[v]) {
                    target = v;
                    break;
                }
            }
            if (target == -1) {
                continue;
            }
            isLost[target] = 0;
            for (int c2 : g->varNeighbors(target)) {
                if (--unknownCount[c2] == 1) {
                    q.push(c2);
                }
            }
        }

        vector<int> remaining;
        for (int v = 0; v < n; ++v) {
            if (isLost[v]) {
                remaining.push_back(v);
            }
        }
        return remaining;
    }

    // Welsh-Powell greedy vertex coloring over check nodes sharing a variable bit.
    vector<int> checkColors() {
        int n = g->numVars();
        int m = g->numChecks();
        if (m == 0) {
            return {};
        }

        vector<vector<int>> conflictAdj(m);
        for (int v = 0; v < n; ++v) {
            auto& chks = g->varNeighbors(v);
            for (size_t i = 0; i < chks.size(); ++i) {
                for (size_t j = i + 1; j < chks.size(); ++j) {
                    conflictAdj[chks[i]].push_back(chks[j]);
                    conflictAdj[chks[j]].push_back(chks[i]);
                }
            }
        }
        for (auto& nbrs : conflictAdj) {
            sort(nbrs.begin(), nbrs.end());
            nbrs.erase(unique(nbrs.begin(), nbrs.end()), nbrs.end());
        }

        vector<int> order(m);
        iota(order.begin(), order.end(), 0);
        sort(order.begin(), order.end(), [&](int a, int b) {
            size_t da = conflictAdj[a].size();
            size_t db = conflictAdj[b].size();
            return (da != db) ? (da > db) : (a < b);
        });

        vector<int> colors(m, -1);
        int colored = 0;
        for (int color = 0; colored < m; ++color) {
            for (int c : order) {
                if (colors[c] != -1) {
                    continue;
                }
                bool clash = any_of(conflictAdj[c].begin(), conflictAdj[c].end(),
                                    [&](int nbr) { return colors[nbr] == color; });
                if (!clash) {
                    colors[c] = color;
                    ++colored;
                }
            }
        }
        return colors;
    }

    vector<vector<int>> layerSchedule() {
        vector<int> colors = checkColors();
        if (colors.empty()) {
            return {};
        }
        int numColors = *max_element(colors.begin(), colors.end()) + 1;
        vector<vector<int>> layers(numColors);
        for (int c = 0; c < (int)colors.size(); ++c) {
            layers[colors[c]].push_back(c);
        }
        return layers;
    }
};

// Progressive Edge Growth (PEG) constructor maximizing girth via BFS.
class PEGBuilder {
    int seedVal = 42;

public:
    PEGBuilder(int seed = 42) : seedVal(seed) {}

    vector<int> bfsDepth(TannerGraph& g, int v) {
        int n = g.numVars();
        int m = g.numChecks();
        if (v < 0 || v >= n) {
            throw out_of_range("Variable node index out of range in bfsDepth");
        }

        vector<int> dist(n + m, -1);
        queue<int> q;
        dist[v] = 0;
        q.push(v);

        while (!q.empty()) {
            int u = q.front();
            q.pop();
            if (u < n) {
                for (int c : g.varNeighbors(u)) {
                    if (dist[n + c] == -1) {
                        dist[n + c] = dist[u] + 1;
                        q.push(n + c);
                    }
                }
            } else {
                for (int w : g.chkNeighbors(u - n)) {
                    if (dist[w] == -1) {
                        dist[w] = dist[u] + 1;
                        q.push(w);
                    }
                }
            }
        }
        return {dist.begin() + n, dist.end()};
    }

    TannerGraph build(int n, int m) {
        if (n <= 0 || m <= 0) {
            throw invalid_argument("PEGBuilder requires n > 0 and m > 0");
        }

        int varDeg = min(3, m);
        TannerGraph g(n, m);
        mt19937 rng((uint32_t)seedVal);
        vector<int> allChecks(m);
        iota(allChecks.begin(), allChecks.end(), 0);

        auto pickMinDegree = [&](vector<int>& candidates) {
            int minDeg = numeric_limits<int>::max();
            for (int c : candidates) {
                minDeg = min(minDeg, g.checkDegree(c));
            }
            vector<int> best;
            for (int c : candidates) {
                if (g.checkDegree(c) == minDeg) {
                    best.push_back(c);
                }
            }
            uniform_int_distribution<size_t> pick(0, best.size() - 1);
            return best[pick(rng)];
        };

        for (int v = 0; v < n; ++v) {
            for (int e = 0; e < varDeg; ++e) {
                int chosen = -1;
                if (e == 0) {
                    chosen = pickMinDegree(allChecks);
                } else {
                    vector<int> dist = bfsDepth(g, v);
                    vector<int> unreachable;
                    vector<int> farthest;
                    int maxDist = -1;
                    for (int c = 0; c < m; ++c) {
                        int d = dist[c];
                        if (d == -1) {
                            unreachable.push_back(c);
                        } else if (d > 1) {
                            if (d > maxDist) {
                                maxDist = d;
                                farthest = {c};
                            } else if (d == maxDist) {
                                farthest.push_back(c);
                            }
                        }
                    }
                    chosen = pickMinDegree(unreachable.empty() ? farthest : unreachable);
                }
                g.addEdge(v, chosen);
            }
        }
        return g;
    }
};
