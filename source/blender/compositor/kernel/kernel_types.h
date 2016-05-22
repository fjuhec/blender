#ifndef CMP_KERNEL_TYPES_H
#define CMP_KERNEL_TYPES_H

#ifdef CMP_DEVICE_CPU
struct float2 {
  float x;
  float y;
};

struct float3 {
  float x;
  float y;
  float z;
};

struct float4 {
  float x;
  float y;
  float z;
  float w;
};
#endif
#define KG_PHASE_REFINE 0
#define KG_PHASE_FINAL 1
struct KernelGlobal {
  int phase;
  int subpixel_samples_xy;
};

struct Node {
  int type;
  int input_0;
  int input_1;
  int input_2;
  int input_3;
  int var_int_0;
  int var_int_1;
  int var_int_2;
  int var_int_3;
  float var_float_0;
  float var_float_1;
  float var_float_2;
  float var_float_3;
};

struct Texture {
  float* buffer;
  int width;
  int height;
};



#endif
