#include "cmp_nodesocket.hpp"
#include "cmp_node.hpp"

#include "DNA_node_types.h"
#include "BKE_node.h"
#include "RNA_access.h"

namespace Compositor {
  NodeSocket::NodeSocket(Node* node, bNodeSocket * socket, RenderContext * render_context) {
    this->node = node;
    this->b_socket = socket;

    bNodeTree* node_tree = node->node_tree;
    for (bNodeLink *link = (bNodeLink*)node_tree->links.first; link; link = (bNodeLink*)link->next) {
      if (link->tosock == socket) {
        this->connected_node = new Node(node_tree, link->fromnode, render_context);
        return;
      }
    }

    // No link
    Node* connected_node;
    PointerRNA ptr;
    float default_value;
    float default_color[4];
    switch(socket->type) {
      case SOCK_FLOAT:
        RNA_pointer_create((ID *)node_tree, &RNA_NodeSocket, socket, &ptr);
        default_value = RNA_float_get(&ptr, "default_value");

        connected_node = new Node(CMP_NODE_VALUE);
        connected_node->var_float_0 = default_value;
        this->connected_node = connected_node;
        break;

      case SOCK_RGBA:
        RNA_pointer_create((ID *)node_tree, &RNA_NodeSocket, socket, &ptr);
        RNA_float_get_array(&ptr, "default_value", default_color);

        connected_node = new Node(CMP_NODE_RGB);
        connected_node->var_float_0 = default_color[0];
        connected_node->var_float_1 = default_color[1];
        connected_node->var_float_2 = default_color[2];
        connected_node->var_float_3 = default_color[3];
        this->connected_node = connected_node;
        break;

    }
  }

  NodeSocket::~NodeSocket() {
  }
}
