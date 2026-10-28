# SS-ELRS — evaluation code, seeds and raw results

Companion repository of the revised manuscript
"Semantic Aware Locality Driven Resource Scheduling for Distributed Healthcare and IoMT Analytics"
(SS-ELRS: Semantic Similarity Enhanced Locality-Aware Resource Scheduling).

## Contents
- `src/ss_elrs_eval.cpp` — single-file C++17 evaluation harness (discrete-event simulator). It implements
  Algorithm 1 (TF-IDF + cosine + threshold-graph clustering), Algorithm 2 / Eq. (8) with the capacity
  constraint (Eqs. 8a–8b), Eq. (10) hop distance, Eq. (11) latency model, Algorithm 3 / Eq. (12) runtime
  adaptation, the baselines Hash, Least-loaded (FIFO) and Delay scheduling, and three ablations.
- `original_prototype/` — the initial prototype submitted with the first version of the manuscript.
- `results/` — raw per-run CSV files behind every table and figure of the paper.
- `analysis/` — Python scripts that compute all reported numbers and tables (`gen_numbers.py`) and draw the figures (`figs.py`).
- `figures/` — Figs. 2–5 of the revised manuscript.

## Build and run
```bash
g++ -std=c++17 -O2 src/ss_elrs_eval.cpp -o ss_elrs_eval        # or: cl /std:c++17 /O2 /EHsc ss_elrs_eval.cpp
./ss_elrs_eval 30 final main        # main comparison (Tables 4-7, Figs. 2-3)        -> final_raw.csv
./ss_elrs_eval 10 final theta       # threshold sensitivity (Fig. 4a)                 -> final_theta.csv
./ss_elrs_eval 10 final weights     # alpha/beta/gamma sensitivity (Section 4.5)      -> final_weights.csv
./ss_elrs_eval 10 final overhead    # scheduling time (Table 8, Fig. 4b)              -> final_overhead.csv
./ss_elrs_eval 10 final similarity  # similarity vs shared data (Section 4.6, Fig. 5) -> final_similarity*.csv
./ss_elrs_eval 10 final scale       # 4/8/16 nodes, 1000/4000 queries (Table 9)       -> final_scale.csv
python3 analysis/gen_numbers.py results gen          # all numbers and LaTeX tables (needs pandas, scipy)
cd results && python3 ../analysis/figs.py            # figures (needs matplotlib)
```

The results in `results/` were produced on an HP Pavilion 15 laptop (AMD Ryzen 5 4500U, 8 GB RAM, 64-bit Windows)
with MSVC 19.44 (Visual Studio 2022): `cl /std:c++17 /O2 /EHsc ss_elrs_eval.cpp`.
Each run is fully determined by its seed. With g++ the standard-library random distributions differ, so values
differ in the decimals but not in the conclusions; timing columns (`schedMs`) depend on the machine.

## Methods in the CSV files
HASH, LL (least-loaded FIFO), DELAY (delay scheduling, lookahead 50), SSRR (semantic clusters with round-robin
mapping = initial prototype), NOSEM (Eq. 8 + Algorithm 3 without semantic grouping), SSELRS_S (SS-ELRS without
Algorithm 3), SSELRS (full method).

## Scope
This is a discrete-event simulation with synthetic healthcare/IoMT and TPC-H-style workloads; queries are not
executed on a database. See Section 5.2 of the paper for the limitations.
