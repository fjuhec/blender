namespace Compositor {
  struct NodeSocket;
}
#ifndef CMP_NODESOCKET_HPP
#define CMP_NODESOCKET_HPP

#include "DNA_node_types.h"
#include "cmp_node.hpp"
#include "cmp_rendercontext.hpp"

namespace Compositor {
  struct NodeSocket {
    Node* node;
    bNodeSocket *b_socket;
    Node* connected_node;

    NodeSocket(Node* node, bNodeSocket *socket, RenderContext * render_context);
    ~NodeSocket();
  };
}
#endif
