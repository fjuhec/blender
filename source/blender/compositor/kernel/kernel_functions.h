#include "kernel_constants.h"
#include "kernel_types.h"

#ifdef CMP_DEVICE_CPU
__cvm_inline float2 make_float2(float x, float y) { return (float2){x, y}; }
__cvm_inline float4 make_float4(float x, float y, float z, float w) { return (float4){x, y, z, w}; }


__cvm_inline float2 operator*(const float2 a, float f) { return make_float2(a.x*f, a.y*f); }
__cvm_inline float2 operator+(const float2 a, const float2 b) { return make_float2(a.x+b.x, a.y+b.y); }
__cvm_inline float2 operator-(const float2 a, float f) { return make_float2(a.x-f, a.y-f); }
#include <cmath>
__cvm_inline float length(float2 a) { return std::sqrt(a.x*a.x + a.y*a.y); }

__cvm_inline float4 operator*(const float4 a, float f) { return make_float4(a.x*f, a.y*f, a.z*f, a.w*f); }
__cvm_inline float4 operator/(const float4 a, int i) { return make_float4(a.x/i, a.y/i, a.z/i, a.w/i); }
__cvm_inline float4 operator/(const float4 a, float f) { return make_float4(a.x/f, a.y*f, a.z*f, a.w*f); }
__cvm_inline float4 operator+(const float4 a, const float4 b) { return make_float4(a.x+b.x, a.y+b.y, a.z+b.z, a.w+b.w); }
__cvm_inline float4 operator+=(float4& a, const float4& b) { return a = a + b; }


#include <cstdlib>
float rand_float(float2 uv) { return static_cast <float> (std::rand()) / static_cast <float> (RAND_MAX); }
float2 rand_float2(float2 uv) { return make_float2(rand_float(uv), rand_float(uv)); }

#ifndef node_stack
Node node_stack[CMP_MAX_NODE_STACK];
Texture textures[CMP_MAX_TEXTURES];
#endif

__cvm_inline Node get_node(int node_offset) { return node_stack[node_offset]; }

#endif
