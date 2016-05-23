CVM_float4_node_start(renderlayer)
  // TODO: make generic for GLSL and CPU.
  int offset = ((int)xy.y) * textures[node.texture_index].width + ((int)xy.x);
  offset *= 4;
  return make_float4(
    textures[node.texture_index].buffer[offset+0],
    textures[node.texture_index].buffer[offset+1],
    textures[node.texture_index].buffer[offset+2],
    textures[node.texture_index].buffer[offset+3]
  );
CVM_float4_node_end
