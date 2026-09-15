#include "mood_model.h"
#include "global_model_65_35.h"
#include <math.h>

static float sigmoid(float x) {
    if (x > 30.0f) return 1.0f;
    if (x < -30.0f) return 0.0f;
    return 1.0f / (1.0f + expf(-x));
}

static float probability(const float *raw, const float *personal_center,
                         const float *personal_scale, const float *global_mean,
                         const float *global_std, const float *weight,
                         unsigned int n, float bias) {
    float score = bias;
    for (unsigned int i = 0; i < n; ++i) {
        const float locally_normalized = (raw[i] - personal_center[i]) / (personal_scale[i] + 1e-6f);
        score += ((locally_normalized - global_mean[i]) / (global_std[i] + 1e-8f)) * weight[i];
    }
    return sigmoid(score);
}

float mood_bvp_probability(const mood_profile_t *p, const float raw[MOOD_BVP_FEATURES]) {
    return probability(raw, p->bvp_center, p->bvp_scale, GLOBAL_BVP_MEAN,
                       GLOBAL_BVP_STD, GLOBAL_BVP_WEIGHT, MOOD_BVP_FEATURES, GLOBAL_BVP_BIAS);
}

float mood_eda_probability(const mood_profile_t *p, const float raw[MOOD_EDA_FEATURES]) {
    return probability(raw, p->eda_center, p->eda_scale, GLOBAL_EDA_MEAN,
                       GLOBAL_EDA_STD, GLOBAL_EDA_WEIGHT, MOOD_EDA_FEATURES, GLOBAL_EDA_BIAS);
}

float mood_fused_score(const mood_profile_t *p, const float bvp[MOOD_BVP_FEATURES], const float eda[MOOD_EDA_FEATURES]) {
    const float eda_weight = (p->eda_weight > 0.0f && p->eda_weight < 1.0f) ? p->eda_weight : 0.70f;
    return eda_weight * mood_eda_probability(p, eda) + (1.0f - eda_weight) * mood_bvp_probability(p, bvp);
}

bool mood_confirm(const mood_profile_t *p, const float bvp[MOOD_BVP_FEATURES], const float eda[MOOD_EDA_FEATURES]) {
    return mood_fused_score(p, bvp, eda) >= p->threshold;
}

