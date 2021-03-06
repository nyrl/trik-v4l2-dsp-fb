#ifndef TRIK_V4L2_DSP_FB_INTERNAL_COMMON_H_
#define TRIK_V4L2_DSP_FB_INTERNAL_COMMON_H_

#include <stdbool.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


typedef struct ImageDescription
{
  size_t m_width;
  size_t m_height;
  size_t m_lineLength;
  size_t m_imageSize;
  uint32_t m_format;
} ImageDescription;

typedef struct TargetDetectParams
{
  int m_detectHueFrom;
  int m_detectHueTo;
  int m_detectSatFrom;
  int m_detectSatTo;
  int m_detectValFrom;
  int m_detectValTo;
} TargetDetectParams;

typedef struct TargetLocation
{
  int m_targetX;
  int m_targetY;
  int m_targetSize;
} TargetLocation;

typedef struct DriverManualControl
{
  bool m_manualMode;

  int  m_ctrlChasisLR;
  int  m_ctrlChasisFB;
  int  m_ctrlHand;
  int  m_ctrlArm;
} DriverManualControl;

typedef struct RoverControl
{
  int m_chasisLeftSpeed;
  int m_chasisRightSpeed;
  int m_handSpeed;
  int m_armSpeed;
} RoverControl;


#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus


#endif // !TRIK_V4L2_DSP_FB_INTERNAL_COMMON_H_
