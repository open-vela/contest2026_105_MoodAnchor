"""Classic decision-level late fusion of independent wrist BVP and EDA models.

Each modality has its own calibrated logistic-regression classifier. Their
probability scores are fused only at the decision layer. Fusion weight and
threshold are selected from subject-wise out-of-fold training predictions.
"""
from __future__ import annotations

import argparse, gc, json, pickle
from pathlib import Path
import numpy as np

BVP_FS, EDA_FS, WINDOW, STEP = 64, 4, 5, 1


def bvp_peaks(x):
    smooth = np.convolve(x, np.ones(5) / 5, mode="same")
    candidates = np.where((smooth[1:-1] > smooth[:-2]) & (smooth[1:-1] >= smooth[2:]))[0] + 1
    threshold, kept = np.percentile(smooth, 65), []
    for index in candidates:
        if smooth[index] < threshold: continue
        if not kept or index - kept[-1] >= 22: kept.append(index)
        elif smooth[index] > smooth[kept[-1]]: kept[-1] = index
    return np.asarray(kept)


def bvp_features(x):
    p = bvp_peaks(x)
    if len(p) < 3: return None
    ibi = np.diff(p) / BVP_FS; hr = 60 / ibi
    rmssd = np.sqrt(np.mean(np.diff(ibi) ** 2)) if len(ibi) > 1 else 0.0
    return np.array([x.mean(), x.std(), np.mean(x * x), hr.mean(), hr.std(), np.std(ibi), rmssd])


def eda_features(x):
    d = np.diff(x) * EDA_FS
    detrended = x - np.linspace(x[0], x[-1], len(x))
    level = max(np.std(d), 1e-8)
    return np.array([x.mean(), x.std(), x.max()-x.min(), (x[-1]-x[0])/WINDOW,
                     np.mean(np.abs(d)), np.std(d), np.sum(d > level)/max(len(d), 1), np.std(detrended)])


def load(path):
    with path.open("rb") as f: obj = pickle.load(f, encoding="latin1")
    bvp = np.asarray(obj["signal"]["wrist"]["BVP"]).reshape(-1)
    eda = np.asarray(obj["signal"]["wrist"]["EDA"]).reshape(-1)
    label = np.asarray(obj["label"]).reshape(-1)
    del obj; gc.collect()
    return bvp, eda, label


def window_label(label, start_s):
    # Labels are recorded on the chest grid; select a 5-s interval at identical time.
    lo = int(start_s / (len(label) / 700.0) * len(label))
    hi = int((start_s + WINDOW) / (len(label) / 700.0) * len(label))
    y = label[lo:max(hi, lo+1)]
    values, counts = np.unique(y, return_counts=True)
    value, share = int(values[np.argmax(counts)]), counts.max() / len(y)
    return value if share >= .8 else None


def make_rows(root, selected):
    rows=[]
    for path in sorted(root.glob("S*/S*.pkl")):
        subject = path.parent.name
        if subject not in selected: continue
        bvp, eda, label = load(path)
        duration = min(len(bvp)/BVP_FS, len(eda)/EDA_FS)
        for start_s in range(0, int(duration)-WINDOW+1, STEP):
            state = window_label(label, start_s)
            if state not in (1, 2): continue
            bf = bvp_features(bvp[start_s*BVP_FS:(start_s+WINDOW)*BVP_FS])
            ef = eda_features(eda[start_s*EDA_FS:(start_s+WINDOW)*EDA_FS])
            if bf is not None: rows.append((subject, state-1, bf, ef))
        del bvp, eda, label; gc.collect()
    return rows


def calibrated_rows(rows):
    output=[]
    for subject in sorted({r[0] for r in rows}):
        person=[r for r in rows if r[0] == subject]
        base_b=np.vstack([r[2] for r in person if r[1] == 0]); base_e=np.vstack([r[3] for r in person if r[1] == 0])
        cb, sb=np.median(base_b,0), np.std(base_b,0)+1e-6
        ce, se=np.median(base_e,0), np.std(base_e,0)+1e-6
        output.extend((s,y,(b-cb)/sb,(e-ce)/se) for s,y,b,e in person)
    return output


def fit(x,y):
    mean, std=x.mean(0), x.std(0)+1e-8; z=(x-mean)/std; w=np.zeros(x.shape[1]); b=0.
    count=np.bincount(y,minlength=2).astype(float); weight=np.array([len(y)/(2*count[v]) for v in y])
    for _ in range(1800):
        p=1/(1+np.exp(-np.clip(z@w+b,-30,30))); error=weight*(p-y)
        w-=.08*(z.T@error/weight.sum()); b-=.08*error.sum()/weight.sum()
    return w,b,mean,std


def predict(model,x):
    w,b,mean,std=model
    return 1/(1+np.exp(-np.clip(((x-mean)/std)@w+b,-30,30)))


def metric(prob,y,threshold):
    pred=(prob>=threshold).astype(int); tp=((y==1)&(pred==1)).sum(); tn=((y==0)&(pred==0)).sum()
    fp=((y==0)&(pred==1)).sum(); fn=((y==1)&(pred==0)).sum()
    precision=tp/max(tp+fp,1); recall=tp/max(tp+fn,1); f1=2*tp/max(2*tp+fp+fn,1)
    return dict(tp=int(tp),tn=int(tn),fp=int(fp),fn=int(fn),precision=float(precision),recall=float(recall),f1=float(f1))


def select_fusion(oof_b, oof_e, y, target, min_eda_weight, max_eda_weight):
    """Select score weight and threshold by out-of-fold, training-only predictions."""
    options=[]
    for eda_weight in np.arange(min_eda_weight, max_eda_weight + .001, .1):
        score=eda_weight*oof_e+(1-eda_weight)*oof_b
        for threshold in np.arange(.20,.96,.02):
            m=metric(score,y,threshold)
            if m["precision"] >= target: options.append((m["f1"],m["recall"],float(eda_weight),float(threshold),m))
    if not options: raise SystemExit("No fusion setting reached the requested training precision target.")
    return max(options, key=lambda v:(v[0],v[1]))


def main():
    p=argparse.ArgumentParser(); p.add_argument("--wesad",type=Path,required=True); p.add_argument("--subjects",default="S2,S3,S4,S5,S6,S7,S8")
    p.add_argument("--test-subject",default="S4"); p.add_argument("--target-precision",type=float,default=.70); p.add_argument("--min-eda-weight",type=float,default=.10); p.add_argument("--max-eda-weight",type=float,default=.90); p.add_argument("--export-only",action="store_true",help="Fit and export from all selected subjects; do not evaluate a test subject."); p.add_argument("--output",type=Path,default=Path("output/wesad_late_fusion")); a=p.parse_args()
    rows=calibrated_rows(make_rows(a.wesad,set(a.subjects.split(","))))
    tr=rows if a.export_only else [r for r in rows if r[0]!=a.test_subject]
    te=[] if a.export_only else [r for r in rows if r[0]==a.test_subject]
    subjects=sorted({r[0] for r in tr})
    # Out-of-fold modality probabilities give an honest training-only basis for fusion tuning.
    oof_b=[]; oof_e=[]; oof_y=[]
    for held in subjects:
        fit_rows=[r for r in tr if r[0]!=held]; val=[r for r in tr if r[0]==held]
        y=np.array([r[1] for r in fit_rows]); xb=np.vstack([r[2] for r in fit_rows]); xe=np.vstack([r[3] for r in fit_rows])
        oof_b.extend(predict(fit(xb,y),np.vstack([r[2] for r in val])))
        oof_e.extend(predict(fit(xe,y),np.vstack([r[3] for r in val])))
        oof_y.extend(r[1] for r in val)
    f1,recall,ew,threshold,oof_metric=select_fusion(np.array(oof_b),np.array(oof_e),np.array(oof_y),a.target_precision,a.min_eda_weight,a.max_eda_weight)
    y=np.array([r[1] for r in tr]); mb=fit(np.vstack([r[2] for r in tr]),y); me=fit(np.vstack([r[3] for r in tr]),y)
    a.output.mkdir(parents=True,exist_ok=True)
    np.savez_compressed(a.output/"fusion_model.npz", bvp_w=mb[0], bvp_b=mb[1], bvp_mean=mb[2], bvp_std=mb[3], eda_w=me[0], eda_b=me[1], eda_mean=me[2], eda_std=me[3], eda_weight=ew, bvp_weight=1-ew, threshold=threshold)
    result={"task":"WESAD wrist BVP + EDA event-conditioned stress confirmation","fusion":"classic decision-level late fusion: weighted average of independently trained modality probabilities","bvp_weight":round(1-ew,2),"eda_weight":round(ew,2),"window_seconds":WINDOW,"step_seconds":STEP,"test_subject":None if a.export_only else a.test_subject,"train_windows":len(tr),"test_windows":len(te),"selection":"subject-wise out-of-fold training predictions; maximize F1 subject to target precision","target_training_precision":a.target_precision,"threshold":round(threshold,2),"out_of_fold_training":{k:round(v,4) if isinstance(v,float) else v for k,v in oof_metric.items()},"model_file":"fusion_model.npz","status":"exported without test evaluation" if a.export_only else "exported and evaluated"}
    if not a.export_only:
        test_y=np.array([r[1] for r in te]); pb=predict(mb,np.vstack([r[2] for r in te])); pe=predict(me,np.vstack([r[3] for r in te]))
        test_metric=metric(ew*pe+(1-ew)*pb,test_y,threshold)
        result["test"]={k:round(v,4) if isinstance(v,float) else v for k,v in test_metric.items()}
    (a.output/"training_result.json").write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding="utf8"); print(json.dumps(result,ensure_ascii=False,indent=2))


if __name__=="__main__": main()
