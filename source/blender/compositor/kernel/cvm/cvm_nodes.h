#define __CVM_node_method_name(node_name) node_execute_##node_name
#define CVM_float_node_start(node_name) __cvm_inline float __CVM_node_method_name(node_name) (KernelGlobal global, Node node, float2 xy) {
#define CVM_float_node_end }
#define CVM_float4_node_start(node_name) __cvm_inline float4 __CVM_node_method_name(node_name) (KernelGlobal global, Node node, float2 xy) {
#define CVM_float4_node_end }
#define CVM_float_node_call(node_input, xy) node_execute_float(global, node.input_##node_input, xy);
#define CVM_float4_node_call(node_input, xy) node_execute_float4(global, node.input_##node_input, xy);




#ifdef CMP_DEVICE_CPU

float node_execute_float(KernelGlobal global, int node_offset, float2 xy);
float4 node_execute_float4(KernelGlobal global, int node_offset, float2 xy);

#endif

#include "cvm_node_color.h"
#include "cvm_node_value.h"
#include "cvm_node_mix.h"
#include "cvm_node_viewer.h"
#include "cvm_node_dummy.h"

#ifdef CMP_DEVICE_CPU
// Include  the defines
#include "BKE_node.h"
float4 node_execute_float4(KernelGlobal global, int node_offset, float2 xy) {
  Node node = get_node(node_offset);
  switch (node.type) {
    case CMP_NODE_VIEWER:
      return node_execute_viewer(global, node, xy);

    case CMP_NODE_R_LAYERS:
      return node_execute_dummy(global, node, xy);

    case CMP_NODE_RGB:
      return node_execute_color(global, node, xy);

    case CMP_NODE_MIX_RGB:
        return node_execute_mix(global, node, xy);

    default:
      return CVM_ERROR;
  }

}

float node_execute_float(KernelGlobal global, int node_offset, float2 xy) {
  Node node = get_node(node_offset);
  switch (node.type) {
    case CMP_NODE_VALUE:
      return node_execute_value(global, node, xy);

    default:
      return 0.0;
  }
}
#endif
