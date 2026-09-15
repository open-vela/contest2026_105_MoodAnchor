"""Evaluate a frozen BVP+EDA late-fusion model on independently held-out people.

No classifier fitting occurs here. Each person's labelled baseline is used only
as a stand-in for the short personal calibration session required at deployment.
"""
from __future__ import annotations

import argparse, gc, json, pickle
from pathlib import Path
import numpy as np

BVP_FS, EDA_FS, WINDOW, STEP = 64, 4, 5, 1


def bvp_peaks(x):
    smooth=np.convolve(x,np.ones(5)/5,mode="same")
    candidates=np.where((smooth[1:-1]>smooth[:-2])&(smooth[1:-1]>=smooth[2:]))[0]+1
    threshold, kept=np.percentile(smooth,65),[]
    for i in candidates:
        if smooth[i] < threshold: continue
        if not kept or i-kept[-1] >= 22: kept.append(i)
        elif smooth[i] > smooth[kept[-1]]: kept[-1]=i
    return np.asarray(kept)


def bvp_features(x):
    p=bvp_peaks(x)
    if len(p)<3: return None
    ibi=np.diff(p)/BVP_FS; hr=60/ibi
    rmssd=np.sqrt(np.mean(np.diff(ibi)**2)) if len(ibi)>1 else 0.0
    return np.array([x.mean(),x.std(),np.mean(x*x),hr.mean(),hr.std(),np.std(ibi),rmssd])


def eda_features(x):
    d=np.diff(x)*EDA_FS; detrended=x-np.linspace(x[0],x[-1],len(x)); level=max(np.std(d),1e-8)
    return np.array([x.mean(),x.std(),x.max()-x.min(),(x[-1]-x[0])/WINDOW,np.mean(np.abs(d)),np.std(d),np.sum(d>level)/max(len(d),1),np.std(detrended)])


def load(path):
    with path.open("rb") as f: obj=pickle.load(f,encoding="latin1")
    bvp=np.asarray(obj["signal"]["wrist"]["BVP"]).reshape(-1); eda=np.asarray(obj["signal"]["wrist"]["EDA"]).reshape(-1); labels=np.asarray(obj["label"]).reshape(-1)
    del obj; gc.collect(); return bvp,eda,labels


def label_for_window(labels,start_s):
    lo,hi=int(start_s*700),int((start_s+WINDOW)*700)
    y=labels[lo:max(hi,lo+1)]; values,counts=np.unique(y,return_counts=True)
    return int(values[np.argmax(counts)]) if counts.max()/len(y)>=.8 else None


def person_rows(path):
    bvp,eda,labels=load(path); rows=[]; duration=min(len(bvp)/BVP_FS,len(eda)/EDA_FS)
    for start_s in range(0,int(duration)-WINDOW+1,STEP):
        state=label_for_window(labels,start_s)
        if state not in (1,2): continue
        bf=bvp_features(bvp[start_s*BVP_FS:(start_s+WINDOW)*BVP_FS])
        if bf is not None: rows.append((state-1,bf,eda_features(eda[start_s*EDA_FS:(start_s+WINDOW)*EDA_FS])))
    return rows


def probability(w,b,mean,std,x):
    return 1/(1+np.exp(-np.clip(((x-mean)/std)@w+b,-30,30)))


def evaluate(model, rows):
    base_b=np.vstack([r[1] for r in rows if r[0]==0]); base_e=np.vstack([r[2] for r in rows if r[0]==0])
    cb,sb=np.median(base_b,0),np.std(base_b,0)+1e-6; ce,se=np.median(base_e,0),np.std(base_e,0)+1e-6
    y=np.array([r[0] for r in rows]); xb=(np.vstack([r[1] for r in rows])-cb)/sb; xe=(np.vstack([r[2] for r in rows])-ce)/se
    pb=probability(model["bvp_w"],model["bvp_b"],model["bvp_mean"],model["bvp_std"],xb)
    pe=probability(model["eda_w"],model["eda_b"],model["eda_mean"],model["eda_std"],xe)
    pred=(float(model["bvp_weight"])*pb+float(model["eda_weight"])*pe>=float(model["threshold"])).astype(int)
    tp=int(((y==1)&(pred==1)).sum()); tn=int(((y==0)&(pred==0)).sum()); fp=int(((y==0)&(pred==1)).sum()); fn=int(((y==1)&(pred==0)).sum())
    return {"windows":len(y),"accuracy":round((tp+tn)/len(y),4),"precision":round(tp/max(tp+fp,1),4),"recall":round(tp/max(tp+fn,1),4),"f1":round(2*tp/max(2*tp+fp+fn,1),4),"tp":tp,"tn":tn,"fp":fp,"fn":fn}


def main():
    p=argparse.ArgumentParser(); p.add_argument("--wesad",type=Path,required=True); p.add_argument("--model",type=Path,required=True); p.add_argument("--subjects",default="S7,S8,S9,S10,S11"); p.add_argument("--output",type=Path,required=True); a=p.parse_args()
    frozen=np.load(a.model); subjects=a.subjects.split(","); result={"mode":"frozen-model inference; no classifier retraining","model":str(a.model),"fusion":{"eda_weight":float(frozen["eda_weight"]),"bvp_weight":float(frozen["bvp_weight"]),"threshold":float(frozen["threshold"])},"calibration_note":"Each evaluation subject's baseline-labelled windows are used only to simulate a personal baseline calibration session.","subjects":{}}
    for subject in subjects:
        path=a.wesad/subject/f"{subject}.pkl"
        if not path.exists(): raise SystemExit(f"Missing {path}")
        result["subjects"][subject]=evaluate(frozen,person_rows(path))
    a.output.mkdir(parents=True,exist_ok=True); (a.output/"frozen_model_test_results.json").write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding="utf8"); print(json.dumps(result,ensure_ascii=False,indent=2))


if __name__=="__main__": main()
