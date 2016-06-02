#include "device_glsl.hpp"
#include <iostream>
#include "device_glsl_compiler.hpp"

namespace Compositor {
  namespace Device {
    void DeviceGLSL::init(Compositor::Node* node) {
      set_num_workers(1);
      std::string glsl_vertex = generate_glsl_vertex_source(node);
      std::cout << glsl_vertex << "\n";
      // GLuint vertex_shader = compile_vertex_shader(glsl_vertex);

      std::string glsl_fragment = generate_glsl_fragment_source(node);
      std::cout << glsl_fragment << "\n";
      // GLuint fragment_shader = compile_fragment_shader(glsl_fragment);

      // glDeleteShader(vertex_shader);
      // glDeleteShader(fragment_shader);
    }

    void DeviceGLSL::execute_task(Task* task) {
    }

    void DeviceGLSL::task_finished(Task* task) {
    }
  }
}
