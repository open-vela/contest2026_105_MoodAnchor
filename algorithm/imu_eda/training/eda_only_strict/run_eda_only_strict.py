"""Independent EDA-only WESAD experiment; raw data -> model -> blind test."""
from __future__ import print_function
import json, os, pickle, sys
import numpy as np
from sklearn.linear_model import LogisticRegression
from sklearn.preprocessing import StandardScaler

ROOT=os.path.dirname(os.path.abspath(__file__))
RAW=os.path.join(os.path.dirname(os.path.dirname(ROOT)),"WESAD")
OUT=os.path.join(ROOT,"output")
SOURCE=["S2","S3","S4","S5","S6","S7","S8","S9"]
TARGET=["S10","S11","S13","S14","S15","S16","S17"]
EDA_FS,LABEL_FS,WINDOW,STEP,GUARD=4,700,5,1,5

def feat(x):
 x=np.asarray(x,float).reshape(-1); d=np.diff(x)
 return np.asarray([x.mean(),x.std(),x.max()-x.min(),(x[-1]-x[0])/WINDOW,np.sqrt(np.mean(x*x)),np.mean(np.abs(d)),np.std(d)],float)
def label(labels,t):
 a,b=int(t*LABEL_FS),int((t+WINDOW)*LABEL_FS); q=labels[a:b]
 if len(q)!=b-a:return None
 c=np.bincount(q.astype(int),minlength=5); z=int(np.argmax(c))
 return (0 if z==1 else 1) if z in (1,2) and c[z]/float(len(q))>=.95 else None
def read(s):
 with open(os.path.join(RAW,s,s+'.pkl'),'rb') as f:d=pickle.load(f,encoding='latin1')
 e=d['signal']['wrist']['EDA'].reshape(-1); y=d['label'].reshape(-1); duration=int(min(len(e)/EDA_FS,len(y)/LABEL_FS)); rows=[]
 for t in range(0,duration-WINDOW+1,STEP):
  z=label(y,t)
  if z is not None:
   f=feat(e[t*EDA_FS:(t+WINDOW)*EDA_FS])
   if np.isfinite(f).all():rows.append((t,z,f))
 return rows
def normalise(rows):
 start=min(t for t,y,x in rows if y==0); cal=[x for t,y,x in rows if y==0 and start<=t<start+120]
 if len(cal)<100:raise RuntimeError('insufficient initial baseline')
 cal=np.vstack(cal);center=np.median(cal,0);mad=np.median(np.abs(cal-center),0)*1.4826;std=cal.std(0);scale=np.where(mad>1e-6,mad,np.where(std>1e-6,std,1.))
 return [{"time":t,"y":y,"x":(x-center)/scale} for t,y,x in rows],{"baseline_start_second":int(start),"baseline_end_second":int(start+120),"baseline_windows":len(cal),"center":center.tolist(),"scale":scale.tolist()}
def arrays(rows):return np.asarray([r['y'] for r in rows]),np.vstack([r['x'] for r in rows])
def fit(x,y):
 sc=StandardScaler();z=sc.fit_transform(x);lr=LogisticRegression(class_weight='balanced',solver='liblinear',max_iter=1000,random_state=20260914).fit(z,y);return sc,lr
def prob(m,x):return m[1].predict_proba(m[0].transform(x))[:,1]
def metric(y,p,t):
 y=np.asarray(y);q=(np.asarray(p)>=t).astype(int);tp=int(((y==1)&(q==1)).sum());tn=int(((y==0)&(q==0)).sum());fp=int(((y==0)&(q==1)).sum());fn=int(((y==1)&(q==0)).sum());pr=tp/float(max(tp+fp,1));re=tp/float(max(tp+fn,1));sp=tn/float(max(tn+fp,1))
 return {'windows':int(len(y)),'accuracy':round((tp+tn)/float(max(len(y),1)),4),'balanced_accuracy':round((re+sp)/2,4),'precision':round(pr,4),'recall':round(re,4),'f1':round(2*pr*re/max(pr+re,1e-12),4),'tp':tp,'tn':tn,'fp':fp,'fn':fn}
def threshold(y,p):
 all=[];ok=[]
 for t in np.arange(.10,.951,.01):
  m=metric(y,p,t);row=(m['f1'],m['recall'],float(t),m);all.append(row)
  if m['precision']>=.90:ok.append(row)
 return max(ok if ok else all,key=lambda a:(a[0],a[1]))
def split(rows,cal_end):
 # First/last halves of each continuous labelled block; guard removes overlap.
 fb=[];blind=[]
 for y in (0,1):
  part=[r for r in rows if r['y']==y and r['time']>=cal_end+GUARD];runs=[];run=[];last=None
  for r in part:
   if last is not None and r['time']>last+STEP:
    if run:runs.append(run)
    run=[]
   run.append(r);last=r['time']
  if run:runs.append(run)
  for run in runs:
   mid=len(run)//2;fb+=run[:max(0,mid-GUARD)];blind+=run[min(len(run),mid+GUARD):]
 return sorted(fb,key=lambda r:r['time']),sorted(blind,key=lambda r:r['time'])
def main():
 os.makedirs(OUT,exist_ok=True);people={};cal={}
 for s in SOURCE+TARGET:
  print('Reading raw EDA',s);sys.stdout.flush();people[s],cal[s]=normalise(read(s))
 oy=[];op=[]
 for held in SOURCE:
  rows=sum([people[s] for s in SOURCE if s!=held],[]);y,x=arrays(rows);m=fit(x,y);hy,hx=arrays(people[held]);oy+=hy.tolist();op+=prob(m,hx).tolist()
 _,_,gt,oof=threshold(np.asarray(oy),np.asarray(op))
 y,x=arrays(sum([people[s] for s in SOURCE],[]));m=fit(x,y)
 result={'protocol_version':'eda_only_clean_v1_test_exclusive_time_blocks','raw_data_root':RAW,'source_subjects':SOURCE,'target_subjects':TARGET,'model':'robust personal baseline normalization + global class-balanced logistic regression','window':{'seconds':WINDOW,'step_seconds':STEP,'label_purity':.95,'guard_seconds':GUARD},'calibration':'first 120 seconds of first labelled baseline, excluded from feedback and blind test','global_threshold':round(gt,2),'global_threshold_oof':oof,'subjects':{}}
 totals={k:0 for k in ['tp','tn','fp','fn']}
 for s in TARGET:
  fb,bl=split(people[s],cal[s]['baseline_end_second']);fy,fx=arrays(fb);by,bx=arrays(bl);fp=prob(m,fx);bp=prob(m,bx);_,_,pt,fm=threshold(fy,fp);gm=metric(by,bp,gt);pm=metric(by,bp,pt)
  result['subjects'][s]={'calibration':cal[s],'feedback_windows':len(fy),'blind_windows':len(by),'personal_threshold':round(pt,2),'feedback_metrics_used_only_for_threshold':fm,'blind_global_threshold':gm,'blind_personal_threshold':pm}
  for k in totals:totals[k]+=pm[k]
  print('Evaluated',s);sys.stdout.flush()
 tp,tn,fp,fn=[totals[k] for k in ['tp','tn','fp','fn']];pr=tp/float(max(tp+fp,1));re=tp/float(max(tp+fn,1));sp=tn/float(max(tn+fp,1));result['aggregate_personalized_blind']={'windows':tp+tn+fp+fn,'accuracy':round((tp+tn)/float(tp+tn+fp+fn),4),'balanced_accuracy':round((re+sp)/2,4),'precision':round(pr,4),'recall':round(re,4),'f1':round(2*pr*re/max(pr+re,1e-12),4),'tp':tp,'tn':tn,'fp':fp,'fn':fn}
 np.savez_compressed(os.path.join(OUT,'global_frozen_eda_only.npz'),mean=m[0].mean_,scale=m[0].scale_,w=m[1].coef_[0],b=m[1].intercept_[0],threshold=gt)
 with open(os.path.join(OUT,'eda_only_results.json'),'w',encoding='utf8') as f:json.dump(result,f,ensure_ascii=False,indent=2)
 print(json.dumps(result['aggregate_personalized_blind'],ensure_ascii=False,indent=2))
if __name__=='__main__':main()
