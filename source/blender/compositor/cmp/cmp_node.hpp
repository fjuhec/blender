namespace Compositor {
  struct Node;
}
#ifndef CMP_NODE_HPP
#define CMP_NODE_HPP

#include "DNA_node_types.h"
#include "cmp_nodesocket.hpp"
#include "cmp_rendercontext.hpp"
#include <list>
#include <string>

namespace Compositor {
  struct Node {
    bNodeTree * node_tree;

    /**
     * Reference to the (optional) bNode for this Node instance.
     * This is for debugging and possible (not likely) future enhancements.
     */
    bNode* b_node;

    int type;
    int stack_index;

    int texture_index;

    float var_float_0;
    float var_float_1;
    float var_float_2;
    float var_float_3;

    int var_int_0;
    int var_int_1;
    int var_int_2;
    int var_int_3;

    float* buffer;
    // TODO: Make int2
    int buffer_width;
    int buffer_height;

    std::string glsl_template;

    std::list<NodeSocket*> inputs;

    // TODO: Needs optional parameter with output socket you are evaluating.
    Node(bNodeTree* node_tree, bNode *node, RenderContext * render_context);
    Node(int type);
    ~Node();
  };
}
#endif
