#include "device_glsl_compiler.hpp"

namespace Compositor {
  namespace Device {
    void build_source(std::stringstream& out, Compositor::Node* node) {
      for (std::list<NodeSocket*>::const_iterator iterator = node->inputs.begin(), end = node->inputs.end(); iterator != end; ++iterator) {
        NodeSocket* socket = *iterator;
        build_source(out, socket->connected_node);
      }

      out << "// START Node\n";
      out << node->glsl_template;
      out << "// END Node\n";
    }

    std::string generate_glsl_source(Compositor::Node* node) {
      std::stringstream source;
      source << "// START GLSL Compositor source\n";
      build_source(source, node);
      source << "// END GLSL Compositor source\n";
      return source.str();
    }
  }
}
