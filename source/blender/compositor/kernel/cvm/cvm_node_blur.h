
CVM_float4_node_start(blur)

  float blur_size = global.width * node.var_float_0;
  float2 move = (rand_float2(xy) * 2.f - 1.f) * blur_size;
  if (length(move) <= blur_size)
    return CVM_float4_node_call(0, xy+move, sample_weight);

  __cvm_out_set sample_weight = 0.;
  return make_float4(0.f, 0.f, 0.f, 0.f);

CVM_float4_node_end
