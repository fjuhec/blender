
CVM_float4_node_start(blur)

  float blur_size = global.width * node.var_float_0;
  float blur_size_squared = blur_size * blur_size;

  if (global.phase == KG_PHASE_REFINE) {
    float2 move = (rand_float2(xy) * 2.f - 1.f) * blur_size;
    if (length_squared(move) <= blur_size_squared)
      return CVM_float4_node_call(0, xy+move, sample_weight);

    __cvm_out_set sample_weight = 0.;
    return make_float4(0.f, 0.f, 0.f, 0.f);
  } else {
    // TODO: FINAL do not randomize
    float2 move = (rand_float2(xy) * 2.f - 1.f) * blur_size;
    if (length_squared(move) <= blur_size_squared)
      return CVM_float4_node_call(0, xy+move, sample_weight);

    __cvm_out_set sample_weight = 1.;
    return make_float4(1.f, 0.f, 0.f, 0.f);
  }


CVM_float4_node_end
