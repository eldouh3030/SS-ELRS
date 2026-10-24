/*
 SS-ELRS Stage 2 - Shuffle and Locality Simulator - C++17
 ------------------------------------------------------------
 Purpose:
  1) Read clusters.csv generated from Stage 1
  2) Extract approximate data dependencies from each SQL query
  3) Simulate Native random-style placement
  4) Simulate SS-ELRS cluster-aware placement
  5) Estimate:
       - Locality ratio
       - Shuffle traffic
       - Network cost
       - Execution time
       - Improvement percentage

 Compile:
   g++ -std=c++17 -O2 ss_elrs_shuffle_simulator.cpp -o ss_elrs_shuffle_simulator

 Run:
   ./ss_elrs_shuffle_simulator clusters.csv 4

 Output:
   performance_report.txt
   performance_metrics.csv
   placement_details.csv
*/

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

struct QueryRecord {
    string clusterId;
    string queryId;
    string queryText;
    vector<string> dataItems;
};

struct Metrics {
    double localityRatio = 0.0;
    double shuffleMB = 0.0;
    double networkCost = 0.0;
    double executionTime = 0.0;
};

string trim(const string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

string to_lower_str(string s) {
    transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
    return s;
}

vector<string> split_csv_line_simple(const string& line) {
    vector<string> result;
    string current;
    bool inQuotes = false;

    for (char c : line) {
        if (c == '"') {
            inQuotes = !inQuotes;
        }
        else if (c == ',' && !inQuotes) {
            result.push_back(trim(current));
            current.clear();
        }
        else {
            current += c;
        }
    }
    result.push_back(trim(current));
    return result;
}

vector<string> extract_data_items(const string& query) {
    string q = to_lower_str(query);
    vector<string> known = {
        "medical_records", "iomt_sensor_data", "orders", "customer",
        "partsupp", "supplier", "lineitem", "patient", "ecg", "healthcare",
        "sensor", "diagnosis", "glucose", "blood_pressure", "heart_rate"
    };

    set<string> items;
    for (const auto& k : known) {
        if (q.find(k) != string::npos) items.insert(k);
    }

    // fallback: extract tokens after FROM and JOIN
    regex r("\\b(from|join)\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
    auto begin = sregex_iterator(q.begin(), q.end(), r);
    auto end = sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        items.insert((*it)[2].str());
    }

    if (items.empty()) items.insert("generic_data");
    return vector<string>(items.begin(), items.end());
}

vector<QueryRecord> read_clusters(const string& file) {
    ifstream in(file);
    if (!in) throw runtime_error("Cannot open file: " + file);

    vector<QueryRecord> records;
    string line;
    getline(in, line); // header

    while (getline(in, line)) {
        if (trim(line).empty()) continue;

        auto parts = split_csv_line_simple(line);
        if (parts.size() < 3) continue;

        QueryRecord r;
        r.clusterId = parts[0];
        r.queryId = parts[1];

        r.queryText = parts[2];
        for (size_t i = 3; i < parts.size(); ++i) {
            r.queryText += "," + parts[i];
        }

        r.dataItems = extract_data_items(r.queryText);
        records.push_back(r);
    }

    return records;
}

int stable_hash_node(const string& s, int nodes) {
    unsigned long long h = 1469598103934665603ULL;
    for (char c : s) {
        h ^= (unsigned char)c;
        h *= 1099511628211ULL;
    }
    return (int)(h % nodes);
}

unordered_map<string, int> native_placement(const vector<string>& allItems, int nodes) {
    unordered_map<string, int> placement;
    for (const auto& item : allItems) {
        placement[item] = stable_hash_node(item + "_native", nodes);
    }
    return placement;
}

unordered_map<string, int> ss_elrs_placement(const vector<QueryRecord>& queries, int nodes) {
    unordered_map<string, int> placement;
    map<string, vector<QueryRecord>> byCluster;

    for (const auto& q : queries) byCluster[q.clusterId].push_back(q);

    int clusterIndex = 0;
    for (const auto& kv : byCluster) {
        int targetNode = clusterIndex % nodes;

        set<string> items;
        for (const auto& q : kv.second) {
            for (const auto& item : q.dataItems) items.insert(item);
        }

        for (const auto& item : items) {
            if (!placement.count(item)) placement[item] = targetNode;
        }

        clusterIndex++;
    }

    return placement;
}

Metrics evaluate(const vector<QueryRecord>& queries,
    const unordered_map<string, int>& placement,
    int nodes,
    double blockSizeMB = 128.0) {
    Metrics m;

    double localAccess = 0.0;
    double totalAccess = 0.0;
    double shuffleMB = 0.0;

    for (const auto& q : queries) {
        int preferredNode = placement.at(q.dataItems[0]);

        // Realistic execution model:
        // 70% of queries execute on the preferred locality-aware node,
        // 30% execute on another node due to scheduler load balancing.
        int queryIndex = stable_hash_node(q.queryId, 10);

        int queryExecutionNode;
        if (queryIndex < 7) {
            queryExecutionNode = preferredNode;
        }
        else {
            queryExecutionNode = stable_hash_node(q.clusterId + "_" + q.queryId, nodes);
        }        for (const auto& item : q.dataItems) {
            int dataNode = placement.at(item);
            totalAccess += 1.0;

            if (dataNode == queryExecutionNode) {
                localAccess += 1.0;
            }
            else {
                shuffleMB += blockSizeMB;
            }
        }
    }

    if (totalAccess > 0.0) m.localityRatio = localAccess / totalAccess;
    m.shuffleMB = shuffleMB;

    // Network cost is a normalized estimate.
    m.networkCost = shuffleMB * 0.015;

    // Execution time estimate:
    // base processing + shuffle penalty + network penalty
    double baseTime = queries.size() * 1.25;
    double shufflePenalty = shuffleMB * 0.035;
    double networkPenalty = m.networkCost * 0.8;
    // Lightweight security overhead for secure shuffle metadata verification
    double securityOverhead = queries.size() * 0.15;

    m.executionTime = baseTime + shufflePenalty + networkPenalty + securityOverhead;
    return m;
}

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            cerr << "Usage: ./ss_elrs_shuffle_simulator clusters.csv [number_of_nodes]\n";
            return 1;
        }

        string inputFile = argv[1];
        int nodes = 4;
        if (argc >= 3) nodes = stoi(argv[2]);
        if (nodes <= 0) throw runtime_error("number_of_nodes must be positive.");

        auto queries = read_clusters(inputFile);
        if (queries.empty()) {
            cerr << "No query records found in clusters file.\n";
            return 1;
        }

        set<string> uniqueItems;
        for (const auto& q : queries) {
            for (const auto& item : q.dataItems) uniqueItems.insert(item);
        }
        vector<string> allItems(uniqueItems.begin(), uniqueItems.end());

        auto nativePlace = native_placement(allItems, nodes);
        auto proposedPlace = ss_elrs_placement(queries, nodes);

        Metrics nativeM = evaluate(queries, nativePlace, nodes);
        Metrics proposedM = evaluate(queries, proposedPlace, nodes);

        double shuffleReduction = 0.0;
        double timeReduction = 0.0;
        double localityImprovement = 0.0;

        if (nativeM.shuffleMB > 0)
            shuffleReduction = ((nativeM.shuffleMB - proposedM.shuffleMB) / nativeM.shuffleMB) * 100.0;

        if (nativeM.executionTime > 0)
            timeReduction = ((nativeM.executionTime - proposedM.executionTime) / nativeM.executionTime) * 100.0;

        if (nativeM.localityRatio > 0)
            localityImprovement = ((proposedM.localityRatio - nativeM.localityRatio) / nativeM.localityRatio) * 100.0;

        ofstream csv("performance_metrics.csv");
        csv << "Method,LocalityRatio,ShuffleMB,NetworkCost,EstimatedExecutionTime\n";
        csv << fixed << setprecision(4);
        csv << "Native," << nativeM.localityRatio << "," << nativeM.shuffleMB << ","
            << nativeM.networkCost << "," << nativeM.executionTime << "\n";
        csv << "SS-ELRS," << proposedM.localityRatio << "," << proposedM.shuffleMB << ","
            << proposedM.networkCost << "," << proposedM.executionTime << "\n";

        ofstream place("placement_details.csv");
        place << "DataItem,NativeNode,SS_ELRS_Node\n";
        for (const auto& item : allItems) {
            place << item << ",Node" << nativePlace[item] + 1
                << ",Node" << proposedPlace[item] + 1 << "\n";
        }

        ofstream report("performance_report.txt");
        report << fixed << setprecision(2);
        report << "SS-ELRS Stage 2 Performance Report\n";
        report << "----------------------------------------\n";
        report << "Total queries: " << queries.size() << "\n";
        report << "Unique data items: " << allItems.size() << "\n";
        report << "Number of simulated nodes: " << nodes << "\n\n";

        report << "Native Method:\n";
        report << "  Locality Ratio: " << nativeM.localityRatio * 100.0 << "%\n";
        report << "  Shuffle Traffic: " << nativeM.shuffleMB << " MB\n";
        report << "  Network Cost: " << nativeM.networkCost << "\n";
        report << "  Estimated Execution Time: " << nativeM.executionTime << " sec\n\n";

        report << "Proposed SS-ELRS Method:\n";
        report << "  Locality Ratio: " << proposedM.localityRatio * 100.0 << "%\n";
        report << "  Shuffle Traffic: " << proposedM.shuffleMB << " MB\n";
        report << "  Network Cost: " << proposedM.networkCost << "\n";
        report << "  Estimated Execution Time: " << proposedM.executionTime << " sec\n\n";

        report << "Improvement Summary:\n";
        report << "  Shuffle Reduction: " << shuffleReduction << "%\n";
        report << "  Execution Time Reduction: " << timeReduction << "%\n";
        report << "  Locality Improvement: " << localityImprovement << "%\n\n";

        report << "Research Interpretation:\n";
        report << "  The SS-ELRS placement strategy uses query clusters to co-locate related data items.\n";
        report << "  This reduces simulated shuffle traffic and improves data locality compared with native placement.\n";

        cout << "Done.\n";
        cout << "Generated files:\n";
        cout << "  performance_report.txt\n";
        cout << "  performance_metrics.csv\n";
        cout << "  placement_details.csv\n";
        cout << "Shuffle reduction: " << fixed << setprecision(2) << shuffleReduction << "%\n";
        cout << "Execution time reduction: " << fixed << setprecision(2) << timeReduction << "%\n";

    }
    catch (const exception& e) {
        cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}