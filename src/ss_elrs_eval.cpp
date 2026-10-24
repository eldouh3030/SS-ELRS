/*
 SS-ELRS Revised Evaluation Harness - C++17
 ------------------------------------------------------------------------
 Implements the complete SS-ELRS pipeline described in the revised manuscript
 and compares it with baseline schedulers over many randomized runs.

   Stage 1  Semantic representation  : SQL normalisation + TF-IDF vectors (Sec. 3.1)
   Stage 2  Semantic clustering      : cosine similarity, threshold graph,
                                       connected components (Algorithm 1)
   Stage 3  Locality-aware placement : Eq. (8) score with node capacity (Algorithm 2)
   Stage 4  Runtime adaptation       : monitoring + migration (Algorithm 3, Eq. 12)

 Simulated cluster model (see manuscript Sec. 4.1):
   - m nodes in racks (2 nodes per rack); hop distance 0 / 2 / 4 (Eq. 10)
   - heterogeneous nominal slots per node, known to all schedulers
   - one random straggler per run (slowdown unknown to planners)
   - tables split into partitions; one home node per partition (seeded hash)
   - per-node LRU cache for fetched remote partitions
   - query latency  = processing + communication + shuffle-sync  (Eq. 11)

 Methods:
   HASH        static hash placement (locality- and semantics-oblivious)
   LL          dynamic least-loaded / FIFO pull (load-balancing only)
   DELAY       dynamic locality-first pull, delay-scheduling style (no semantics)
   SSRR        semantic clusters mapped round-robin (initial prototype; ablation)
   SSELRS_S    semantic clusters + Eq.(8) + capacity, static (ablation: no Alg. 3)
   SSELRS      full SS-ELRS (Eq.(8) + capacity + runtime adaptation)

 Build : g++ -std=c++17 -O2 ss_elrs_eval.cpp -o ss_elrs_eval
 Run   : ./ss_elrs_eval [runs=30] [out_prefix=results]
*/

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <numeric>
#include <queue>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;

// ------------------------------------------------------------------ config
struct Config {
    int nodes = 4;
    int partitions = 16;          // partitions for large tables
    double procRateMBs = 400.0;   // per-slot scan rate
    double bwIntraRack = 1250.0;  // MB/s (10 GbE)
    double bwCrossRack = 300.0;   // MB/s (oversubscribed core)
    double fetchSyncSec = 0.010;  // per remote partition synchronisation (shuffle delay)
    double queryOverheadSec = 0.005;
    double cacheFrac = 0.15;      // per-node cache = cacheFrac * dataset size
    double noiseSigma = 0.20;     // lognormal noise on actual processing time
    // SS-ELRS parameters
    double theta = 0.35;          // semantic threshold (Eq. 6)
    double alpha = 0.5, beta = 0.3, gamma = 0.2;  // Eq. (8)
    double lambda = 0.05;         // minimum useful locality score
    double epsilon = 0.10;        // capacity slack
    int chunkQueries = 25;        // max queries per placement unit
    double tauImb = 0.15;         // imbalance trigger (Alg. 3)
    double tauComm = 0.25;        // comm-cost trigger (Eq. 12), normalised
    int monitorWindows = 20;      // monitoring windows per run
    int delayLookahead = 50;      // DELAY baseline lookahead
};

// ------------------------------------------------------------------ data model
struct Table { string name; double sizeMB; int parts; };
struct Partition { int table; int idx; double sizeMB; int home; };

struct Access { string table; int mode; };  // mode 0 window/range, 1 point, 2 scan

struct Query {
    int id;
    string sql;
    vector<int> parts;   // partition ids read
    double bytesMB = 0;
    int cluster = -1;
};

struct World {
    vector<Table> tables;
    unordered_map<string, int> tIndex;
    vector<Partition> parts;
    vector<vector<int>> tableParts;
    double totalMB = 0;
};

int hopDist(int a, int b) {        // Eq. (10) distance: network hops
    if (a == b) return 0;
    if (a / 2 == b / 2) return 2;  // same rack: node-ToR-node
    return 4;                      // cross rack: node-ToR-agg-ToR-node
}

// ------------------------------------------------------------------ workload generators
static string pick(const vector<string>& v, mt19937& r) {
    return v[uniform_int_distribution<int>(0, (int)v.size() - 1)(r)];
}

World buildWorld(bool healthcare, const Config& cfg, mt19937& rng) {
    World w;
    vector<pair<string, double>> base;
    if (healthcare)
        base = {{"iomt_sensor_data", 2048}, {"ecg_records", 1536}, {"medical_records", 1024},
                {"vital_signs", 768}, {"patients", 128}, {"iomt_devices", 32}};
    else  // TPC-H scale factor 1 (approximate table sizes)
        base = {{"lineitem", 760}, {"orders", 170}, {"partsupp", 120}, {"part", 24},
                {"customer", 24}, {"supplier", 1.4}, {"nation", 0.01}, {"region", 0.01}};
    uniform_real_distribution<double> jit(0.8, 1.2);
    uniform_int_distribution<int> nodeD(0, cfg.nodes - 1);
    for (auto& b : base) {
        Table t{b.first, b.second * jit(rng), b.second >= 100 ? cfg.partitions : 1};
        w.tIndex[t.name] = (int)w.tables.size();
        w.tables.push_back(t);
        w.tableParts.push_back({});
        for (int i = 0; i < t.parts; ++i) {
            Partition p{(int)w.tables.size() - 1, i, t.sizeMB / t.parts, nodeD(rng)};
            w.tableParts.back().push_back((int)w.parts.size());
            w.parts.push_back(p);
        }
        w.totalMB += t.sizeMB;
    }
    return w;
}

void addAccess(Query& q, const World& w, const Access& a, mt19937& rng) {
    int t = w.tIndex.at(a.table);
    const auto& tp = w.tableParts[t];
    int P = (int)tp.size();
    if (a.mode == 2 || P == 1) {
        for (int p : tp) q.parts.push_back(p);
    } else if (a.mode == 1) {
        q.parts.push_back(tp[uniform_int_distribution<int>(0, P - 1)(rng)]);
    } else {
        int k = uniform_int_distribution<int>(1, 6)(rng);
        int end = P - 1 - uniform_int_distribution<int>(0, 3)(rng);  // recent window
        for (int i = max(0, end - k + 1); i <= end; ++i) q.parts.push_back(tp[i]);
    }
}

vector<Query> genHealthcare(int n, const World& w, mt19937& rng) {
    vector<string> diagnoses = {"diabetes", "hypertension", "arrhythmia", "cardiac_risk", "respiratory_distress"};
    vector<string> devices = {"ecg_monitor", "heart_rate_sensor", "glucose_sensor", "blood_pressure_sensor", "oxygen_sensor"};
    vector<string> alerts = {"LOW", "MEDIUM", "HIGH", "CRITICAL"};
    vector<string> depts = {"cardiology", "endocrinology", "emergency", "icu", "remote_monitoring"};
    uniform_int_distribution<int> patD(0, 11);
    vector<Query> Q;
    for (int i = 0; i < n; ++i) {
        Query q; q.id = i;
        int pat = patD(rng);
        int hr = uniform_int_distribution<int>(60, 150)(rng), gl = uniform_int_distribution<int>(70, 290)(rng);
        int sys = uniform_int_distribution<int>(90, 180)(rng), ox = uniform_int_distribution<int>(82, 99)(rng);
        int risk = uniform_int_distribution<int>(0, 99)(rng), sid = uniform_int_distribution<int>(500, 10500)(rng);
        ostringstream s;
        vector<Access> acc;
        switch (pat) {
        case 0: s << "SELECT patient_id, diagnosis, glucose_level, insulin_status FROM medical_records WHERE diagnosis = 'diabetes' AND glucose_level > " << gl << ";"; acc = {{"medical_records", 0}}; break;
        case 1: s << "SELECT patient_id, blood_pressure, diagnosis, risk_score FROM medical_records WHERE diagnosis = 'hypertension' AND blood_pressure > " << sys << ";"; acc = {{"medical_records", 0}}; break;
        case 2: s << "SELECT sensor_id, patient_id, ecg_signal, heart_rate FROM iomt_sensor_data WHERE heart_rate > " << hr << " AND ecg_signal IS NOT NULL;"; acc = {{"iomt_sensor_data", 0}}; break;
        case 3: s << "SELECT patient_id, heart_rate, cardiac_status FROM ecg_records WHERE diagnosis = 'arrhythmia' AND heart_rate > " << hr << ";"; acc = {{"ecg_records", 0}}; break;
        case 4: s << "SELECT patient_id, oxygen_saturation, alert_level FROM vital_signs WHERE oxygen_saturation < " << ox << " AND alert_level = '" << pick(alerts, rng) << "';"; acc = {{"vital_signs", 0}}; break;
        case 5: s << "SELECT p.patient_id, p.department, m.diagnosis, m.risk_score FROM patients p JOIN medical_records m ON p.patient_id = m.patient_id WHERE p.department = '" << pick(depts, rng) << "' AND m.risk_score > " << risk << ";"; acc = {{"patients", 2}, {"medical_records", 0}}; break;
        case 6: s << "SELECT d.device_id, d.device_type, s.heart_rate, s.ecg_signal FROM iomt_devices d JOIN iomt_sensor_data s ON d.device_id = s.sensor_id WHERE d.device_type = '" << pick(devices, rng) << "' AND s.heart_rate > " << hr << ";"; acc = {{"iomt_devices", 2}, {"iomt_sensor_data", 0}}; break;
        case 7: s << "SELECT patient_id, glucose_level, blood_pressure, risk_score FROM medical_records WHERE glucose_level > " << gl << " AND blood_pressure > " << sys << ";"; acc = {{"medical_records", 0}}; break;
        case 8: s << "SELECT patient_id, ecg_signal, heart_rate, alert_level FROM ecg_records WHERE heart_rate > " << hr << " AND alert_level = 'CRITICAL';"; acc = {{"ecg_records", 0}}; break;
        case 9: s << "SELECT sensor_id, patient_id, monitoring_status, timestamp_metadata FROM iomt_sensor_data WHERE sensor_id = " << sid << " AND monitoring_status = 'ACTIVE';"; acc = {{"iomt_sensor_data", 1}}; break;
        case 10: s << "SELECT patient_id, diagnosis, oxygen_saturation, respiratory_rate FROM vital_signs WHERE diagnosis = 'respiratory_distress' AND oxygen_saturation < " << ox << ";"; acc = {{"vital_signs", 0}}; break;
        default: s << "SELECT p.patient_id, m.diagnosis, v.heart_rate, v.blood_pressure FROM patients p JOIN medical_records m ON p.patient_id = m.patient_id JOIN vital_signs v ON p.patient_id = v.patient_id WHERE m.diagnosis = '" << pick(diagnoses, rng) << "' AND v.heart_rate > " << hr << ";"; acc = {{"patients", 2}, {"medical_records", 0}, {"vital_signs", 0}}; break;
        }
        q.sql = s.str();
        for (auto& a : acc) addAccess(q, w, a, rng);
        sort(q.parts.begin(), q.parts.end());
        q.parts.erase(unique(q.parts.begin(), q.parts.end()), q.parts.end());
        for (int p : q.parts) q.bytesMB += w.parts[p].sizeMB;
        Q.push_back(q);
    }
    return Q;
}

vector<Query> genTpch(int n, const World& w, mt19937& rng) {
    vector<string> regions = {"AFRICA", "AMERICA", "ASIA", "EUROPE", "MIDDLE EAST"};
    vector<string> segs = {"AUTOMOBILE", "BUILDING", "FURNITURE", "MACHINERY", "HOUSEHOLD"};
    vector<string> prios = {"1-URGENT", "2-HIGH", "3-MEDIUM", "4-NOT SPECIFIED", "5-LOW"};
    vector<string> modes = {"AIR", "RAIL", "SHIP", "TRUCK", "MAIL"};
    vector<string> types = {"STANDARD", "ECONOMY", "PROMO", "LARGE", "SMALL"};
    uniform_int_distribution<int> patD(0, 9);
    vector<Query> Q;
    for (int i = 0; i < n; ++i) {
        Query q; q.id = i;
        int pat = patD(rng);
        int nk = uniform_int_distribution<int>(0, 24)(rng), pk = uniform_int_distribution<int>(0, 19999)(rng);
        int disc = uniform_int_distribution<int>(1, 10)(rng), qty = uniform_int_distribution<int>(5, 49)(rng);
        int price = uniform_int_distribution<int>(500, 100500)(rng);
        ostringstream s;
        vector<Access> acc;
        switch (pat) {
        case 0: s << "SELECT c.c_custkey, c.c_name, o.o_orderkey, o.o_totalprice FROM customer c JOIN orders o ON c.c_custkey = o.o_custkey WHERE c.c_mktsegment = '" << pick(segs, rng) << "' AND o.o_totalprice > " << price << ";"; acc = {{"customer", 2}, {"orders", 0}}; break;
        case 1: s << "SELECT l.l_orderkey, l.l_partkey, l.l_quantity, l.l_discount FROM lineitem l WHERE l.l_quantity > " << qty << " AND l.l_discount < " << disc << ";"; acc = {{"lineitem", 0}}; break;
        case 2: s << "SELECT s.s_suppkey, s.s_name, ps.ps_partkey, ps.ps_supplycost FROM supplier s JOIN partsupp ps ON s.s_suppkey = ps.ps_suppkey WHERE s.s_nationkey = " << nk << " AND ps.ps_supplycost > " << price / 10 << ";"; acc = {{"supplier", 2}, {"partsupp", 0}}; break;
        case 3: s << "SELECT o.o_orderpriority, COUNT(*) FROM orders o JOIN lineitem l ON o.o_orderkey = l.l_orderkey WHERE o.o_orderpriority = '" << pick(prios, rng) << "' GROUP BY o.o_orderpriority;"; acc = {{"orders", 0}, {"lineitem", 0}}; break;
        case 4: s << "SELECT p.p_partkey, p.p_name, ps.ps_supplycost FROM part p JOIN partsupp ps ON p.p_partkey = ps.ps_partkey WHERE p.p_type LIKE '%" << pick(types, rng) << "%' AND ps.ps_supplycost < " << price / 5 << ";"; acc = {{"part", 2}, {"partsupp", 0}}; break;
        case 5: s << "SELECT n.n_name, r.r_name, c.c_custkey FROM customer c JOIN nation n ON c.c_nationkey = n.n_nationkey JOIN region r ON n.n_regionkey = r.r_regionkey WHERE r.r_name = '" << pick(regions, rng) << "';"; acc = {{"customer", 2}, {"nation", 2}, {"region", 2}}; break;
        case 6: s << "SELECT l.l_shipmode, SUM(l.l_extendedprice) FROM lineitem l WHERE l.l_shipmode = '" << pick(modes, rng) << "' GROUP BY l.l_shipmode;"; acc = {{"lineitem", 0}}; break;
        case 7: s << "SELECT s.s_suppkey, s.s_acctbal, n.n_name FROM supplier s JOIN nation n ON s.s_nationkey = n.n_nationkey WHERE s.s_acctbal > " << price / 2 << " AND n.n_nationkey = " << nk << ";"; acc = {{"supplier", 2}, {"nation", 2}}; break;
        case 8: s << "SELECT p.p_partkey, l.l_quantity, l.l_extendedprice FROM part p JOIN lineitem l ON p.p_partkey = l.l_partkey WHERE p.p_partkey = " << pk << " AND l.l_quantity > " << qty << ";"; acc = {{"part", 2}, {"lineitem", 0}}; break;
        default: s << "SELECT c.c_name, o.o_orderstatus, SUM(o.o_totalprice) FROM customer c JOIN orders o ON c.c_custkey = o.o_custkey WHERE c.c_nationkey = " << nk << " GROUP BY c.c_name, o.o_orderstatus;"; acc = {{"customer", 2}, {"orders", 0}}; break;
        }
        q.sql = s.str();
        for (auto& a : acc) addAccess(q, w, a, rng);
        sort(q.parts.begin(), q.parts.end());
        q.parts.erase(unique(q.parts.begin(), q.parts.end()), q.parts.end());
        for (int p : q.parts) q.bytesMB += w.parts[p].sizeMB;
        Q.push_back(q);
    }
    return Q;
}

// ------------------------------------------------------------------ Stage 1-2: semantic clustering
static const unordered_set<string> STOP = {
    "select","from","where","and","or","join","inner","left","right","full","outer","on","as","group",
    "by","order","having","limit","offset","insert","into","update","delete","create","table","values",
    "set","distinct","count","sum","avg","min","max","is","null","not","in","between","like","case",
    "when","then","else","end","desc","asc","true","false"};

vector<string> tokenize(const string& sql) {
    string s = sql;
    transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
    static const regex strLit("'[^']*'"), numLit("\\b[0-9]+(\\.[0-9]+)?\\b");
    s = regex_replace(s, strLit, " str_value ");
    s = regex_replace(s, numLit, " num_value ");
    for (char& c : s) if (!(isalnum((unsigned char)c) || c == '_' || c == '.')) c = ' ';
    stringstream ss(s); string tok; vector<string> out;
    while (ss >> tok) {
        if (tok.size() < 2 || STOP.count(tok)) continue;
        if (tok.find('.') != string::npos) {
            out.push_back(tok);
            stringstream ps(tok); string part;
            while (getline(ps, part, '.')) if (part.size() >= 2 && !STOP.count(part)) out.push_back(part);
        } else out.push_back(tok);
    }
    return out;
}

struct ClusterResult { vector<int> label; int k = 0; double ms = 0; };

// TF-IDF + cosine + threshold graph connected components (Algorithm 1)
ClusterResult semanticCluster(vector<Query>& Q, double theta) {
    auto t0 = chrono::steady_clock::now();
    int n = (int)Q.size();
    unordered_map<string, int> vocab;
    vector<unordered_map<int, double>> tf(n);
    vector<int> df;
    for (int i = 0; i < n; ++i) {
        auto toks = tokenize(Q[i].sql);
        for (auto& t : toks) {
            auto it = vocab.find(t);
            int id = it == vocab.end() ? (vocab[t] = (int)df.size(), df.push_back(0), (int)df.size() - 1) : it->second;
            tf[i][id] += 1.0;
        }
        for (auto& kv : tf[i]) { kv.second /= max<size_t>(1, toks.size()); df[kv.first]++; }
    }
    // weighted vectors, L2-normalised
    vector<vector<pair<int, double>>> v(n);
    for (int i = 0; i < n; ++i) {
        double norm = 0;
        for (auto& kv : tf[i]) {
            double w = kv.second * (log((1.0 + n) / (1.0 + df[kv.first])) + 1.0);
            v[i].push_back({kv.first, w}); norm += w * w;
        }
        norm = sqrt(norm);
        for (auto& p : v[i]) p.second /= norm;
        sort(v[i].begin(), v[i].end());
    }
    auto cosine = [&](int a, int b) {
        double d = 0; size_t x = 0, y = 0;
        while (x < v[a].size() && y < v[b].size()) {
            if (v[a][x].first == v[b][y].first) d += v[a][x++].second * v[b][y++].second;
            else if (v[a][x].first < v[b][y].first) ++x; else ++y;
        }
        return d;
    };
    ClusterResult r; r.label.assign(n, -1);
    // pairwise similarity (O(n^2 d)) with threshold-graph BFS
    vector<vector<int>> adj(n);
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (cosine(i, j) >= theta) { adj[i].push_back(j); adj[j].push_back(i); }
    for (int i = 0; i < n; ++i) {
        if (r.label[i] != -1) continue;
        queue<int> q; q.push(i); r.label[i] = r.k;
        while (!q.empty()) { int u = q.front(); q.pop(); for (int x : adj[u]) if (r.label[x] == -1) { r.label[x] = r.k; q.push(x); } }
        r.k++;
    }
    for (int i = 0; i < n; ++i) Q[i].cluster = r.label[i];
    r.ms = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
    return r;
}

// ------------------------------------------------------------------ node & cache
struct LRU {
    double cap, used = 0;
    list<int> order; unordered_map<int, list<int>::iterator> pos;
    explicit LRU(double c = 0) : cap(c) {}
    bool has(int p) const { return pos.count(p) > 0; }
    void touch(int p) { auto it = pos.find(p); if (it != pos.end()) { order.splice(order.begin(), order, it->second); } }
    void insert(int p, double sz, const World& w) {
        if (has(p)) { touch(p); return; }
        if (sz > cap) return;
        while (used + sz > cap && !order.empty()) {
            int e = order.back(); order.pop_back(); pos.erase(e); used -= w.parts[e].sizeMB;
        }
        order.push_front(p); pos[p] = order.begin(); used += sz;
    }
};

struct Metrics {
    double shuffleMB = 0, commCost = 0, localMB = 0, totalMB = 0, makespan = 0, busy = 0;
    double schedMs = 0;   // wall-clock scheduling overhead (measured)
    int clusters = 0, migrations = 0;
    double locality() const { return totalMB > 0 ? localMB / totalMB : 0; }
};

struct Sim {
    const World& w; const Config& c; vector<double> slots, slow; vector<LRU> cache;
    mt19937 noiseRng; Metrics m;
    vector<double> commWin;   // per-node comm cost within monitoring window
    vector<double> estDone, actDone;  // for observed speed ratio
    Sim(const World& W, const Config& C, const vector<double>& S, const vector<double>& SL, unsigned seed)
        : w(W), c(C), slots(S), slow(SL), noiseRng(seed) {
        for (int j = 0; j < c.nodes; ++j) cache.emplace_back(c.cacheFrac * w.totalMB);
        commWin.assign(c.nodes, 0); estDone.assign(c.nodes, 0); actDone.assign(c.nodes, 0);
    }
    double estTime(const Query& q, int j) const { return (q.bytesMB / c.procRateMBs + c.queryOverheadSec) / slots[j]; }
    // execute query q on node j; returns actual duration (Eq. 11)
    double exec(const Query& q, int j) {
        double proc = q.bytesMB / c.procRateMBs + c.queryOverheadSec;
        double comm = 0, sync = 0;
        for (int p : q.parts) {
            double sz = w.parts[p].sizeMB; m.totalMB += sz;
            int h = w.parts[p].home;
            if (h == j) { m.localMB += sz; continue; }
            if (cache[j].has(p)) { cache[j].touch(p); m.localMB += sz; continue; }
            int d = hopDist(h, j);
            m.shuffleMB += sz; m.commCost += sz * d; commWin[j] += sz * d;
            comm += sz / (d == 2 ? c.bwIntraRack : c.bwCrossRack);
            sync += c.fetchSyncSec;
            cache[j].insert(p, sz, w);
        }
        lognormal_distribution<double> ln(0.0, c.noiseSigma);
        double t = (proc * ln(noiseRng) + comm + sync) * slow[j] / slots[j];
        estDone[j] += estTime(q, j); actDone[j] += t; m.busy += t;
        return t;
    }
};

// static execution of per-node queues
void runStatic(Sim& s, const vector<Query>& Q, const vector<vector<int>>& queues) {
    double mk = 0;
    for (int j = 0; j < s.c.nodes; ++j) {
        double t = 0;
        for (int qi : queues[j]) t += s.exec(Q[qi], j);
        mk = max(mk, t);
    }
    s.m.makespan = mk;
}

// dynamic pull from a global FIFO; pickFn chooses an index in the window
template <class F>
void runPull(Sim& s, const vector<Query>& Q, const vector<int>& arrival, int lookahead, F score) {
    deque<int> pending(arrival.begin(), arrival.end());
    using E = pair<double, int>;
    priority_queue<E, vector<E>, greater<E>> free;
    for (int j = 0; j < s.c.nodes; ++j) free.push({0.0, j});
    double mk = 0;
    while (!pending.empty()) {
        auto [t, j] = free.top(); free.pop();
        int best = 0; double bs = -1e18;
        int W = min<int>(lookahead, (int)pending.size());
        for (int k = 0; k < W; ++k) { double sc = score(Q[pending[k]], j); if (sc > bs + 1e-12) { bs = sc; best = k; } }
        int qi = pending[best]; pending.erase(pending.begin() + best);
        double d = s.exec(Q[qi], j);
        mk = max(mk, t + d);
        free.push({t + d, j});
    }
    s.m.makespan = mk;
}

// ------------------------------------------------------------------ Stage 3: Eq. (8) placement
struct Chunk { int cluster; vector<int> qs; vector<int> parts; double sizeMB = 0; double work = 0; };

double eq8Score(const Chunk& ch, int j, const World& w, const Config& c, const vector<set<int>>& predCache) {
    if (ch.sizeMB <= 0) return 0;
    double loc = 0, cac = 0, comm = 0;
    for (int p : ch.parts) {
        double sz = w.parts[p].sizeMB; int h = w.parts[p].home;
        if (h == j) loc += sz;
        else if (predCache[j].count(p)) cac += sz;
        else comm += sz * hopDist(h, j);
    }
    double Dlocal = loc / ch.sizeMB, Ccache = cac / ch.sizeMB, CommN = comm / (ch.sizeMB * 4.0);
    return c.alpha * Dlocal + c.beta * Ccache - c.gamma * CommN;
}

vector<Chunk> makeChunks(const vector<Query>& Q, int k, const World& w, const Config& c, const vector<int>& order, bool semantic = true) {
    vector<vector<int>> byC(k);
    for (int qi : order) byC[semantic ? Q[qi].cluster : 0].push_back(qi);
    vector<Chunk> out;
    for (int ci = 0; ci < k; ++ci) {
        auto& v = byC[ci];
        // order queries inside a cluster by their first partition so chunks share data
        if (semantic) stable_sort(v.begin(), v.end(), [&](int a, int b) { return Q[a].parts.front() < Q[b].parts.front(); });
        for (size_t s0 = 0; s0 < v.size(); s0 += c.chunkQueries) {
            Chunk ch; ch.cluster = ci; set<int> ps;
            for (size_t i = s0; i < min(v.size(), s0 + c.chunkQueries); ++i) {
                ch.qs.push_back(v[i]); for (int p : Q[v[i]].parts) ps.insert(p);
                ch.work += Q[v[i]].bytesMB / c.procRateMBs + c.queryOverheadSec;
            }
            ch.parts.assign(ps.begin(), ps.end());
            for (int p : ch.parts) ch.sizeMB += w.parts[p].sizeMB;
            out.push_back(ch);
        }
    }
    // largest clusters first (Algorithm 2 processes heavy clusters while capacity is available)
    vector<double> cw(k, 0); for (auto& ch : out) cw[ch.cluster] += ch.work;
    stable_sort(out.begin(), out.end(), [&](const Chunk& a, const Chunk& b) { return cw[a.cluster] > cw[b.cluster]; });
    return out;
}

// Algorithm 2: returns per-node ordered list of chunk indices
vector<vector<int>> placeChunks(const vector<Chunk>& ch, const World& w, const Config& c, const vector<double>& slots) {
    int m = c.nodes;
    double totalW = 0; for (auto& x : ch) totalW += x.work;
    double sumS = accumulate(slots.begin(), slots.end(), 0.0);
    vector<double> cap(m), load(m, 0);
    for (int j = 0; j < m; ++j) cap[j] = (1 + c.epsilon) * totalW * slots[j] / sumS;   // capacity in work units
    vector<set<int>> pred(m);
    vector<double> predUsed(m, 0);
    vector<vector<int>> plan(m);
    for (int i = 0; i < (int)ch.size(); ++i) {
        int best = -1; double bs = -1e18;
        for (int j = 0; j < m; ++j) {
            if (load[j] + ch[i].work > cap[j]) continue;            // capacity constraint
            double sc = eq8Score(ch[i], j, w, c, pred);
            if (sc > bs) { bs = sc; best = j; }
        }
        if (best == -1 || bs < c.lambda) {                          // no feasible / no useful locality
            double bt = 1e18; int bj = 0;
            for (int j = 0; j < m; ++j) { double t = (load[j] + ch[i].work) / slots[j]; if (t < bt) { bt = t; bj = j; } }
            if (best == -1) best = bj;
            else if (bs < c.lambda) best = bj;
        }
        plan[best].push_back(i); load[best] += ch[i].work;
        for (int p : ch[i].parts) if (w.parts[p].home != best && !pred[best].count(p)) {
            pred[best].insert(p); predUsed[best] += w.parts[p].sizeMB;
        }
        // bound predicted cache by capacity (drop oldest insertions approximately)
        if (predUsed[best] > c.cacheFrac * w.totalMB) {
            pred[best].clear(); predUsed[best] = 0;
            for (int p : ch[i].parts) if (w.parts[p].home != best) { pred[best].insert(p); predUsed[best] += w.parts[p].sizeMB; }
        }
    }
    return plan;
}

// Algorithm 3: event-driven execution with monitoring and migration
void runAdaptive(Sim& s, const vector<Query>& Q, const vector<Chunk>& ch, vector<vector<int>> plan) {
    int m = s.c.nodes;
    vector<deque<int>> chunkQ(m);           // pending chunks per node
    vector<deque<int>> cur(m);              // queries of current chunk
    for (int j = 0; j < m; ++j) for (int x : plan[j]) chunkQ[j].push_back(x);
    vector<double> tfree(m, 0);
    int total = (int)Q.size(), done = 0;
    int window = max(1, total / s.c.monitorWindows), nextMon = window;
    auto pendingWork = [&](int j) {
        double wk = 0;
        for (int qi : cur[j]) wk += s.estTime(Q[qi], j);
        for (int x : chunkQ[j]) wk += ch[x].work / s.slots[j];
        return wk;
    };
    auto ratio = [&](int j) { return s.estDone[j] > 0 ? s.actDone[j] / s.estDone[j] : 1.0; };
    double mk = 0;
    while (done < total) {
        // next node to finish its current query
        int j = -1; double bt = 1e18;
        for (int k = 0; k < m; ++k) {
            if (cur[k].empty() && chunkQ[k].empty()) continue;
            if (tfree[k] < bt) { bt = tfree[k]; j = k; }
        }
        if (j == -1) break;
        // idle-node redistribution (Alg. 3, lines 14-15): a node that becomes idle before
        // another node has started its pending chunks takes the best-scoring pending chunk.
        bool stole = false;
        for (int k = 0; k < m; ++k) {
            if (!(cur[k].empty() && chunkQ[k].empty()) || tfree[k] > bt) continue;
            int src = -1, cx = -1; double bs = -1e18;
            vector<set<int>> live(m);
            for (auto& kv : s.cache[k].pos) live[k].insert(kv.first);
            for (int t = 0; t < m; ++t) {
                if (t == k || chunkQ[t].empty() || (cur[t].empty() && chunkQ[t].size() < 2)) continue;
                if (tfree[t] + pendingWork(t) * ratio(t) <= bt + ch[chunkQ[t].back()].work / s.slots[k]) continue;
                for (size_t ii = 0; ii < chunkQ[t].size(); ++ii) {
                    int x = chunkQ[t][ii];
                    double sc = eq8Score(ch[x], k, s.w, s.c, live);
                    if (sc > bs) { bs = sc; src = t; cx = (int)ii; }
                }
            }
            if (src != -1) {
                int x = chunkQ[src][cx];
                chunkQ[src].erase(chunkQ[src].begin() + cx);
                chunkQ[k].push_back(x); s.m.migrations++;
                tfree[k] = max(tfree[k], bt); stole = true;
            }
        }
        if (stole) continue;
        if (cur[j].empty()) { int x = chunkQ[j].front(); chunkQ[j].pop_front(); for (int qi : ch[x].qs) cur[j].push_back(qi); }
        int qi = cur[j].front(); cur[j].pop_front();
        double d = s.exec(Q[qi], j);
        tfree[j] += d; mk = max(mk, tfree[j]); done++;
        if (done >= nextMon) {
            nextMon += window;
            double now = bt;
            // projected finish using observed speed ratio (monitoring)
            vector<double> proj(m);
            double mean = 0;
            for (int k = 0; k < m; ++k) { proj[k] = max(now, tfree[k]) + pendingWork(k) * ratio(k); mean += proj[k]; }
            mean /= m;
            // communication trigger (Eq. 12): normalised window comm cost
            double winTot = 0; for (double x : s.commWin) winTot += x;
            for (int k = 0; k < m; ++k) {
                double commShare = winTot > 0 ? s.commWin[k] / winTot : 0;
                bool commTrig = commShare > (1.0 / m) * (1 + s.c.tauComm) && chunkQ[k].size() > 1;
                bool imbTrig = proj[k] > (1 + s.c.tauImb) * mean && !chunkQ[k].empty();
                if (!commTrig && !imbTrig) continue;
                // move the last pending chunk to the best-scoring under-loaded node
                int x = chunkQ[k].back();
                vector<set<int>> live(m);
                for (int t = 0; t < m; ++t) for (auto& kv : s.cache[t].pos) live[t].insert(kv.first);
                int bj = -1; double bs = -1e18;
                for (int t = 0; t < m; ++t) {
                    if (t == k) continue;
                    double addT = ch[x].work / s.slots[t] * ratio(t);
                    if (proj[t] + addT > mean * (1 + s.c.tauImb * 0.5)) continue;
                    double sc = eq8Score(ch[x], t, s.w, s.c, live);
                    if (imbTrig || sc > eq8Score(ch[x], k, s.w, s.c, live)) if (sc > bs) { bs = sc; bj = t; }
                }
                if (bj != -1) {
                    chunkQ[k].pop_back(); chunkQ[bj].push_back(x); s.m.migrations++;
                    double wk = ch[x].work;
                    proj[k] -= wk / s.slots[k] * ratio(k); proj[bj] += wk / s.slots[bj] * ratio(bj);
                }
            }
            fill(s.commWin.begin(), s.commWin.end(), 0.0);
        }
    }
    s.m.makespan = mk;
}

// ------------------------------------------------------------------ experiment driver
struct RunOut { map<string, Metrics> by; int clusters; double clusterMs; };

RunOut runOnce(bool hc, int n, unsigned seed, const Config& c, const vector<string>& methods) {
    mt19937 rng(seed);
    World w = buildWorld(hc, c, rng);
    vector<Query> Q = hc ? genHealthcare(n, w, rng) : genTpch(n, w, rng);
    vector<double> slots(c.nodes);
    for (int j = 0; j < c.nodes; ++j) slots[j] = (j < c.nodes / 2) ? 8 : 4;   // heterogeneous capacity
    vector<double> slow(c.nodes, 1.0);
    slow[uniform_int_distribution<int>(0, c.nodes - 1)(rng)] = uniform_real_distribution<double>(1.0, 1.6)(rng);
    unsigned simSeed = rng();
    vector<int> arrival(n); iota(arrival.begin(), arrival.end(), 0);

    RunOut out;
    auto cr = semanticCluster(Q, c.theta);
    out.clusters = cr.k; out.clusterMs = cr.ms;

    for (auto& meth : methods) {
        Sim s(w, c, slots, slow, simSeed);
        if (meth == "HASH") {
            vector<vector<int>> qs(c.nodes);
            for (int i = 0; i < n; ++i) qs[(unsigned)(i * 2654435761u ^ seed) % c.nodes].push_back(i);
            runStatic(s, Q, qs);
        } else if (meth == "LL") {
            runPull(s, Q, arrival, 1, [](const Query&, int) { return 0.0; });
        } else if (meth == "DELAY") {
            runPull(s, Q, arrival, c.delayLookahead, [&](const Query& q, int j) {
                double loc = 0;
                for (int p : q.parts) if (w.parts[p].home == j || s.cache[j].has(p)) loc += w.parts[p].sizeMB;
                return q.bytesMB > 0 ? loc / q.bytesMB : 0.0;
            });
        } else if (meth == "SSRR") {
            vector<vector<int>> qs(c.nodes);
            for (int i = 0; i < n; ++i) qs[Q[i].cluster % c.nodes].push_back(i);
            s.m.schedMs = cr.ms; runStatic(s, Q, qs);
        } else {  // SSELRS_S / SSELRS
            auto t0 = chrono::steady_clock::now();
            bool sem = meth != "NOSEM";
            auto chunks = makeChunks(Q, sem ? cr.k : 1, w, c, arrival, sem);
            auto plan = placeChunks(chunks, w, c, slots);
            double placeMs = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
            s.m.schedMs = cr.ms + placeMs;
            if (!sem) s.m.schedMs = placeMs;
            if (meth == "SSELRS_S") {
                vector<vector<int>> qs(c.nodes);
                for (int j = 0; j < c.nodes; ++j) for (int x : plan[j]) for (int qi : chunks[x].qs) qs[j].push_back(qi);
                runStatic(s, Q, qs);
            } else {
                auto t1 = chrono::steady_clock::now();
                runAdaptive(s, Q, chunks, plan);
                // monitoring overhead is not separable from simulation here; scheduling ms excludes it
                (void)t1;
            }
        }
        s.m.clusters = cr.k;
        out.by[meth] = s.m;
    }
    return out;
}

int main(int argc, char** argv) {
    int runs = argc > 1 ? atoi(argv[1]) : 30;
    string pre = argc > 2 ? argv[2] : "results";
    string mode = argc > 3 ? argv[3] : "main";
    Config c;
    vector<string> methods = {"HASH", "LL", "DELAY", "SSRR", "NOSEM", "SSELRS_S", "SSELRS"};

    if (mode == "main") {
        ofstream f(pre + "_raw.csv");
        f << "workload,n,run,seed,method,clusters,shuffleMB,commCost,locality,makespan,throughput,utilization,schedMs,migrations\n";
        f << fixed << setprecision(6);
        for (int hc = 1; hc >= 0; --hc)
            for (int n : {100, 500, 1000})
                for (int r = 0; r < runs; ++r) {
                    unsigned seed = 1000003u * (unsigned)(r + 1) + (unsigned)n * 31u + (unsigned)hc;
                    auto o = runOnce(hc, n, seed, c, methods);
                    for (auto& kv : o.by) {
                        auto& m = kv.second;
                        double util = m.busy / (c.nodes * m.makespan);
                        f << (hc ? "healthcare" : "tpch") << "," << n << "," << r << "," << seed << "," << kv.first << ","
                          << o.clusters << "," << m.shuffleMB << "," << m.commCost << "," << m.locality() << ","
                          << m.makespan << "," << n / m.makespan << "," << util << "," << m.schedMs << "," << m.migrations << "\n";
                    }
                    cerr << "." << flush;
                }
        cerr << "\n";
    } else if (mode == "theta") {   // threshold sensitivity
        ofstream f(pre + "_theta.csv");
        f << "workload,n,run,theta,clusters,method,shuffleMB,makespan,commCost,locality\n";
        for (int hc = 1; hc >= 0; --hc)
            for (double th : {0.15, 0.20, 0.25, 0.30, 0.35, 0.40, 0.45, 0.50, 0.60, 0.70, 0.80})
                for (int r = 0; r < runs; ++r) {
                    Config cc = c; cc.theta = th;
                    unsigned seed = 7777u * (unsigned)(r + 1) + (unsigned)hc;
                    auto o = runOnce(hc, 500, seed, cc, {"HASH", "SSELRS"});
                    for (auto& kv : o.by)
                        f << (hc ? "healthcare" : "tpch") << ",500," << r << "," << th << "," << o.clusters << "," << kv.first << ","
                          << kv.second.shuffleMB << "," << kv.second.makespan << "," << kv.second.commCost << "," << kv.second.locality() << "\n";
                }
    } else if (mode == "weights") { // alpha/beta/gamma sensitivity
        ofstream f(pre + "_weights.csv");
        f << "alpha,beta,gamma,run,method,shuffleMB,makespan,commCost\n";
        vector<array<double, 3>> ws = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.5, 0.3, 0.2}, {0.4, 0.4, 0.2}, {0.34, 0.33, 0.33}, {0.6, 0.2, 0.2}, {0.3, 0.5, 0.2}};
        for (auto& wv : ws)
            for (int r = 0; r < runs; ++r) {
                Config cc = c; cc.alpha = wv[0]; cc.beta = wv[1]; cc.gamma = wv[2];
                unsigned seed = 4242u * (unsigned)(r + 1);
                auto o = runOnce(true, 500, seed, cc, {"HASH", "SSELRS"});
                for (auto& kv : o.by)
                    f << wv[0] << "," << wv[1] << "," << wv[2] << "," << r << "," << kv.first << ","
                      << kv.second.shuffleMB << "," << kv.second.makespan << "," << kv.second.commCost << "\n";
            }
    } else if (mode == "overhead") { // scaling of scheduling overhead
        ofstream f(pre + "_overhead.csv");
        f << "n,run,clusterMs,schedMs,makespanSec\n";
        for (int n : {100, 250, 500, 1000, 2000, 4000})
            for (int r = 0; r < runs; ++r) {
                unsigned seed = 999u * (unsigned)(r + 1) + n;
                auto o = runOnce(true, n, seed, c, {"SSELRS"});
                f << n << "," << r << "," << o.clusterMs << "," << o.by["SSELRS"].schedMs << "," << o.by["SSELRS"].makespan << "\n";
            }
    }
    return 0;
}
