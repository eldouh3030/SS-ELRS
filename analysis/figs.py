import pandas as pd, numpy as np, matplotlib
matplotlib.use('Agg'); import matplotlib.pyplot as plt
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':9,'axes.spines.top':False,'axes.spines.right':False,
 'axes.edgecolor':'#8a8a85','axes.labelcolor':'#2b2b29','xtick.color':'#55554f','ytick.color':'#55554f',
 'axes.grid':True,'grid.color':'#e6e5e0','grid.linewidth':0.6,'axes.axisbelow':True})
d=pd.read_csv('final_raw.csv')
M=['HASH','LL','DELAY','NOSEM','SSELRS']
L={'HASH':'Hash (static)','LL':'Least-loaded (FIFO)','DELAY':'Delay scheduling','NOSEM':'SS-ELRS w/o semantic grouping','SSELRS':'SS-ELRS'}
C={'SSELRS':'#2a78d6','HASH':'#eb6834','LL':'#1baf7a','DELAY':'#eda100','NOSEM':'#e87ba4'}
H={'SSELRS':'','HASH':'///','LL':'\\\\\\','DELAY':'...','NOSEM':'xx'}
def bars(metric,scale,ylabel,fname,title):
    fig,axs=plt.subplots(1,2,figsize=(7.2,2.9))
    for ax,(w,wt) in zip(axs,[('healthcare','(a) Healthcare/IoMT workloads'),('tpch','(b) TPC-H-style workloads')]):
        ns=[100,500,1000]; x=np.arange(3); bw=0.16
        for i,m in enumerate(M):
            g=d[(d.workload==w)&(d.method==m)].groupby('n')[metric]
            mu=g.mean().reindex(ns)/scale; sd=g.std().reindex(ns)/scale
            ax.bar(x+(i-2)*bw,mu,bw*0.88,yerr=sd,color=C[m],hatch=H[m],edgecolor='white',linewidth=0.4,
                   error_kw=dict(elinewidth=0.8,capsize=2,ecolor='#3a3a36'),label=L[m])
        ax.set_xticks(x); ax.set_xticklabels([f'{n} queries' for n in ns]); ax.set_title(wt,fontsize=9,loc='left')
        ax.set_ylabel(ylabel)
    h,l=axs[0].get_legend_handles_labels()
    fig.legend(h,l,loc='lower center',ncol=3,frameon=False,fontsize=8,bbox_to_anchor=(0.5,-0.02))
    fig.tight_layout(rect=(0,0.12,1,1)); fig.savefig(fname,dpi=300,bbox_inches='tight'); plt.close(fig)
bars('shuffleMB',1024,'Shuffle traffic (GB)','fig5_shuffle.png','')
bars('makespan',1,'Makespan (s)','fig6_makespan.png','')
# fig7: theta sensitivity + overhead
t=pd.read_csv('final_theta.csv'); o=pd.read_csv('final_overhead.csv')
fig,axs=plt.subplots(1,2,figsize=(7.2,2.7))
p=t.pivot_table(index=['workload','theta','run'],columns='method',values='shuffleMB').reset_index()
p['red']=100*(p.HASH-p.SSELRS)/p.HASH
cl=t[t.method=='SSELRS'].groupby(['workload','theta']).clusters.mean()
ax=axs[0]
for w,c,lab in [('healthcare','#2a78d6','Healthcare/IoMT'),('tpch','#eb6834','TPC-H-style')]:
    g=p[p.workload==w].groupby('theta').red; mu,sd=g.mean(),g.std()
    ax.plot(mu.index,mu.values,'-o',color=c,lw=2,ms=4,label=lab); ax.fill_between(mu.index,mu-sd,mu+sd,color=c,alpha=0.15,lw=0)
ax.axvline(0.35,color='#8a8a85',ls='--',lw=0.8); ax.text(0.36,5,'θ = 0.35',fontsize=8,color='#55554f')
ax.set_ylim(0,100); ax.set_xlabel('Semantic threshold θ'); ax.set_ylabel('Shuffle reduction vs Hash (%)')
ax.set_title('(a) Sensitivity to θ (500 queries, 10 runs)',fontsize=9,loc='left'); ax.legend(frameon=False,fontsize=8,loc='lower right')
ax=axs[1]; g=o.groupby('n').schedMs; mu,sd=g.mean(),g.std()
ax.errorbar(mu.index,mu.values,yerr=sd.values,fmt='-o',color='#2a78d6',lw=2,ms=4,capsize=2,label='Measured')
ax.set_xlabel('Number of queries n'); ax.set_ylabel('SS-ELRS scheduling time (ms)')
ax.set_title('(b) Scheduling overhead (wall-clock)',fontsize=9,loc='left')
fig.tight_layout(); fig.savefig('fig7_theta_overhead.png',dpi=300,bbox_inches='tight')
print('ok')
# fig: semantic similarity vs shared input data (optional)
import os
if os.path.exists('final_similarity_bins.csv'):
    b=pd.read_csv('final_similarity_bins.csv'); b=b[b.pairs>0]
    fig,ax=plt.subplots(figsize=(3.4,2.5))
    for w,c,lab in [('healthcare','#2a78d6','Healthcare/IoMT'),('tpch','#eb6834','TPC-H-style')]:
        g=b[b.workload==w].groupby('bin_lo').mean_overlap; mu,sd=g.mean(),g.std()
        x=mu.index+0.05
        ax.errorbar(x,mu.values,yerr=sd.values,fmt='-o',color=c,lw=2,ms=4,capsize=2,label=lab)
    ax.set_xlabel('Cosine similarity of query pair (bin centre)'); ax.set_ylabel('Shared input data\n(weighted Jaccard)')
    ax.set_xlim(0,1); ax.set_ylim(0,0.8); ax.legend(frameon=False,fontsize=8,loc='upper left')
    fig.tight_layout(); fig.savefig('fig8_similarity_overlap.png',dpi=300,bbox_inches='tight')
