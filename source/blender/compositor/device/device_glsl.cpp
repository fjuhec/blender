#include "device_glsl.hpp"
#include <iostream>
#include "device_glsl_compiler.hpp"

namespace Compositor {
  namespace Device {
    void DeviceGLSL::init(Compositor::Node* node) {
      set_num_workers(1);

      std::string glsl_source = generate_glsl_source(node);
      std::cout << glsl_source << "\n";
    }

    void DeviceGLSL::execute_task(Task* task) {
    }

    void DeviceGLSL::task_finished(Task* task) {
    }
  }
}
