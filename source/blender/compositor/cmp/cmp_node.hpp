namespace Compositor {
  struct Node;
}
#ifndef CMP_NODE_HPP
#define CMP_NODE_HPP

#include "DNA_node_types.h"
#include "cmp_nodesocket.hpp"
#include <list>

namespace Compositor {
  struct Node {
    bNodeTree * node_tree;
    bNode* b_node;
    int type;
    int stack_index;

    int texture_index;

    float var_float_0;
    float var_float_1;
    float var_float_2;
    float var_float_3;

    std::list<NodeSocket*> inputs;

    Node(bNodeTree* node_tree, bNode *node);
    Node();
    ~Node();

    void add_input_socket(NodeSocket* socket);

  };
}
#endif
