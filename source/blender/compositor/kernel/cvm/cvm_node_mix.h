CVM_float4_node_start(mix)
  float value = CVM_float_node_call(0, xy);
  float inverse = 1.0 - value;
  float4 color1 = CVM_float4_node_call(1, xy);
  float4 color2 = CVM_float4_node_call(2, xy);
  
  return color1 * inverse + color2 * value;

CVM_float4_node_end
