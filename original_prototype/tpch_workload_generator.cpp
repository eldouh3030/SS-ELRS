/*
 SS-ELRS TPC-H Style Workload Generator - C++17
 ------------------------------------------------------------
 Purpose:
  Generate realistic TPC-H style SQL workloads for SS-ELRS evaluation.

 Compile:
   g++ -std=c++17 -O2 tpch_workload_generator.cpp -o tpch_workload_generator

 Run:
   ./tpch_workload_generator

 Output:
   tpch_queries_100.txt
   tpch_queries_500.txt
   tpch_queries_1000.txt
*/

#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace std;

string pick(const vector<string>& v, mt19937& rng) {
    uniform_int_distribution<int> dist(0, (int)v.size() - 1);
    return v[dist(rng)];
}

void generate_workload(const string& filename, int n) {
    mt19937 rng(42 + n);

    vector<string> regions = {"AFRICA", "AMERICA", "ASIA", "EUROPE", "MIDDLE EAST"};
    vector<string> marketSegments = {"AUTOMOBILE", "BUILDING", "FURNITURE", "MACHINERY", "HOUSEHOLD"};
    vector<string> priorities = {"1-URGENT", "2-HIGH", "3-MEDIUM", "4-NOT SPECIFIED", "5-LOW"};
    vector<string> shipModes = {"AIR", "RAIL", "SHIP", "TRUCK", "MAIL"};
    vector<string> partTypes = {"STANDARD", "ECONOMY", "PROMO", "LARGE", "SMALL"};

    ofstream out(filename);
    if (!out) {
        cerr << "Cannot create " << filename << "\n";
        return;
    }

    for (int i = 1; i <= n; ++i) {
        int pattern = i % 10;

        int nationKey = (i * 7) % 25;
        int partKey = (i * 13) % 20000;
        int discount = 1 + (i % 10);
        int quantity = 5 + (i % 45);
        int price = 500 + (i * 37) % 100000;

        if (pattern == 0) {
            out << "SELECT c.c_custkey, c.c_name, o.o_orderkey, o.o_totalprice "
                << "FROM customer c JOIN orders o ON c.c_custkey = o.o_custkey "
                << "WHERE c.c_mktsegment = '" << pick(marketSegments, rng)
                << "' AND o.o_totalprice > " << price << ";\n";
        } else if (pattern == 1) {
            out << "SELECT l.l_orderkey, l.l_partkey, l.l_quantity, l.l_discount "
                << "FROM lineitem l "
                << "WHERE l.l_quantity > " << quantity
                << " AND l.l_discount < " << discount << ";\n";
        } else if (pattern == 2) {
            out << "SELECT s.s_suppkey, s.s_name, ps.ps_partkey, ps.ps_supplycost "
                << "FROM supplier s JOIN partsupp ps ON s.s_suppkey = ps.ps_suppkey "
                << "WHERE s.s_nationkey = " << nationKey
                << " AND ps.ps_supplycost > " << price / 10 << ";\n";
        } else if (pattern == 3) {
            out << "SELECT o.o_orderpriority, COUNT(*) "
                << "FROM orders o JOIN lineitem l ON o.o_orderkey = l.l_orderkey "
                << "WHERE o.o_orderpriority = '" << pick(priorities, rng)
                << "' GROUP BY o.o_orderpriority;\n";
        } else if (pattern == 4) {
            out << "SELECT p.p_partkey, p.p_name, ps.ps_supplycost "
                << "FROM part p JOIN partsupp ps ON p.p_partkey = ps.ps_partkey "
                << "WHERE p.p_type LIKE '%" << pick(partTypes, rng)
                << "%' AND ps.ps_supplycost < " << price / 5 << ";\n";
        } else if (pattern == 5) {
            out << "SELECT n.n_name, r.r_name, c.c_custkey "
                << "FROM customer c JOIN nation n ON c.c_nationkey = n.n_nationkey "
                << "JOIN region r ON n.n_regionkey = r.r_regionkey "
                << "WHERE r.r_name = '" << pick(regions, rng) << "';\n";
        } else if (pattern == 6) {
            out << "SELECT l.l_shipmode, SUM(l.l_extendedprice) "
                << "FROM lineitem l "
                << "WHERE l.l_shipmode = '" << pick(shipModes, rng)
                << "' GROUP BY l.l_shipmode;\n";
        } else if (pattern == 7) {
            out << "SELECT s.s_suppkey, s.s_acctbal, n.n_name "
                << "FROM supplier s JOIN nation n ON s.s_nationkey = n.n_nationkey "
                << "WHERE s.s_acctbal > " << price / 2
                << " AND n.n_nationkey = " << nationKey << ";\n";
        } else if (pattern == 8) {
            out << "SELECT p.p_partkey, l.l_quantity, l.l_extendedprice "
                << "FROM part p JOIN lineitem l ON p.p_partkey = l.l_partkey "
                << "WHERE p.p_partkey = " << partKey
                << " AND l.l_quantity > " << quantity << ";\n";
        } else {
            out << "SELECT c.c_name, o.o_orderstatus, SUM(o.o_totalprice) "
                << "FROM customer c JOIN orders o ON c.c_custkey = o.o_custkey "
                << "WHERE c.c_nationkey = " << nationKey
                << " GROUP BY c.c_name, o.o_orderstatus;\n";
        }
    }

    out.close();
    cout << "Generated " << filename << " with " << n << " queries.\n";
}

int main() {
    generate_workload("tpch_queries_100.txt", 100);
    generate_workload("tpch_queries_500.txt", 500);
    generate_workload("tpch_queries_1000.txt", 1000);

    cout << "Done.\n";
    return 0;
}