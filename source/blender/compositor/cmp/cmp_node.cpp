#include "cmp_node.hpp"

namespace Compositor {
  Node::Node() {
    this->node_tree = NULL;
    this->b_node = NULL;
    this->stack_index = -1;
  }

  Node::Node(bNodeTree* node_tree, bNode *node) {
    this->node_tree = node_tree;
    this->b_node = node;
    this->stack_index = -1;
    this->texture_index = -1;

    this->type = node->type;

    for (bNodeSocket *socket = (bNodeSocket *)node->inputs.first; socket; socket = socket->next) {
      this->add_input_socket(new NodeSocket(this, socket));
    }
  }

  Node::~Node() {
  }

  void Node::add_input_socket(NodeSocket* socket) {
    this->inputs.push_back(socket);
  }
}
