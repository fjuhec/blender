#include "kernel_types.h"
#include "kernel_functions.h"

#ifdef CMP_DEVICE_CPU
Node node_stack[CMP_MAX_NODE_STACK];
Texture textures[CMP_MAX_TEXTURES];

__cvm_inline Node get_node(int node_offset) { return node_stack[node_offset]; }

#endif


#include "cvm_nodes.h"
