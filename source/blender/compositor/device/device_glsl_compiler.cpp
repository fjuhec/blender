#include "device_glsl_compiler.hpp"
#include <iostream>


extern "C" {
  extern char datatoc_kernel_fragment_header_glsl[];
  extern char datatoc_kernel_fragment_main_glsl[];
  extern char datatoc_kernel_vertex_glsl[];
}

namespace Compositor {
  namespace Device {
    void build_generic_header(std::stringstream& out, Compositor::Node* node) {
      out << datatoc_kernel_fragment_header_glsl;
    }

    void build_headers(std::stringstream& out, Compositor::Node* node) {
      for (std::list<NodeSocket*>::const_iterator iterator = node->inputs.begin(), end = node->inputs.end(); iterator != end; ++iterator) {
        NodeSocket* socket = *iterator;
        build_headers(out, socket->connected_node);
      }

      out << "function void test();\n";
    }

    void build_source(std::stringstream& out, Compositor::Node* node) {
      for (std::list<NodeSocket*>::const_iterator iterator = node->inputs.begin(), end = node->inputs.end(); iterator != end; ++iterator) {
        NodeSocket* socket = *iterator;
        build_source(out, socket->connected_node);
      }

      out << "// START Node\n";
      out << node->glsl_template;
      out << "// END Node\n";
    }

    void build_main(std::stringstream& out) {
      out << datatoc_kernel_fragment_main_glsl;
    }

    std::string generate_glsl_vertex_source(Compositor::Node* node) {
      std::stringstream source;
      source << datatoc_kernel_vertex_glsl;
      return source.str();
    }

    std::string generate_glsl_fragment_source(Compositor::Node* node) {
      std::stringstream source;
      build_generic_header(source, node);
      source << "// START GLSL Compositor source\n";
      // build_headers(source, node);
      // build_source(source, node);
      build_main(source);
      source << "// END GLSL Compositor source\n";
      return source.str();
    }

    GLuint compile_vertex_shader(std::string vertex_source) {
      // std::cout << "version" << glGetString(GL_VERSION) << "\n";
      return 0;
      // GLuint shader = glCreateShader(GL_VERTEX_SHADER);
      // return shader;
    }

    GLuint compile_fragment_shader(std::string vertex_source) {
      // std::cout << "version" << glGetString(GL_VERSION) << "\n";
      return 0;
      // GLuint shader = glCreateShader(GL_FRAGMENT_SHADER);
      // return shader;
    }
  }
}
