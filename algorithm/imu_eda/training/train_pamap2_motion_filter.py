"""Train a wrist-IMU motion gate for the MoodAnchor physiological detector.

This is deliberately NOT an emotion classifier.  It blocks windows whose
movement is likely to explain a heart-rate / SpO2 change before the existing
physiology model is consulted.

Dataset: PAMAP2 Protocol data, wrist ("hand") 3-axis accelerometer and gyro.
"""
from __future__ import print_function

import json
import os
import pickle
import sys

import numpy as np
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import accuracy_score, confusion_matrix, precision_recall_fscore_support
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import StandardScaler

ROOT = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(ROOT, "data", "PAMAP2_Dataset", "Protocol")
OUT = os.path.join(ROOT, "output")

# PAMAP2 activity IDs.  The gate is intentionally conservative: all common
# locomotion is blocked so its physiological effects do not reach the next
# stage.  Computer work is part of the allow class, which is useful for this
# project's intended quiet-use setting.
BLOCK_IDS = set([5, 6, 7, 20, 24])
ALLOW_IDS = set([1, 2, 3, 9, 10, 11, 16, 17, 18, 19])
ACTIVITY_NAMES = {
    1: "lying", 2: "sitting", 3: "standing", 4: "walking", 5: "running",
    6: "cycling", 7: "nordic_walking", 9: "watching_tv", 10: "computer_work",
    11: "car_driving", 12: "ascending_stairs", 13: "descending_stairs",
    16: "vacuum_cleaning", 17: "ironing", 18: "folding_laundry",
    19: "house_cleaning", 20: "playing_soccer", 24: "rope_jumping",
}
FEATURE_NAMES = [
    "acc_mean", "acc_std", "acc_max", "acc_range", "acc_rms", "acc_energy",
    "acc_jerk_mean", "gyro_mean", "gyro_std", "gyro_max", "gyro_energy",
]


def magnitude(a):
    return np.sqrt(np.sum(a * a, axis=1))


def features_for_window(acc, gyro):
    am = magnitude(acc)
    gm = magnitude(gyro)
    jerk = np.abs(np.diff(am))
    return [
        float(np.mean(am)), float(np.std(am)), float(np.max(am)),
        float(np.max(am) - np.min(am)), float(np.sqrt(np.mean(am * am))),
        float(np.mean(am * am)), float(np.mean(jerk)) if len(jerk) else 0.0,
        float(np.mean(gm)), float(np.std(gm)), float(np.max(gm)),
        float(np.mean(gm * gm)),
    ]


def subject_windows(path, subject_id, window=200, stride=100):
    # PAMAP2 columns: activity=1; wrist/hand acc16g=4:7, gyro=10:13.
    raw = np.loadtxt(path)
    activity = raw[:, 1].astype(int)
    acc = raw[:, 4:7]
    gyro = raw[:, 10:13]
    X, y, groups, activity_labels = [], [], [], []
    for start in range(0, len(activity) - window + 1, stride):
        labels = activity[start:start + window]
        label = int(np.bincount(labels).argmax())
        purity = float(np.mean(labels == label))
        if purity < 0.90:
            continue
        if label in BLOCK_IDS:
            target = 1
        elif label in ALLOW_IDS:
            target = 0
        else:
            continue
        a = acc[start:start + window]
        g = gyro[start:start + window]
        if not (np.isfinite(a).all() and np.isfinite(g).all()):
            continue
        X.append(features_for_window(a, g))
        y.append(target)
        groups.append(subject_id)
        activity_labels.append(label)
    return X, y, groups, activity_labels


def metric_dict(y_true, y_pred):
    # sklearn 0.19 (installed on the training workstation) predates the
    # zero_division keyword.  Both classes are present in these protocol
    # splits, so its legacy behaviour is sufficient here.
    p, r, f, _ = precision_recall_fscore_support(
        y_true, y_pred, average="binary", pos_label=1
    )
    tn, fp, fn, tp = confusion_matrix(y_true, y_pred, labels=[0, 1]).ravel()
    return {
        "accuracy": round(float(accuracy_score(y_true, y_pred)), 4),
        "motion_precision": round(float(p), 4),
        "motion_recall": round(float(r), 4),
        "motion_f1": round(float(f), 4),
        "tn_allow_correct": int(tn), "fp_over_filter": int(fp),
        "fn_motion_missed": int(fn), "tp_motion_blocked": int(tp),
    }


def choose_threshold(y, prob, min_recall=0.90):
    # A missed exercise window is the risky error for this gate. Pick the
    # highest threshold that still keeps recall >= 0.95 on validation, which
    # limits needless filtering while prioritising protection from confounds.
    candidates = np.arange(0.10, 0.91, 0.01)
    valid = []
    for t in candidates:
        m = metric_dict(y, (prob >= t).astype(int))
        if m["motion_recall"] >= min_recall:
            valid.append((t, m))
    if valid:
        return valid[-1]
    return 0.50, metric_dict(y, (prob >= 0.50).astype(int))


def select_diverse_baseline(y, activity_labels, wanted=120):
    """Pick 2 min of low-activity windows, spread across available tasks."""
    eligible = np.where(y == 0)[0]
    labels = sorted(set(activity_labels[eligible].tolist()))
    selected = []
    if not len(eligible):
        return np.asarray([], dtype=int)
    quota = max(1, wanted // max(1, len(labels)))
    for label in labels:
        choices = eligible[activity_labels[eligible] == label]
        selected.extend(choices[:quota].tolist())
    used = set(selected)
    selected.extend([i for i in eligible if i not in used][:max(0, wanted - len(selected))])
    return np.asarray(sorted(selected[:wanted]), dtype=int)


def split_feedback_and_blind(indices, y, seed):
    """Deterministic 50/50 class-stratified split after calibration."""
    rng = np.random.RandomState(seed)
    feedback, blind = [], []
    for cls in [0, 1]:
        part = indices[y[indices] == cls].copy()
        rng.shuffle(part)
        cut = len(part) // 2
        feedback.extend(part[:cut].tolist())
        blind.extend(part[cut:].tolist())
    return np.asarray(feedback, dtype=int), np.asarray(blind, dtype=int)


def align_to_personal_baseline(X, personal_mean, personal_std, global_mean, global_std):
    """Freeze classifier weights; only align this wearer's quiet baseline."""
    return global_mean + ((X - personal_mean) / np.maximum(personal_std, 1e-6)) * global_std


def write_c_header(model, threshold):
    scaler = model.named_steps["scale"]
    lr = model.named_steps["lr"]
    text = """/* Auto-generated PAMAP2 wrist-IMU motion gate.\n * score = sigmoid(intercept + sum(coef[i] * (feature[i]-mean[i])/scale[i]))\n * If score >= threshold: filter this window; otherwise run physiology model.\n */\n#ifndef MOODANCHOR_IMU_MOTION_GATE_H\n#define MOODANCHOR_IMU_MOTION_GATE_H\n\n#define IMU_MOTION_FEATURE_COUNT %d\n#define IMU_MOTION_THRESHOLD %.8ff\nstatic const char * const IMU_MOTION_FEATURE_NAMES[] = {%s};\nstatic const float IMU_MOTION_MEAN[] = {%s};\nstatic const float IMU_MOTION_SCALE[] = {%s};\nstatic const float IMU_MOTION_COEF[] = {%s};\nstatic const float IMU_MOTION_INTERCEPT = %.8ff;\n\n#endif\n""" % (
        len(FEATURE_NAMES), threshold,
        ", ".join('"%s"' % x for x in FEATURE_NAMES),
        ", ".join("%.8ff" % x for x in scaler.mean_),
        ", ".join("%.8ff" % x for x in scaler.scale_),
        ", ".join("%.8ff" % x for x in lr.coef_[0]), float(lr.intercept_[0]),
    )
    with open(os.path.join(OUT, "imu_motion_gate_params.h"), "w") as f:
        f.write(text)


def main():
    os.makedirs(OUT, exist_ok=True)
    X, y, groups, activities = [], [], [], []
    for sid in range(101, 110):
        path = os.path.join(DATA, "subject%d.dat" % sid)
        if not os.path.exists(path):
            raise RuntimeError("Missing %s" % path)
        sx, sy, sg, sa = subject_windows(path, sid)
        print("subject %d: %d usable windows" % (sid, len(sx)))
        X.extend(sx); y.extend(sy); groups.extend(sg); activities.extend(sa)
    X, y, groups, activities = np.asarray(X), np.asarray(y), np.asarray(groups), np.asarray(activities)

    # Global weights are trained on several people then frozen. S107/S108 are
    # new wearers: 2 min baseline -> 50% labelled feedback -> 50% blind test.
    train = np.isin(groups, [101, 102, 103, 104, 105, 106])
    model = Pipeline([
        ("scale", StandardScaler()),
        ("lr", LogisticRegression(class_weight="balanced", solver="liblinear", max_iter=1000, random_state=42)),
    ])
    model.fit(X[train], y[train])
    global_quiet_mean = np.mean(X[train & (y == 0)], axis=0)
    global_quiet_std = np.std(X[train & (y == 0)], axis=0)
    per_subject, all_global_y, all_global_pred, all_personal_y, all_personal_pred = {}, [], [], [], []
    for sid in [107, 108]:
        subject_idx = np.where(groups == sid)[0]
        local_y, local_x, local_act = y[subject_idx], X[subject_idx], activities[subject_idx]
        baseline_local = select_diverse_baseline(local_y, local_act, wanted=120)
        if len(baseline_local) < 60:
            raise RuntimeError("Subject %d does not have 60 seconds of quiet baseline" % sid)
        baseline_set = set(baseline_local.tolist())
        remaining_local = np.asarray([i for i in range(len(local_y)) if i not in baseline_set], dtype=int)
        feedback_local, blind_local = split_feedback_and_blind(remaining_local, local_y, sid)
        personal_mean, personal_std = np.mean(local_x[baseline_local], axis=0), np.std(local_x[baseline_local], axis=0)
        # Calibration strength itself is chosen solely from feedback.  Zero
        # means retain the frozen global feature space; one means full personal
        # baseline alignment. This prevents a harmful calibration from being
        # forced on a wearer whose baseline does not transfer cleanly.
        candidates = []
        for strength in [0.0, 0.25, 0.50, 0.75, 1.0]:
            centre = (1.0 - strength) * global_quiet_mean + strength * personal_mean
            spread = (1.0 - strength) * global_quiet_std + strength * personal_std
            candidate_x = align_to_personal_baseline(local_x, centre, spread, global_quiet_mean, global_quiet_std)
            candidate_prob = model.predict_proba(candidate_x[feedback_local])[:, 1]
            candidate_threshold, candidate_metrics = choose_threshold(local_y[feedback_local], candidate_prob, min_recall=0.90)
            candidates.append((candidate_metrics["accuracy"], strength, candidate_threshold, candidate_metrics, candidate_x))
        _, strength, threshold, feedback_metrics, aligned = max(candidates, key=lambda item: item[0])
        blind_global_pred = (model.predict_proba(local_x[blind_local])[:, 1] >= 0.50).astype(int)
        blind_personal_pred = (model.predict_proba(aligned[blind_local])[:, 1] >= threshold).astype(int)
        per_subject[str(sid)] = {
            "baseline_windows": int(len(baseline_local)), "baseline_seconds": int(len(baseline_local)),
            "feedback_windows": int(len(feedback_local)), "blind_windows": int(len(blind_local)),
            "personal_calibration_strength": round(float(strength), 2),
            "personal_threshold": round(float(threshold), 2),
            "feedback_metrics_used_for_threshold_only": feedback_metrics,
            "blind_global_frozen_metrics": metric_dict(local_y[blind_local], blind_global_pred),
            "blind_personalized_metrics": metric_dict(local_y[blind_local], blind_personal_pred),
        }
        all_global_y.extend(local_y[blind_local]); all_global_pred.extend(blind_global_pred)
        all_personal_y.extend(local_y[blind_local]); all_personal_pred.extend(blind_personal_pred)
    global_blind = metric_dict(np.asarray(all_global_y), np.asarray(all_global_pred))
    personalized_blind = metric_dict(np.asarray(all_personal_y), np.asarray(all_personal_pred))
    result = {
        "purpose": "Personalized vigorous-motion filter before physiological emotion detection; not an emotion classifier.",
        "dataset": "PAMAP2 Protocol, wrist/hand IMU, 9 subjects, 100 Hz",
        "feature_window": "2.0 seconds, 1.0 second step",
        "block_activities": [ACTIVITY_NAMES[x] for x in sorted(BLOCK_IDS)],
        "allow_activities": [ACTIVITY_NAMES[x] for x in sorted(ALLOW_IDS)],
        "global_train_subjects": [101, 102, 103, 104, 105, 106],
        "personalization_protocol": "Each unseen wearer: 120 s quiet baseline -> 50% labelled feedback threshold tuning -> 50% blind test. Global logistic-regression weights remain frozen.",
        "unseen_personalization_subjects": [107, 108],
        "aggregate_blind_global_frozen_metrics": global_blind,
        "aggregate_blind_personalized_metrics": personalized_blind,
        "per_unseen_subject": per_subject,
        "samples": {"global_train": int(np.sum(train)), "personalized_blind_total": int(len(all_personal_y))},
        "features": FEATURE_NAMES,
    }
    with open(os.path.join(OUT, "metrics.json"), "w") as f:
        json.dump(result, f, indent=2)
    with open(os.path.join(OUT, "imu_motion_gate.pkl"), "wb") as f:
        pickle.dump({"model": model, "initial_threshold": 0.50, "features": FEATURE_NAMES}, f, protocol=2)
    write_c_header(model, 0.50)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
