r"""Generate every reported number from the raw CSVs.
Outputs: numbers.json (for the response letter), numbers.tex (\R{key} macros),
and LaTeX tables tab_*.tex. Usage: python3 gen_numbers.py <results_dir> <out_dir>"""
import sys, json, os
import numpy as np, pandas as pd
from scipy import stats

src = sys.argv[1] if len(sys.argv) > 1 else '.'
out = sys.argv[2] if len(sys.argv) > 2 else '.'
os.makedirs(out, exist_ok=True)
d = pd.read_csv(f'{src}/final_raw.csv')
th = pd.read_csv(f'{src}/final_theta.csv')
wt = pd.read_csv(f'{src}/final_weights.csv')
ov = pd.read_csv(f'{src}/final_overhead.csv')
W = {'healthcare': 'hc', 'tpch': 'tp'}
NS = [100, 500, 1000]
K = {}

def f1(x): return f'{x:.1f}'
def ci(x):
    x = np.asarray(x, float); h = stats.t.ppf(0.975, len(x) - 1) * x.std(ddof=1) / np.sqrt(len(x))
    return x.mean(), x.std(ddof=1), x.mean() - h, x.mean() + h

K['runs'] = str(d.run.nunique())
P = d.pivot_table(index=['workload', 'n', 'run'], columns='method',
                  values=['shuffleMB', 'commCost', 'makespan', 'locality', 'throughput', 'utilization', 'migrations'])
for w, wk in W.items():
    for n in NS:
        g = P.loc[(w, n)]
        for base in ['HASH', 'LL', 'DELAY', 'NOSEM']:
            b = base.lower()
            for met, mk in [('shuffleMB', 'shuf'), ('commCost', 'comm'), ('makespan', 'mk')]:
                red = 100 * (g[(met, base)] - g[(met, 'SSELRS')]) / g[(met, base)]
                m, s, lo, hi = ci(red)
                key = f'{wk}.{b}.{mk}.{n}'
                K[key] = f1(m); K[key + '.abs'] = f1(abs(m)); K[key + '.sd'] = f1(s)
                K[key + '.lo'] = f1(lo); K[key + '.hi'] = f1(hi); K[key + '.win'] = str(int((red > 0).sum()))
                K[key + '.sig'] = 'yes' if (lo > 0 or hi < 0) else 'no'
            lp = 100 * (g[('locality', 'SSELRS')] - g[('locality', base)])
            K[f'{wk}.{b}.loc.{n}'] = f'{lp.mean():+.1f}'; K[f'{wk}.{b}.loc.{n}.abs'] = f1(abs(lp.mean()))
            tr = g[('throughput', 'SSELRS')] / g[('throughput', base)]
            K[f'{wk}.{b}.thr.{n}'] = f'{tr.mean():.2f}'
        # ablations
        a = 100 * (g[('makespan', 'SSELRS_S')] - g[('makespan', 'SSELRS')]) / g[('makespan', 'SSELRS_S')]
        r = 100 * (g[('shuffleMB', 'SSRR')] - g[('shuffleMB', 'SSELRS_S')]) / g[('shuffleMB', 'SSRR')]
        K[f'{wk}.alg3.{n}'] = f1(a.mean()); K[f'{wk}.alg3.{n}.sd'] = f1(a.std())
        K[f'{wk}.eq8rr.{n}'] = f1(r.mean()); K[f'{wk}.eq8rr.{n}.sd'] = f1(r.std())
        K[f'{wk}.mig.{n}'] = f1(g[('migrations', 'SSELRS')].mean())
        for meth in ['HASH', 'LL', 'DELAY', 'NOSEM', 'SSELRS']:
            K[f'{wk}.util.{meth.lower()}.{n}'] = f1(100 * g[('utilization', meth)].mean())
        s = d[(d.workload == w) & (d.n == n) & (d.method == 'SSELRS')]
        K[f'{wk}.cv.shuf.{n}'] = f'{100 * s.shuffleMB.std() / s.shuffleMB.mean():.0f}'
        K[f'{wk}.cv.mk.{n}'] = f'{100 * s.makespan.std() / s.makespan.mean():.0f}'
    # ranges over n (rounded integers)
    for base in ['hash', 'll', 'delay', 'nosem']:
        for mk in ['shuf', 'comm', 'mk']:
            vals = [float(K[f'{wk}.{base}.{mk}.{n}']) for n in NS]
            K[f'{wk}.{base}.{mk}.min'] = f'{min(vals):.0f}'; K[f'{wk}.{base}.{mk}.max'] = f'{max(vals):.0f}'
            K[f'{wk}.{base}.{mk}.absmin'] = f'{min(map(abs, vals)):.0f}'; K[f'{wk}.{base}.{mk}.absmax'] = f'{max(map(abs, vals)):.0f}'
        lv = [float(K[f'{wk}.{base}.loc.{n}']) for n in NS]
        K[f'{wk}.{base}.loc.min'] = f'{min(lv):.1f}'; K[f'{wk}.{base}.loc.max'] = f'{max(lv):.1f}'
        tv = [float(K[f'{wk}.{base}.thr.{n}']) for n in NS]
        K[f'{wk}.{base}.thr.min'] = f'{min(tv):.2f}'; K[f'{wk}.{base}.thr.max'] = f'{max(tv):.2f}'
    for k in ['cv.shuf', 'cv.mk']:
        v = [int(K[f'{wk}.{k}.{n}']) for n in NS]; K[f'{wk}.{k}.min'] = str(min(v)); K[f'{wk}.{k}.max'] = str(max(v))
    sd = [float(K[f'{wk}.hash.shuf.{n}.sd']) for n in NS]
    K[f'{wk}.hash.shuf.sdmin'] = f1(min(sd)); K[f'{wk}.hash.shuf.sdmax'] = f1(max(sd))
    for key, src_keys in [('alg3', [500, 1000]), ('eq8rr', NS)]:
        v = [float(K[f'{wk}.{key}.{n}']) for n in src_keys]
        K[f'{wk}.{key}.min'] = f1(min(v)); K[f'{wk}.{key}.max'] = f1(max(v))
    K[f'{wk}.clusters'] = f'{d[(d.workload == w) & (d.method == "SSELRS")].clusters.mean():.0f}'
    cl = d[(d.workload == w) & (d.method == 'SSELRS')].clusters
    K[f'{wk}.clusters.min'] = str(cl.min()); K[f'{wk}.clusters.max'] = str(cl.max())
    for u in ['delay', 'sselrs', 'hash']:
        v = [float(K[f'{wk}.util.{u}.{n}']) for n in NS]; K[f'{wk}.util.{u}.min'] = f'{min(v):.0f}'; K[f'{wk}.util.{u}.max'] = f'{max(v):.0f}'

al = [float(K[f'{w}.alg3.{n}']) for w in ['hc', 'tp'] for n in [500, 1000]]
K['alg3.min'] = f'{min(al):.0f}'; K['alg3.max'] = f'{max(al):.0f}'
mg = [float(K[f'{w}.mig.{n}']) for w in ['hc', 'tp'] for n in [500, 1000]]
K['mig.min'] = f'{min(mg):.0f}'; K['mig.max'] = f'{max(mg):.0f}'

# theta sweep
pt = th.pivot_table(index=['workload', 'theta', 'run'], columns='method', values='shuffleMB').reset_index()
pt['red'] = 100 * (pt.HASH - pt.SSELRS) / pt.HASH
for w, wk in W.items():
    r = pt[pt.workload == w].groupby('theta').red.mean()
    K[f'{wk}.theta.min'] = f1(r.min()); K[f'{wk}.theta.max'] = f1(r.max())
    c = th[(th.workload == w) & (th.method == 'SSELRS')].groupby('theta').clusters.mean()
    K[f'{wk}.theta.cmin'] = f'{c.min():.0f}'; K[f'{wk}.theta.cmax'] = f'{c.max():.0f}'
    one = c[c <= 1.0001]
    K[f'{wk}.theta.onecluster'] = f'{one.index.max():.2f}' if len(one) else 'none'
K['theta.lo'] = f'{th.theta.min():.2f}'; K['theta.hi'] = f'{th.theta.max():.2f}'
# weights
pw = wt.pivot_table(index=['alpha', 'beta', 'gamma', 'run'], columns='method', values=['shuffleMB', 'makespan'])
pw.columns = [f'{a}_{b}' for a, b in pw.columns]; pw = pw.reset_index()
pw['s'] = 100 * (pw.shuffleMB_HASH - pw.shuffleMB_SSELRS) / pw.shuffleMB_HASH
pw['m'] = 100 * (pw.makespan_HASH - pw.makespan_SSELRS) / pw.makespan_HASH
gw = pw.groupby(['alpha', 'beta', 'gamma'])[['s', 'm']].mean()
K['w.shuf.min'] = f1(gw.s.min()); K['w.shuf.max'] = f1(gw.s.max()); K['w.mk.min'] = f1(gw.m.min()); K['w.mk.max'] = f1(gw.m.max())
best = gw.s.idxmax(); K['w.best'] = f'({best[0]:g}, {best[1]:g}, {best[2]:g})'
worst = gw.s.idxmin(); K['w.worst'] = f'({worst[0]:g}, {worst[1]:g}, {worst[2]:g})'
K['w.default.shuf'] = f1(gw.loc[(0.5, 0.3, 0.2)].s)
K['w.runs'] = str(wt.run.nunique()); K['theta.runs'] = str(th.run.nunique()); K['ovh.runs'] = str(ov.run.nunique())
# overhead
ov['pct'] = 100 * (ov.schedMs / 1000) / ov.makespanSec
go = ov.groupby('n')
for n, g in go:
    K[f'ovh.ms.{n}'] = f1(g.schedMs.mean()); K[f'ovh.pct.{n}'] = f'{g.pct.mean():.2f}'
K['ovh.ms.max'] = f'{go.schedMs.mean().iloc[-1]:.0f}'; K['ovh.nmax'] = str(ov.n.max())
K['ovh.pct.min'] = f'{go.pct.mean().min():.2f}'; K['ovh.pct.max'] = f'{go.pct.mean().max():.2f}'
K['ovh.ms.1000r'] = f'{go.schedMs.mean().loc[1000]:.0f}'


WL = {'healthcare': 'Healthcare/IoMT', 'tpch': 'TPC-H-style'}
def pm(m, s, p=1): return f'{m:.{p}f} $\\pm$ {s:.{p}f}'
# ---------------- significance (two-sided Wilcoxon signed-rank on per-run paired values)
from scipy.stats import wilcoxon
for base in ['HASH', 'LL', 'DELAY']:
    for met, mk in [('shuffleMB', 'shuf'), ('makespan', 'mk')]:
        ps = []
        for w in W:
            for n in NS:
                g = P.loc[(w, n)]
                a, b = g[(met, base)].values, g[(met, 'SSELRS')].values
                try: ps.append(wilcoxon(a, b).pvalue)
                except ValueError: ps.append(1.0)
                K[f'{W[w]}.{base.lower()}.{mk}.{n}.p'] = f'{ps[-1]:.1e}'
        K[f'wil.{base.lower()}.{mk}.pmax'] = f'{max(ps):.1e}'
K['wil.hash.pmax'] = f"{max(float(K['wil.hash.shuf.pmax']), float(K['wil.hash.mk.pmax'])):.0e}"

# ---------------- similarity vs data overlap (optional file)
import os
if os.path.exists(f'{src}/final_similarity.csv'):
    sm = pd.read_csv(f'{src}/final_similarity.csv')
    for w, wk in W.items():
        g = sm[sm.workload == w]
        K[f'sim.{wk}.spearman'] = f'{g.spearman.mean():.2f}'; K[f'sim.{wk}.spearman.sd'] = f'{g.spearman.std():.2f}'
        K[f'sim.{wk}.pearson'] = f'{g.pearson.mean():.2f}'
        K[f'sim.{wk}.within'] = f'{g.within_overlap.mean():.2f}'; K[f'sim.{wk}.between'] = f'{g.between_overlap.mean():.2f}'
        K[f'sim.{wk}.ratio'] = f'{(g.within_overlap / g.between_overlap).mean():.1f}'
    K['sim.runs'] = str(sm.run.nunique())
    K['sim.pairs'] = f'{int(sm.pairs.iloc[0]):,}'.replace(',', '{,}')
# ---------------- scale experiment (optional file)
if os.path.exists(f'{src}/final_scale.csv'):
    sc = pd.read_csv(f'{src}/final_scale.csv')
    Q2 = sc.pivot_table(index=['workload', 'nodes', 'n', 'run'], columns='method', values=['shuffleMB', 'makespan', 'utilization', 'schedMs'])
    rows = []; rng = {}
    for w in ['healthcare', 'tpch']:
        first = True
        for m in sorted(sc.nodes.unique()):
            for n in sorted(sc.n.unique()):
                g = Q2.loc[(w, m, n)]
                sh = 100 * (g[('shuffleMB', 'HASH')] - g[('shuffleMB', 'SSELRS')]) / g[('shuffleMB', 'HASH')]
                mh = 100 * (g[('makespan', 'HASH')] - g[('makespan', 'SSELRS')]) / g[('makespan', 'HASH')]
                sd = 100 * (g[('shuffleMB', 'DELAY')] - g[('shuffleMB', 'SSELRS')]) / g[('shuffleMB', 'DELAY')]
                md = 100 * (g[('makespan', 'DELAY')] - g[('makespan', 'SSELRS')]) / g[('makespan', 'DELAY')]
                ut = 100 * g[('utilization', 'SSELRS')]; ms = g[('schedMs', 'SSELRS')]
                wk = W[w]
                for key, val in [('shuf', sh), ('mk', mh), ('dshuf', sd), ('dmk', md)]:
                    K[f'sc.{wk}.{m}.{n}.{key}'] = f'{val.mean():.1f}'; K[f'sc.{wk}.{m}.{n}.{key}.abs'] = f'{abs(val.mean()):.1f}'
                    rng.setdefault((wk, key), []).append(val.mean())
                K[f'sc.{wk}.{m}.{n}.util'] = f'{ut.mean():.0f}'
                rows.append(' & '.join([WL[w] if first else '', str(m), str(n), pm(sh.mean(), sh.std()), pm(mh.mean(), mh.std()),
                                        pm(sd.mean(), sd.std()), pm(md.mean(), md.std()), f'{ut.mean():.0f}', f'{ms.mean():.0f}']) + r' \\')
                first = False
        if w == 'healthcare': rows.append(r'\midrule')
    open(f'{out}/tab_scale.tex', 'w').write('\n'.join(rows) + '\n')
    for (wk, key), v in rng.items():
        K[f'sc.{wk}.{key}.min'] = f'{min(v):.0f}'; K[f'sc.{wk}.{key}.max'] = f'{max(v):.0f}'
    K['sc.runs'] = str(sc.run.nunique()); K['sc.nmax'] = str(sc.n.max()); K['sc.mmax'] = str(sc.nodes.max())

json.dump(K, open(f'{out}/numbers.json', 'w'), indent=0, sort_keys=True)
with open(f'{out}/numbers.tex', 'w') as fh:
    fh.write('% generated by gen_numbers.py -- do not edit\n')
    for k, v in sorted(K.items()):
        fh.write(f'\\expandafter\\def\\csname r@{k}\\endcsname{{{v}}}\n')

# ---------------- LaTeX tables
WL = {'healthcare': 'Healthcare/IoMT', 'tpch': 'TPC-H-style'}
def pm(m, s, p=1): return f'{m:.{p}f} $\\pm$ {s:.{p}f}'
def reltab(base, fname):
    rows = []
    for w in ['healthcare', 'tpch']:
        for i, n in enumerate(NS):
            g = P.loc[(w, n)]; cells = [WL[w] if i == 0 else '', str(n)]
            for met in ['shuffleMB', 'commCost', 'makespan']:
                red = 100 * (g[(met, base)] - g[(met, 'SSELRS')]) / g[(met, base)]
                m, s, lo, hi = ci(red); cells.append(f'{m:.1f} $\\pm$ {s:.1f} [{lo:.1f}, {hi:.1f}]')
            lp = 100 * (g[('locality', 'SSELRS')] - g[('locality', base)]); cells.append(f'{lp.mean():+.1f}')
            tr = g[('throughput', 'SSELRS')] / g[('throughput', base)]; cells.append(f'{tr.mean():.2f}')
            red = g[('makespan', base)] - g[('makespan', 'SSELRS')]; cells.append(f'{int((red > 0).sum())}/{K["runs"]}')
            rows.append(' & '.join(cells) + r' \\')
        if w == 'healthcare': rows.append(r'\midrule')
    open(f'{out}/{fname}', 'w').write('\n'.join(rows) + '\n')
reltab('HASH', 'tab_hash.tex'); reltab('DELAY', 'tab_delay.tex'); reltab('LL', 'tab_ll.tex')
rows = []
for w in ['healthcare', 'tpch']:
    for i, n in enumerate(NS):
        g = P.loc[(w, n)]
        c = 100 * (g[('shuffleMB', 'NOSEM')] - g[('shuffleMB', 'SSELRS')]) / g[('shuffleMB', 'NOSEM')]
        r = 100 * (g[('shuffleMB', 'SSRR')] - g[('shuffleMB', 'SSELRS_S')]) / g[('shuffleMB', 'SSRR')]
        a = 100 * (g[('makespan', 'SSELRS_S')] - g[('makespan', 'SSELRS')]) / g[('makespan', 'SSELRS_S')]
        rows.append(' & '.join([WL[w] if i == 0 else '', str(n), pm(c.mean(), c.std()), pm(r.mean(), r.std()), pm(a.mean(), a.std())]) + r' \\')
    if w == 'healthcare': rows.append(r'\midrule')
open(f'{out}/tab_ablation.tex', 'w').write('\n'.join(rows) + '\n')
NAME = {'HASH': 'Hash', 'LL': 'Least-loaded', 'DELAY': 'Delay scheduling', 'NOSEM': 'SS-ELRS w/o grouping', 'SSELRS': r'\textbf{SS-ELRS}'}
rows = []
for w in ['healthcare', 'tpch']:
    for i, n in enumerate(NS):
        for j, meth in enumerate(['HASH', 'LL', 'DELAY', 'NOSEM', 'SSELRS']):
            s = d[(d.workload == w) & (d.n == n) & (d.method == meth)]
            lead = (WL[w] if (i == 0 and j == 0) else '')
            rows.append(' & '.join([lead, str(n) if j == 0 else '', NAME[meth],
                pm(s.shuffleMB.mean() / 1024, s.shuffleMB.std() / 1024), pm(s.commCost.mean() / 1024, s.commCost.std() / 1024),
                pm(100 * s.locality.mean(), 100 * s.locality.std()), pm(s.makespan.mean(), s.makespan.std()),
                pm(s.throughput.mean(), s.throughput.std()), pm(100 * s.utilization.mean(), 100 * s.utilization.std())]) + r' \\')
        if not (w == 'tpch' and n == 1000): rows.append(r'\midrule' if n == 1000 else r'\cmidrule(lr){2-9}')
open(f'{out}/tab_abs.tex', 'w').write('\n'.join(rows) + '\n')
rows = [f'{n} & {pm(g.schedMs.mean(), g.schedMs.std())} & {g.makespanSec.mean():.1f} & {g.pct.mean():.2f} \\\\' for n, g in go]
open(f'{out}/tab_overhead.tex', 'w').write('\n'.join(rows) + '\n')
print(len(K), 'numbers written')
