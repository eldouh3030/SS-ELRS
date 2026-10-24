import pandas as pd
def red(df,keys):
    p=df.pivot_table(index=keys+['run'],columns='method',values=['shuffleMB','makespan'])
    p.columns=[f"{a}_{b}" for a,b in p.columns]; p=p.reset_index()
    p['shred']=100*(p.shuffleMB_HASH-p.shuffleMB_SSELRS)/p.shuffleMB_HASH
    p['mkred']=100*(p.makespan_HASH-p.makespan_SSELRS)/p.makespan_HASH
    return p.groupby(keys)[['shred','mkred']].agg(['mean','std']).round(1)
t=pd.read_csv('final_theta.csv')
r=red(t,['workload','theta']); r[('clusters','mean')]=t[t.method=='SSELRS'].groupby(['workload','theta']).clusters.mean().round(1)
print("theta sweep\n",r.to_string())
w=pd.read_csv('final_weights.csv'); print("\nweights\n",red(w,['alpha','beta','gamma']).to_string())
o=pd.read_csv('final_overhead.csv'); o['pct']=100*(o.schedMs/1000)/o.makespanSec
print("\noverhead\n",o.groupby('n')[['clusterMs','schedMs','makespanSec','pct']].agg(['mean','std']).round(3).to_string())
