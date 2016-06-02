#ifndef CMP_DEVICE_DEVICE_GLSL_COMPILER_HPP
#define CMP_DEVICE_DEVICE_GLSL_COMPILER_HPP

#include "cmp_node.hpp"
#include <sstream>

namespace Compositor {
  namespace Device {
    std::string generate_glsl_source(Compositor::Node* node);
  }
}
#endif
