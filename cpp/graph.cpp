#pragma once

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <iostream>
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
    TannerGraph(int vars = 0, int checks = 0) {
        if (vars < 0 || checks < 0) {
            throw invalid_argument("TannerGraph dimensions must be non-negative");
        }
        n = vars;
        m = checks;
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
        for (int c = 0; c < rows; c++) {
            if ((int)H[c].size() != cols) {
                throw invalid_argument("Incidence matrix rows must all have the same width");
            }
            for (int v = 0; v < cols; v++) {
                if (H[c][v] != 0) {
                    g.addEdge(v, c);
                }
            }
        }
        return g;
    }

    static TannerGraph hamming74() {
        Matrix H = {
            {1, 1, 0, 1, 1, 0, 0},
            {1, 0, 1, 1, 0, 1, 0},
            {0, 1, 1, 1, 0, 0, 1}
        };
        return fromIncidenceMatrix(H);
    }

    void addEdge(int v, int c) {
        if (v < 0 || v >= n || c < 0 || c >= m) {
            throw out_of_range("Node index out of range in addEdge");
        }
        // Avoid duplicate edges so the graph stays a simple bipartite graph
        for (int existing : varAdj[v]) {
            if (existing == c) {
                return;
            }
        }
        varAdj[v].push_back(c);
        chkAdj[c].push_back(v);
    }

    Matrix incidenceMatrix() {
        Matrix H(m, vector<int>(n, 0));
        for (int c = 0; c < m; c++) {
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
        for (int v = 0; v < n; v++) {
            total += (int)varAdj[v].size();
        }
        return total;
    }

    vector<int>& varNeighbors(int v) { return varAdj.at(v); }
    vector<int>& chkNeighbors(int c) { return chkAdj.at(c); }

    bool isCheckSatisfied(int c, const Bits& bits) {
        if (c < 0 || c >= m) {
            throw out_of_range("Check index out of range");
        }
        if ((int)bits.size() != n) {
            throw invalid_argument("Bit vector length must equal numVars()");
        }
        int parity = 0;
        for (int v : chkAdj[c]) {
            if (bits[v] == BIT_UNKNOWN) {
                return false;
            }
            parity ^= (bits[v] & 1);
        }
        return parity == 0;
    }

    vector<int> failedChecks(const Bits& bits) {
        vector<int> failed;
        for (int c = 0; c < m; c++) {
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
        for (int c = 0; c < m; c++) {
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

    // Builds one combined adjacency list where:
    //   variable nodes are 0 .. n-1
    //   check nodes    are n .. n+m-1
    vector<vector<int>> buildCombinedGraph() {
        int n = g->numVars();
        int m = g->numChecks();
        vector<vector<int>> adj(n + m);
        for (int v = 0; v < n; v++) {
            for (int c : g->varNeighbors(v)) {
                int chkNode = n + c;
                adj[v].push_back(chkNode);
                adj[chkNode].push_back(v);
            }
        }
        return adj;
    }

public:
    GraphAnalyzer(TannerGraph& graph) {
        g = &graph;
    }

    // Finds shortest cycle length (girth) using BFS from each variable node.
    int girth() {
        int n = g->numVars();
        int m = g->numChecks();
        int total = n + m;
        if (n == 0 || m == 0) {
            return 0;
        }

        vector<vector<int>> adj = buildCombinedGraph();
        int best = INT_MAX;

        for (int start = 0; start < n; start++) {
            vector<int> dist(total, -1);
            vector<int> parent(total, -1);
            queue<int> q;

            dist[start] = 0;
            q.push(start);

            while (!q.empty()) {
                int u = q.front();
                q.pop();

                // No shorter cycle is possible deeper in this BFS tree
                if (best != INT_MAX && 2 * dist[u] + 2 >= best) {
                    break;
                }

                for (int w : adj[u]) {
                    if (dist[w] == -1) {
                        dist[w] = dist[u] + 1;
                        parent[w] = u;
                        q.push(w);
                    } else if (w != parent[u]) {
                        int cycleLen = dist[u] + dist[w] + 1;
                        if (cycleLen < best) {
                            best = cycleLen;
                        }
                    }
                }

                // 4 is the smallest possible cycle in any bipartite graph
                if (best == 4) {
                    return 4;
                }
            }
        }

        if (best == INT_MAX) {
            return 0;
        }
        return best;
    }

    // Checks if the whole Tanner graph is a single connected component using BFS.
    bool isConnected() {
        int n = g->numVars();
        int m = g->numChecks();
        int total = n + m;
        if (total <= 1) {
            return true;
        }

        vector<vector<int>> adj = buildCombinedGraph();
        vector<bool> visited(total, false);
        queue<int> q;

        visited[0] = true;
        q.push(0);
        int count = 0;

        while (!q.empty()) {
            int u = q.front();
            q.pop();
            count++;

            for (int w : adj[u]) {
                if (!visited[w]) {
                    visited[w] = true;
                    q.push(w);
                }
            }
        }
        return count == total;
    }

    // Iterative degree-1 peeling returning the remaining unrecoverable stopping set.
    vector<int> stoppingSet(vector<int> lostVars) {
        int n = g->numVars();
        int m = g->numChecks();

        vector<bool> isLost(n, false);
        for (int v : lostVars) {
            if (v >= 0 && v < n) {
                isLost[v] = true;
            }
        }

        vector<int> unknownCount(m, 0);
        queue<int> q;
        for (int c = 0; c < m; c++) {
            for (int v : g->chkNeighbors(c)) {
                if (isLost[v]) {
                    unknownCount[c]++;
                }
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

            isLost[target] = false;
            for (int c2 : g->varNeighbors(target)) {
                unknownCount[c2]--;
                if (unknownCount[c2] == 1) {
                    q.push(c2);
                }
            }
        }

        vector<int> remaining;
        for (int v = 0; v < n; v++) {
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

        // Step 1: Build conflict graph (edge between two checks if they share a variable)
        vector<vector<bool>> hasEdge(m, vector<bool>(m, false));
        vector<vector<int>> conflictAdj(m);
        for (int v = 0; v < n; v++) {
            vector<int>& chks = g->varNeighbors(v);
            for (int i = 0; i < (int)chks.size(); i++) {
                for (int j = i + 1; j < (int)chks.size(); j++) {
                    int c1 = chks[i];
                    int c2 = chks[j];
                    if (!hasEdge[c1][c2]) {
                        hasEdge[c1][c2] = true;
                        hasEdge[c2][c1] = true;
                        conflictAdj[c1].push_back(c2);
                        conflictAdj[c2].push_back(c1);
                    }
                }
            }
        }

        // Step 2: Sort check nodes by descending conflict degree
        // Using (-degree, checkIndex) so standard sort() puts largest degree first
        vector<pair<int, int>> degOrder;
        for (int c = 0; c < m; c++) {
            int deg = (int)conflictAdj[c].size();
            degOrder.push_back({-deg, c});
        }
        sort(degOrder.begin(), degOrder.end());

        // Step 3: Greedy coloring pass by pass (Welsh-Powell)
        vector<int> colors(m, -1);
        int colored = 0;
        for (int color = 0; colored < m; color++) {
            for (int i = 0; i < m; i++) {
                int c = degOrder[i].second;
                if (colors[c] != -1) {
                    continue;
                }
                bool clash = false;
                for (int nbr : conflictAdj[c]) {
                    if (colors[nbr] == color) {
                        clash = true;
                        break;
                    }
                }
                if (!clash) {
                    colors[c] = color;
                    colored++;
                }
            }
        }
        return colors;
    }

    // Groups check nodes by their Welsh-Powell color class.
    vector<vector<int>> layerSchedule() {
        vector<int> colors = checkColors();
        if (colors.empty()) {
            return {};
        }

        int maxColor = 0;
        for (int col : colors) {
            if (col > maxColor) {
                maxColor = col;
            }
        }

        vector<vector<int>> layers(maxColor + 1);
        for (int c = 0; c < (int)colors.size(); c++) {
            layers[colors[c]].push_back(c);
        }
        return layers;
    }
};

// Progressive Edge Growth (PEG) constructor maximizing girth via BFS.
class PEGBuilder {
    int seedVal = 42;

    // Picks a check node with the lowest current degree from candidates (random tie-break).
    int pickMinDegree(TannerGraph& g, vector<int>& candidates, mt19937& rng) {
        int minDeg = INT_MAX;
        for (int c : candidates) {
            if (g.checkDegree(c) < minDeg) {
                minDeg = g.checkDegree(c);
            }
        }
        vector<int> best;
        for (int c : candidates) {
            if (g.checkDegree(c) == minDeg) {
                best.push_back(c);
            }
        }
        int idx = rng() % best.size();
        return best[idx];
    }

public:
    PEGBuilder(int seed = 42) {
        seedVal = seed;
    }

    // BFS from variable node v returning distance to every check node (-1 if unreachable).
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
                    int chkNode = n + c;
                    if (dist[chkNode] == -1) {
                        dist[chkNode] = dist[u] + 1;
                        q.push(chkNode);
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

        vector<int> checkDist(m, -1);
        for (int c = 0; c < m; c++) {
            checkDist[c] = dist[n + c];
        }
        return checkDist;
    }

    TannerGraph build(int n, int m) {
        if (n <= 0 || m <= 0) {
            throw invalid_argument("PEGBuilder requires n > 0 and m > 0");
        }

        int varDeg = min(3, m);
        TannerGraph g(n, m);
        mt19937 rng((uint32_t)seedVal);

        vector<int> allChecks;
        for (int c = 0; c < m; c++) {
            allChecks.push_back(c);
        }

        for (int v = 0; v < n; v++) {
            for (int e = 0; e < varDeg; e++) {
                int chosen = -1;
                if (e == 0) {
                    chosen = pickMinDegree(g, allChecks, rng);
                } else {
                    vector<int> dist = bfsDepth(g, v);
                    vector<int> unreachable;
                    vector<int> farthest;
                    int maxDist = -1;

                    for (int c = 0; c < m; c++) {
                        int d = dist[c];
                        if (d == -1) {
                            unreachable.push_back(c);
                        } else if (d > 1) {
                            if (d > maxDist) {
                                maxDist = d;
                                farthest.clear();
                                farthest.push_back(c);
                            } else if (d == maxDist) {
                                farthest.push_back(c);
                            }
                        }
                    }

                    if (!unreachable.empty()) {
                        chosen = pickMinDegree(g, unreachable, rng);
                    } else {
                        chosen = pickMinDegree(g, farthest, rng);
                    }
                }
                g.addEdge(v, chosen);
            }
        }
        return g;
    }
};
