#include "cmp_unroll.hpp"

#include "BKE_node.h"

namespace Compositor {

  static bNode* find_active_viewer_node(bNodeTree* node_tree) {
    for (bNode *node = (bNode *)node_tree->nodes.first; node; node = node->next) {
      if (node->type == CMP_NODE_VIEWER) {
        // TODO: Active node
        return node;
      }
    }

    return NULL;
  }

  static Node* unroll_b_node(bNodeTree *node_tree, bNode * node) {
    Node* result = new Node(node_tree, node);
    return result;
  }

  Node* unroll(bNodeTree *node_tree) {
    bNode* viewer_node = find_active_viewer_node(node_tree);
    if (viewer_node == NULL) {
      return NULL;
    }


    Node* result = unroll_b_node(node_tree, viewer_node);
    return result;
  }
}
