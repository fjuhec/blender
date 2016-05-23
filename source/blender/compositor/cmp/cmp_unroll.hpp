#ifndef CMP_UNROLL_HPP
#define CMP_UNROLL_HPP

#include "cmp_node.hpp"
#include "DNA_node_types.h"
#include "cmp_rendercontext.hpp"

namespace Compositor {
  Node* unroll(bNodeTree * node_tree, RenderContext * render_context);
}
#endif
