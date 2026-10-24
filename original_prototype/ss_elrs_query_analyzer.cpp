/*
 SS-ELRS Query Analyzer - C++17 Prototype
 ------------------------------------------------------------
 Purpose:
  1) Read SQL queries from queries.txt
  2) Normalize and tokenize SQL text
  3) Extract table/column/condition-like tokens
  4) Build TF-IDF vectors
  5) Compute cosine similarity matrix
  6) Cluster similar queries using threshold-based graph clustering

 Compile:
   g++ -std=c++17 -O2 ss_elrs_query_analyzer.cpp -o ss_elrs_query_analyzer

 Run:
   ./ss_elrs_query_analyzer queries.txt 0.35

 Output:
   similarity_matrix.csv
   clusters.csv
   cluster_report.txt
*/

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <queue>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;

static unordered_set<string> STOPWORDS = {
    "select","from","where","and","or","join","inner","left","right","full","outer",
    "on","as","group","by","order","having","limit","offset","insert","into","update",
    "delete","create","table","values","set","distinct","count","sum","avg","min","max",
    "is","null","not","in","between","like","case","when","then","else","end","desc","asc",
    "true","false"
};

string to_lower_str(string s) {
    transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)tolower(c); });
    return s;
}

string normalize_sql(string s) {
    s = to_lower_str(s);

    // Replace string literals with generic token
    s = regex_replace(s, regex("'[^']*'"), " str_value ");

    // Replace numeric literals with generic token
    s = regex_replace(s, regex("\\b[0-9]+(\\.[0-9]+)?\\b"), " num_value ");

    // Keep letters, digits, underscore, dot; convert others to spaces
    for (char &c : s) {
        if (!(isalnum((unsigned char)c) || c == '_' || c == '.')) c = ' ';
    }

    return s;
}

vector<string> tokenize_query(const string& query) {
    string norm = normalize_sql(query);
    stringstream ss(norm);
    string token;
    vector<string> tokens;

    while (ss >> token) {
        if (token.size() < 2) continue;
        if (STOPWORDS.count(token)) continue;

        // Split table.column into both full token and individual terms
        if (token.find('.') != string::npos) {
            tokens.push_back(token);
            string part;
            stringstream ps(token);
            while (getline(ps, part, '.')) {
                if (part.size() >= 2 && !STOPWORDS.count(part)) tokens.push_back(part);
            }
        } else {
            tokens.push_back(token);
        }
    }
    return tokens;
}

vector<string> read_queries(const string& file) {
    ifstream in(file);
    if (!in) {
        throw runtime_error("Cannot open input file: " + file);
    }

    vector<string> queries;
    string line, current;

    while (getline(in, line)) {
        if (line.empty()) continue;

        current += " " + line;

        // Query ends with semicolon; otherwise keep accumulating multi-line SQL
        if (line.find(';') != string::npos) {
            queries.push_back(current);
            current.clear();
        }
    }

    if (!current.empty()) queries.push_back(current);
    return queries;
}

unordered_map<string, double> build_tf(const vector<string>& tokens) {
    unordered_map<string, double> tf;
    for (const auto& t : tokens) tf[t]++;

    double total = max<size_t>(1, tokens.size());
    for (auto& kv : tf) kv.second /= total;
    return tf;
}

double cosine_similarity(const unordered_map<string, double>& a,
                         const unordered_map<string, double>& b) {
    if (a.empty() || b.empty()) return 0.0;

    const auto *small = &a, *large = &b;
    if (a.size() > b.size()) swap(small, large);

    double dot = 0.0;
    for (const auto& kv : *small) {
        auto it = large->find(kv.first);
        if (it != large->end()) dot += kv.second * it->second;
    }

    double normA = 0.0, normB = 0.0;
    for (const auto& kv : a) normA += kv.second * kv.second;
    for (const auto& kv : b) normB += kv.second * kv.second;

    if (normA == 0.0 || normB == 0.0) return 0.0;
    return dot / (sqrt(normA) * sqrt(normB));
}

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            cerr << "Usage: ./ss_elrs_query_analyzer queries.txt [similarity_threshold]\n";
            return 1;
        }

        string input_file = argv[1];
        double threshold = 0.35;
        if (argc >= 3) threshold = stod(argv[2]);

        vector<string> queries = read_queries(input_file);
        int n = (int)queries.size();

        if (n == 0) {
            cerr << "No queries found.\n";
            return 1;
        }

        vector<vector<string>> tokenized(n);
        vector<unordered_map<string, double>> tf(n);
        unordered_map<string, int> df;

        for (int i = 0; i < n; ++i) {
            tokenized[i] = tokenize_query(queries[i]);
            tf[i] = build_tf(tokenized[i]);

            unordered_set<string> unique_terms(tokenized[i].begin(), tokenized[i].end());
            for (const auto& t : unique_terms) df[t]++;
        }

        unordered_map<string, double> idf;
        for (const auto& kv : df) {
            idf[kv.first] = log((1.0 + n) / (1.0 + kv.second)) + 1.0;
        }

        vector<unordered_map<string, double>> tfidf(n);
        for (int i = 0; i < n; ++i) {
            for (const auto& kv : tf[i]) {
                tfidf[i][kv.first] = kv.second * idf[kv.first];
            }
        }

        vector<vector<double>> sim(n, vector<double>(n, 0.0));
        for (int i = 0; i < n; ++i) {
            sim[i][i] = 1.0;
            for (int j = i + 1; j < n; ++j) {
                sim[i][j] = sim[j][i] = cosine_similarity(tfidf[i], tfidf[j]);
            }
        }

        ofstream sm("similarity_matrix.csv");
        sm << "QueryID";
        for (int i = 0; i < n; ++i) sm << ",Q" << i + 1;
        sm << "\n";
        sm << fixed << setprecision(4);
        for (int i = 0; i < n; ++i) {
            sm << "Q" << i + 1;
            for (int j = 0; j < n; ++j) sm << "," << sim[i][j];
            sm << "\n";
        }

        // Threshold graph clustering using BFS
        vector<int> cluster_id(n, -1);
        int cid = 0;

        for (int i = 0; i < n; ++i) {
            if (cluster_id[i] != -1) continue;

            queue<int> q;
            q.push(i);
            cluster_id[i] = cid;

            while (!q.empty()) {
                int u = q.front();
                q.pop();

                for (int v = 0; v < n; ++v) {
                    if (cluster_id[v] == -1 && sim[u][v] >= threshold) {
                        cluster_id[v] = cid;
                        q.push(v);
                    }
                }
            }
            cid++;
        }

        map<int, vector<int>> clusters;
        for (int i = 0; i < n; ++i) clusters[cluster_id[i]].push_back(i);

        ofstream cf("clusters.csv");
        cf << "ClusterID,QueryID,QueryText\n";
        for (const auto& c : clusters) {
            for (int idx : c.second) {
                string clean = queries[idx];
                replace(clean.begin(), clean.end(), ',', ' ');
                cf << "C" << c.first + 1 << ",Q" << idx + 1 << "," << clean << "\n";
            }
        }

        ofstream report("cluster_report.txt");
        report << "SS-ELRS Query Analyzer Report\n";
        report << "----------------------------------------\n";
        report << "Total queries: " << n << "\n";
        report << "Similarity threshold: " << threshold << "\n";
        report << "Detected clusters: " << clusters.size() << "\n\n";

        for (const auto& c : clusters) {
            report << "Cluster C" << c.first + 1 << " contains " << c.second.size() << " queries:\n";
            for (int idx : c.second) {
                report << "  Q" << idx + 1 << ": " << queries[idx] << "\n";
            }
            report << "\n";
        }

        cout << "Done.\n";
        cout << "Generated files:\n";
        cout << "  similarity_matrix.csv\n";
        cout << "  clusters.csv\n";
        cout << "  cluster_report.txt\n";
        cout << "Clusters detected: " << clusters.size() << "\n";

    } catch (const exception& e) {
        cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}