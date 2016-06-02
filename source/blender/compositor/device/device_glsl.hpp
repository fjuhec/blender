#ifndef CMP_DEVICE_DEVICE_GLSL_HPP
#define CMP_DEVICE_DEVICE_GLSL_HPP

#include "device.hpp"

namespace Compositor {
  namespace Device {
    class DeviceGLSL: public Device {

    public:
      void init(Compositor::Node* node);

      void execute_task(Task* task);
      void task_finished(Task* task);
    };
  }
}
#endif
