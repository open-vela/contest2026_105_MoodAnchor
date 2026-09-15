/* Auto-generated PAMAP2 wrist-IMU motion gate.
 * score = sigmoid(intercept + sum(coef[i] * (feature[i]-mean[i])/scale[i]))
 * If score >= threshold: filter this window; otherwise run physiology model.
 */
#ifndef MOODANCHOR_IMU_MOTION_GATE_H
#define MOODANCHOR_IMU_MOTION_GATE_H

#define IMU_MOTION_FEATURE_COUNT 11
#define IMU_MOTION_THRESHOLD 0.50000000f
static const char * const IMU_MOTION_FEATURE_NAMES[] = {"acc_mean", "acc_std", "acc_max", "acc_range", "acc_rms", "acc_energy", "acc_jerk_mean", "gyro_mean", "gyro_std", "gyro_max", "gyro_energy"};
static const float IMU_MOTION_MEAN[] = {10.60328740f, 2.24119217f, 20.87589770f, 14.06816781f, 11.11818369f, 135.32541930f, 0.76230690f, 1.03270928f, 0.58812203f, 2.88896890f, 2.96766054f};
static const float IMU_MOTION_SCALE[] = {2.30461137f, 3.54422624f, 20.23141768f, 21.91169378f, 3.42219385f, 143.41049768f, 0.95741532f, 1.08789395f, 0.60973052f, 2.98991476f, 5.87834895f};
static const float IMU_MOTION_COEF[] = {-2.47135662f, 6.64065197f, -0.90562601f, -1.40650326f, -3.22312125f, 3.63862981f, 2.72778904f, -1.25400567f, -0.66776492f, -0.54923526f, 1.50763113f};
static const float IMU_MOTION_INTERCEPT = -0.25384146f;

#endif
