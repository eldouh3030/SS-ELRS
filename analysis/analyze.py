import pandas as pd, numpy as np
from scipy import stats
d=pd.read_csv('final_raw.csv')
M=["HASH","LL","DELAY","SSRR","NOSEM","SSELRS_S","SSELRS"]
def ms(x): 
    x=np.asarray(x); h=stats.t.ppf(0.975,len(x)-1)*x.std(ddof=1)/np.sqrt(len(x)); return x.mean(),x.std(ddof=1),h
print("== absolute mean ± sd (30 runs) ==")
for (w,n),g in d.groupby(['workload','n']):
    print(f"\n{w} n={n}")
    for m in M:
        s=g[g.method==m]
        print(f"  {m:9s} shuffle {s.shuffleMB.mean()/1024:8.2f}±{s.shuffleMB.std()/1024:6.2f} GB | comm {s.commCost.mean()/1024:8.1f}±{s.commCost.std()/1024:6.1f} GB·hop | loc {100*s.locality.mean():5.1f}±{100*s.locality.std():4.1f}% | mk {s.makespan.mean():6.2f}±{s.makespan.std():5.2f}s | thr {s.throughput.mean():6.2f}±{s.throughput.std():5.2f} | util {100*s.utilization.mean():5.1f}±{100*s.utilization.std():4.1f}% | sched {s.schedMs.mean():6.2f}±{s.schedMs.std():5.2f}ms")
print("\n== paired reduction of SSELRS vs baseline: mean ± sd [95% CI], wins/30 ==")
P=d.pivot_table(index=['workload','n','run'],columns='method',values=['shuffleMB','commCost','makespan','locality','throughput'])
rows=[]
for base in ['HASH','LL','DELAY']:
  for (w,n),g in P.groupby(level=[0,1]):
    r={'baseline':base,'workload':w,'n':n}
    for met in ['shuffleMB','commCost','makespan']:
        red=100*(g[(met,base)]-g[(met,'SSELRS')])/g[(met,base)]
        m_,s_,h=ms(red); r[met]=f"{m_:.1f} ± {s_:.1f} [{m_-h:.1f}, {m_+h:.1f}] ({(red>0).sum()}/30)"
    lp=100*(g[('locality','SSELRS')]-g[('locality',base)])
    m_,s_,h=ms(lp); r['locality_pp']=f"{m_:+.1f} ± {s_:.1f} pp"
    tr=g[('throughput','SSELRS')]/g[('throughput',base)]
    m_,s_,h=ms(tr); r['thr_ratio']=f"{m_:.2f}× ± {s_:.2f}"
    rows.append(r)
R=pd.DataFrame(rows); pd.set_option('display.width',250); pd.set_option('display.max_colwidth',60)
print(R.to_string(index=False))
R.to_csv('summary_paired.csv',index=False)
print("\n== ablation: SSELRS_S vs SSELRS makespan; SSRR vs SSELRS_S shuffle ==")
for (w,n),g in P.groupby(level=[0,1]):
    a=100*(g[('makespan','SSELRS_S')]-g[('makespan','SSELRS')])/g[('makespan','SSELRS_S')]
    b=100*(g[('shuffleMB','SSRR')]-g[('shuffleMB','SSELRS_S')])/g[('shuffleMB','SSRR')]
    c=100*(g[('makespan','SSRR')]-g[('makespan','SSELRS_S')])/g[('makespan','SSRR')]
    print(w,n,f"Alg3 makespan gain {a.mean():.1f}±{a.std():.1f}%  | Eq8 vs RR shuffle {b.mean():.1f}±{b.std():.1f}%, makespan {c.mean():.1f}±{c.std():.1f}%")
print("\n== clusters ==");print(d[d.method=='SSELRS'].groupby(['workload','n']).clusters.agg(['mean','min','max']))
t=pd.read_csv('final_theta.csv')
pt=t.pivot_table(index=['workload','theta','run'],columns='method',values=['shuffleMB','makespan']).reset_index()
pt['shred']=100*(pt[('shuffleMB','HASH')]-pt[('shuffleMB','SSELRS')])/pt[('shuffleMB','HASH')]
pt['mkred']=100*(pt[('makespan','HASH')]-pt[('makespan','SSELRS')])/pt[('makespan','HASH')]
cl=t[t.method=='SSELRS'].groupby(['workload','theta']).clusters.mean()
print("\n== theta sweep (n=500, 10 runs) ==")
print(pt.groupby(['workload','theta'])[['shred','mkred']].agg(['mean','std']).round(1).join(cl.round(1)).to_string())
wv=pd.read_csv('final_weights.csv')
pw=wv.pivot_table(index=['alpha','beta','gamma','run'],columns='method',values=['shuffleMB','makespan']).reset_index()
pw['shred']=100*(pw[('shuffleMB','HASH')]-pw[('shuffleMB','SSELRS')])/pw[('shuffleMB','HASH')]
pw['mkred']=100*(pw[('makespan','HASH')]-pw[('makespan','SSELRS')])/pw[('makespan','HASH')]
print("\n== weights (healthcare n=500, 10 runs) ==")
print(pw.groupby(['alpha','beta','gamma'])[['shred','mkred']].agg(['mean','std']).round(1).to_string())
o=pd.read_csv('final_overhead.csv')
o['pct']=100*(o.schedMs/1000)/o.makespanSec
print("\n== overhead ==");print(o.groupby('n')[['clusterMs','schedMs','makespanSec','pct']].agg(['mean','std']).round(3).to_string())
