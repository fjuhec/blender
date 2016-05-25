#define MA_RAMP_BLEND		0
#define MA_RAMP_ADD			1
#define MA_RAMP_MULT		2
#define MA_RAMP_SUB			3
#define MA_RAMP_SCREEN		4
#define MA_RAMP_DIV			5
#define MA_RAMP_DIFF		6
#define MA_RAMP_DARK		7
#define MA_RAMP_LIGHT		8
#define MA_RAMP_OVERLAY		9
#define MA_RAMP_DODGE		10
#define MA_RAMP_BURN		11
#define MA_RAMP_HUE			12
#define MA_RAMP_SAT			13
#define MA_RAMP_VAL			14
#define MA_RAMP_COLOR		15
#define MA_RAMP_SOFT        16
#define MA_RAMP_LINEAR      17

CVM_float4_node_start(mix)
  float weight_value;
  float weight_color1;
  float weight_color2;
  float value = CVM_float_node_call(0, xy, __cvm_in_ref weight_value);
  float inverse = 1.0 - value;
  float4 color1 = CVM_float4_node_call(1, xy, __cvm_in_ref weight_color1);
  float4 color2 = CVM_float4_node_call(2, xy, __cvm_in_ref weight_color2);
  __cvm_out_set sample_weight = min(weight_color1, weight_color2);

  const int mix_type = node.var_int_0;
  if (mix_type == MA_RAMP_BLEND) {
    return color1 * inverse + color2 * value;

  } else if (mix_type == MA_RAMP_ADD) {
    return color1 + color2 * value;
  } else {
    return CVM_ERROR;
  }


CVM_float4_node_end
