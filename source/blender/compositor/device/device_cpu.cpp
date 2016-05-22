#include "device_cpu.hpp"

#define CMP_DEVICE_CPU
#define __cvm_inline inline
#include "kernel.h"
extern "C" {
  #include "BKE_node.h"
}

namespace Compositor {
  namespace Device {

    static void set_stack_index(Node* node, int *next_stack_index, int *next_texture_index) {
      if (node->stack_index != -1) return;

      node->stack_index = *next_stack_index;
      (*next_stack_index) ++;

      if (node->type == CMP_NODE_R_LAYERS) {
        node->texture_index = *next_texture_index;
        (*next_texture_index) ++;
      }

      for (std::list<NodeSocket*>::const_iterator iterator = node->inputs.begin(), end = node->inputs.end(); iterator != end; ++iterator) {
        NodeSocket* socket = *iterator;
        set_stack_index(socket->connected_node, next_stack_index, next_texture_index);
      }
    }

    static void update_node_stack(Node* node) {
      int index = node->stack_index;
      node_stack[index].type = node->type;

      node_stack[index].var_float_0 = node->var_float_0;
      node_stack[index].var_float_1 = node->var_float_1;
      node_stack[index].var_float_2 = node->var_float_2;
      node_stack[index].var_float_3 = node->var_float_3;

      int input_index = 0;
      for (std::list<NodeSocket*>::const_iterator iterator = node->inputs.begin(), end = node->inputs.end(); iterator != end; ++iterator) {
        NodeSocket* socket = *iterator;
        Node* connected_node = socket->connected_node;

        if (input_index == 0) {
          node_stack[index].input_0 = connected_node->stack_index;
        } else if (input_index == 1) {
          node_stack[index].input_1 = connected_node->stack_index;
        } else if (input_index == 2) {
          node_stack[index].input_2 = connected_node->stack_index;
        } else if (input_index == 3) {
          node_stack[index].input_3 = connected_node->stack_index;
        }

        update_node_stack(connected_node);

        input_index ++;
      }
    }

    static void update_textures(Node* node) {
      int index = node->stack_index;
      int texture_index = node->texture_index;
      if (texture_index != -1) {
        // TODO: upload texture
      }
      int input_index = 0;
      for (std::list<NodeSocket*>::const_iterator iterator = node->inputs.begin(), end = node->inputs.end(); iterator != end; ++iterator) {
        NodeSocket* socket = *iterator;
        Node* connected_node = socket->connected_node;
        update_textures(connected_node);
        input_index ++;
      }
    }

    void DeviceCPU::init(Node* node) {
      int next_stack_index = 0;
      int next_texture_index = 0;
      set_stack_index(node, &next_stack_index, &next_texture_index);
      update_node_stack(node);
      update_textures(node);
    }

    void DeviceCPU::execute_task(Task* task) {
      if (task->is_cancelled()) { return; }
      // perform task
      KernelGlobal globals;
      globals.phase = KG_PHASE_REFINE;
      globals.subpixel_samples_xy = 8;

      Node* node = task->node;
      const int curr_iteration = task->iteration;
      const int prev_iteration = curr_iteration - 1;

      float* buffer = task->output->buffer;
      int width = task->output->width;
      for (int y = task->y_min; y < task->y_max ; y ++) {
        int offset = (y*width + task->x_min)*4;
        for (int x = task->x_min; x < task->x_max ; x ++) {
          float2 xy = make_float2((float)x, (float)y);
          float4 color = node_execute_float4(globals, node->stack_index, xy);
          buffer[offset] = ((buffer[offset]*prev_iteration)+color.x)/curr_iteration;
          buffer[offset+1] = ((buffer[offset+1]*prev_iteration)+color.y)/curr_iteration;
          buffer[offset+2] = ((buffer[offset+2]*prev_iteration)+color.z)/curr_iteration;
          buffer[offset+3] = ((buffer[offset+3]*prev_iteration)+color.w)/curr_iteration;
          offset += 4;

        }
      }
    }
    void DeviceCPU::task_finished(Task* task) {
      task->output->update_subimage(task->x_min, task->y_min, task->x_max, task->y_max);
    }

  }
}
