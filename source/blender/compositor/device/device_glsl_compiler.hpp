#ifndef CMP_DEVICE_DEVICE_GLSL_COMPILER_HPP
#define CMP_DEVICE_DEVICE_GLSL_COMPILER_HPP

#include "GPU_glew.h"
#include "cmp_node.hpp"
#include <sstream>

namespace Compositor {
  namespace Device {
    std::string generate_glsl_vertex_source(Compositor::Node* node);
    std::string generate_glsl_fragment_source(Compositor::Node* node);

    GLuint compile_vertex_shader(std::string vertex_source);
    GLuint compile_fragment_shader(std::string vertex_source);
  }
}
#endif
