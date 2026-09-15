"""One-time PC-side exporter: .npz frozen model -> C header for Huangshan Pi."""
import argparse
from pathlib import Path
import numpy as np

def array(name, values):
    values=np.asarray(values).reshape(-1)
    return "static const float %s[%d] = { %s };" % (name,len(values),", ".join(f"{float(v):.9g}f" for v in values))

p=argparse.ArgumentParser(); p.add_argument("--model",type=Path,required=True); p.add_argument("--output",type=Path,required=True); a=p.parse_args()
m=np.load(a.model)
text="\n".join(["#pragma once","/* Generated from the frozen S2-S11 model. Do not hand-edit. */",array("GLOBAL_BVP_WEIGHT",m["bvp_w"]),f"static const float GLOBAL_BVP_BIAS = {float(m['bvp_b']):.9g}f;",array("GLOBAL_BVP_MEAN",m["bvp_mean"]),array("GLOBAL_BVP_STD",m["bvp_std"]),array("GLOBAL_EDA_WEIGHT",m["eda_w"]),f"static const float GLOBAL_EDA_BIAS = {float(m['eda_b']):.9g}f;",array("GLOBAL_EDA_MEAN",m["eda_mean"]),array("GLOBAL_EDA_STD",m["eda_std"]),""])
a.output.write_text(text,encoding="utf8")
