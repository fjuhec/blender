#include "cmp_node.hpp"

#include "BKE_node.h"
#include "DNA_node_types.h"
#include "RNA_access.h"

#include "BLI_listbase.h"
extern "C" {
#  include "RE_pipeline.h"
#  include "RE_shader_ext.h"
#  include "RE_render_ext.h"
}


namespace Compositor {
  Node::Node() {
    this->node_tree = NULL;
    this->b_node = NULL;
    this->stack_index = -1;
    this->texture_index = -1;
    this->buffer = NULL;
  }

  Node::Node(bNodeTree* node_tree, bNode *node, RenderContext * render_context) {
    this->node_tree = node_tree;
    this->b_node = node;
    this->stack_index = -1;
    this->texture_index = -1;
    this->buffer = NULL;

    this->type = node->type;

    for (bNodeSocket *socket = (bNodeSocket *)node->inputs.first; socket; socket = socket->next) {
      this->inputs.push_back(new NodeSocket(this, socket, render_context));
    }

    switch (node->type) {
      case CMP_NODE_MIX_RGB:
        this->var_int_0 = node->custom1;
        break;

      case CMP_NODE_VALUE:
        PointerRNA ptr;
        RNA_pointer_create((ID *)node_tree, &RNA_NodeSocket, node->outputs.first, &ptr);
        this->var_float_0 = RNA_float_get(&ptr, "default_value");
        break;

      case CMP_NODE_R_LAYERS:
        short layer_id = node->custom1;
        Scene* scene = (Scene*)node->id;

        // TODO: Do not hardcode, but base on the output socket.
        int renderpass = SCE_PASS_COMBINED;
        int elementsize = 4;
        const char *view_name = render_context->view_name;

        Render *re = (scene) ? RE_GetRender(scene->id.name) : NULL;
        RenderResult *rr = NULL;
        float *buffer = NULL;

        if (re)
          rr = RE_AcquireResultRead(re);

        if (rr) {
      		SceneRenderLayer *srl = (SceneRenderLayer *)BLI_findlink(&scene->r.layers, layer_id);

          if (srl) {

      			RenderLayer *rl = RE_GetRenderLayer(rr, srl->name);
      			if (rl) {
      				buffer = RE_RenderLayerGetPass(rl, renderpass, view_name);
              // TODO: Looks double....
      				// if (buffer == NULL && renderpass == SCE_PASS_COMBINED) {
      				// 	buffer = RE_RenderLayerGetPass(rl, SCE_PASS_COMBINED, view_name);
      				// }
              this->buffer = buffer;
              this->buffer_width = rl->rectx;
              this->buffer_height = rl->recty;
      			}
      		}
      	}
      	if (re) {
      		RE_ReleaseResult(re);
      		re = NULL;
      	}

        break;
    }
  }

  Node::~Node() {
  }
}
