# SS-ELRS — revised evaluation (code, seeds, raw results)

Companion package for the revised manuscript
"Semantic Aware Locality Driven Resource Scheduling for Distributed Healthcare and IoMT Analytics"
(SS-ELRS: Semantic Similarity Enhanced Locality-Aware Resource Scheduling).

## Contents
- `src/ss_elrs_eval.cpp` — single-file C++17 evaluation harness. Implements Algorithm 1 (TF-IDF + cosine + threshold-graph clustering),
  Algorithm 2 / Eq. (8) with the capacity constraint (Eqs. 8a–8b), Eq. (10) hop distance, Eq. (11) latency model,
  Algorithm 3 / Eq. (12) runtime adaptation, and the baselines Hash, Least-loaded (FIFO) and Delay scheduling, plus ablations.
- `original_prototype/` — the initial prototype (query analyzer, simulator, workload generators) submitted with the first version.
- `results/` — raw per-run CSV files used for every table and figure.
- `analysis/` — Python scripts that compute the tables (mean ± SD, 95% CI) and draw the figures.
- `figures/` — Figs. 5–7 of the revised manuscript.

## Build and run
```bash
g++ -std=c++17 -O2 src/ss_elrs_eval.cpp -o ss_elrs_eval
./ss_elrs_eval 30 final main       # main comparison: 2 workloads x 3 sizes x 30 runs x 7 methods -> final_raw.csv
./ss_elrs_eval 10 final theta      # threshold sensitivity              -> final_theta.csv
./ss_elrs_eval 10 final weights    # alpha/beta/gamma sensitivity       -> final_weights.csv
./ss_elrs_eval 10 final overhead   # wall-clock scheduling time         -> final_overhead.csv
python3 analysis/gen_numbers.py results gen   # all numbers and LaTeX tables of the paper (needs pandas, scipy)
cd results && python3 ../analysis/figs.py      # Figs. 5-7 (needs matplotlib)
```
The results in results/ were produced on an HP Pavilion 15 (AMD Ryzen 5 4500U, 8 GB RAM, Windows 64-bit) with MSVC 19.44 (Visual Studio 2022), command: cl /std:c++17 /O2 /EHsc ss_elrs_eval.cpp. With g++ the random distributions differ slightly, so values differ in the decimals but not in the conclusions.
(seed of run r for size n and workload w: 1000003*(r+1) + 31*n + [w==healthcare]); timing columns (schedMs) vary with the machine.

## Methods in the CSV files
HASH, LL (least-loaded FIFO), DELAY (delay scheduling, lookahead 50), SSRR (semantic clusters, round-robin mapping = initial prototype),
NOSEM (Eq. 8 + Alg. 3 without semantic grouping), SSELRS_S (SS-ELRS without Alg. 3), SSELRS (full method).

## Scope
This is a discrete-event simulation with synthetic healthcare/IoMT and TPC-H-style workloads; queries are not executed on a database.
See Section 5.2 of the revised manuscript for the limitations.
