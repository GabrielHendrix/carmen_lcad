#ifndef CARMEN_STEREO_MAPPING_KALMAN_FILTER_H
#define CARMEN_STEREO_MAPPING_KALMAN_FILTER_H

/* Carmen includes */
#include <carmen/carmen.h>
#include <carmen/global.h>

#include <math.h>

/* Stereo includes */
#include <carmen/stereo_util.h>

/* OpenCV Includes */
#include <opencv/cv.h>
#include <opencv2/video/tracking.hpp>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
  double value;
  double value_variance;
  double value_variance_factor;
  double observation_variance;

  bool filter_outliers;
  double outlier_mean_value;
  double outlier_max_distance_from_mean;
} kalman_filter_params;

typedef struct
{
  CvKalman *kalman_filter;
  CvMat *control;
  CvMat *z_k;
} kalman_filter;

void kalman_update_state(cv::KalmanFilter *filter, kalman_filter_params *state[], double measurements[]);
void init_kalman_filter_params(kalman_filter_params *state, double value_variance, double value_variance_factor, double observation_variance);

#ifdef __cplusplus
}
#endif

#endif
