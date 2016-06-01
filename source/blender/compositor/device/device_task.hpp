namespace Compositor {
  namespace Device {
    class Task;
  }
}
#ifndef CMP_DEVICE_DEVICE_TASK_HPP
#define CMP_DEVICE_DEVICE_TASK_HPP

#include "device.hpp"
#include "cmp_output.hpp"

namespace Compositor {
  namespace Device {
    struct Task {
      Compositor::Node *node;
      int x_min;
      int y_min;
      int x_max;
      int y_max;
      Compositor::Output *output;

      int iteration;
      int max_iteration;
      int xy_subsamples;

      Task(Compositor::Node *node, int x_min, int y_min, int x_max, int y_max, Compositor::Output *output);

      bool is_cancelled() {
        bNodeTree * node_tree = this->output->node_tree;
        return (node_tree->test_break(node_tree->tbh));
      }

    };
  }
}
#endif
