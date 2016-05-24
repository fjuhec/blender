CVM_float4_node_start(renderlayer)
  // TODO: make generic for GLSL and CPU.
  int width = textures[node.texture_index].width;
  int height = textures[node.texture_index].height;

  if (xy.x < 0 || xy.y < 0 || xy.x >= width || xy.y >= height) {
    return make_float4(0.0,0.0,0.0,0.0);
  }

  int offset = ((int)xy.y) * width + ((int)xy.x);
  offset *= 4;
  return make_float4(
    textures[node.texture_index].buffer[offset+0],
    textures[node.texture_index].buffer[offset+1],
    textures[node.texture_index].buffer[offset+2],
    textures[node.texture_index].buffer[offset+3]
  );
CVM_float4_node_end
