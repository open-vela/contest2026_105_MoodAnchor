"""Personalize only the decision threshold with balanced feedback, then pure test.

For each person, first 2 min baseline calibrates features. The early half of
each baseline/stress block supplies simulated verified feedback (both FP and
FN are observable). The later half of each block is untouched pure testing.
Classifier and fusion weights are frozen throughout.
"""
from __future__ import annotations

import argparse, json
from pathlib import Path
import numpy as np
from evaluate_frozen_fusion import person_rows, probability


def classify_metrics(y, score, threshold):
    p=(score>=threshold).astype(int); tp=int(((y==1)&(p==1)).sum()); tn=int(((y==0)&(p==0)).sum())
    fp=int(((y==0)&(p==1)).sum()); fn=int(((y==1)&(p==0)).sum())
    return {"windows":int(len(y)),"accuracy":round((tp+tn)/len(y),4),"precision":round(tp/max(tp+fp,1),4),"recall":round(tp/max(tp+fn,1),4),"f1":round(2*tp/max(2*tp+fp+fn,1),4),"tp":tp,"tn":tn,"fp":fp,"fn":fn}


def choose_threshold(score,y,target=.90):
    options=[]
    for threshold in np.arange(.10,.96,.01):
        m=classify_metrics(y,score,threshold)
        if m["precision"]>=target: options.append((m["f1"],m["recall"],float(threshold),m))
    # Some people cannot reach the target with a threshold alone; favor F1 then.
    if not options:
        all_options=[(classify_metrics(y,score,t)["f1"],classify_metrics(y,score,t)["recall"],float(t),classify_metrics(y,score,t)) for t in np.arange(.10,.96,.01)]
        return max(all_options,key=lambda x:(x[0],x[1]))
    return max(options,key=lambda x:(x[0],x[1]))


def main():
    p=argparse.ArgumentParser(); p.add_argument("--wesad",type=Path,required=True); p.add_argument("--model",type=Path,required=True); p.add_argument("--subjects",default="S13,S14,S15,S16,S17"); p.add_argument("--eda-weight",type=float,default=.70); p.add_argument("--target-precision",type=float,default=.90); p.add_argument("--output",type=Path,required=True); a=p.parse_args()
    frozen=np.load(a.model); a.output.mkdir(parents=True,exist_ok=True)
    result={"protocol":"first 2 min baseline static calibration; first half of each baseline/stress block provides verified FP+FN feedback; later half of each block is pure test","fusion":{"eda_weight":a.eda_weight,"bvp_weight":1-a.eda_weight},"subjects":{}}
    for subject in a.subjects.split(","):
        rows=person_rows(a.wesad/subject/f"{subject}.pkl"); y=np.array([r[0] for r in rows])
        initial_base=np.arange(min(120,len(rows))); base_cal=initial_base[y[initial_base]==0]
        if len(base_cal)<100: raise SystemExit(f"{subject} lacks 2 min initial baseline")
        b=np.vstack([r[1] for r in rows]); e=np.vstack([r[2] for r in rows])
        cb,sb=np.median(b[base_cal],0),np.std(b[base_cal],0)+1e-6; ce,se=np.median(e[base_cal],0),np.std(e[base_cal],0)+1e-6
        xb,xe=(b-cb)/sb,(e-ce)/se
        pb=probability(frozen["bvp_w"],frozen["bvp_b"],frozen["bvp_mean"],frozen["bvp_std"],xb); pe=probability(frozen["eda_w"],frozen["eda_b"],frozen["eda_mean"],frozen["eda_std"],xe)
        score=a.eda_weight*pe+(1-a.eda_weight)*pb
        feedback=[]; pure=[]
        for label in (0,1):
            idx=np.flatnonzero(y==label)
            # Calibration windows are excluded from feedback and test.
            if label==0: idx=idx[idx>=120]
            cut=len(idx)//2
            feedback.extend(idx[:cut]); pure.extend(idx[cut:])
        feedback=np.array(sorted(feedback)); pure=np.array(sorted(pure))
        _,_,threshold,feedback_metric=choose_threshold(score[feedback],y[feedback],a.target_precision)
        pure_metric=classify_metrics(y[pure],score[pure],threshold)
        personal={key:frozen[key] for key in frozen.files}; personal.update(eda_weight=a.eda_weight,bvp_weight=1-a.eda_weight,threshold=threshold,personal_bvp_center=cb,personal_bvp_scale=sb,personal_eda_center=ce,personal_eda_scale=se)
        np.savez_compressed(a.output/f"{subject}_personalized_model.npz",**personal)
        result["subjects"][subject]={"total_valid_windows":len(rows),"static_calibration_windows":len(base_cal),"feedback_windows":len(feedback),"pure_test_windows":len(pure),"final_personal_threshold":round(threshold,2),"feedback_metrics":feedback_metric,"pure_test_metrics":pure_metric,"model_file":f"{subject}_personalized_model.npz"}
    (a.output/"personalized_balanced_feedback_pure_test.json").write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding="utf8"); print(json.dumps(result,ensure_ascii=False,indent=2))


if __name__=="__main__": main()
