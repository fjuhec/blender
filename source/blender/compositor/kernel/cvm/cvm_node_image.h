CVM_float4_node_start(image)
  // TODO: make generic for GLSL and CPU.
  int width = textures[node.texture_index].width;
  int height = textures[node.texture_index].height;

  if (xy.x < 0 || xy.y < 0 || xy.x >= width || xy.y >= height) {
    __cvm_out_set sample_weight = 0.0f;
    return make_float4(0.0,0.0,0.0,0.0);
  }

  int offset = ((int)xy.y) * width + ((int)xy.x);
  offset *= 4;
  __cvm_out_set sample_weight = 1.0f;
  return make_float4(
    textures[node.texture_index].buffer[offset+0],
    textures[node.texture_index].buffer[offset+1],
    textures[node.texture_index].buffer[offset+2],
    textures[node.texture_index].buffer[offset+3]
  );
CVM_float4_node_end
