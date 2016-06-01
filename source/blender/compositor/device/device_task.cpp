#include "device_task.hpp"

namespace Compositor {
  namespace Device {
    Task::Task(Compositor::Node *node, int x_min, int y_min, int x_max, int y_max, Compositor::Output *output) {
      this->node = node;
      this->output = output;
      this->x_min = x_min;
      this->y_min = y_min;
      this->x_max = x_max;
      this->y_max = y_max;
      this->iteration = 0;
      this->max_iteration = 1;
      this->xy_subsamples = 8;
    }
  }
}
