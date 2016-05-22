#include "cmp_output.hpp"

#include "BLI_listbase.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"
#include "BLI_math_color.h"
#include "BLI_math_vector.h"
#include "BKE_image.h"
#include "BKE_scene.h"
#include "WM_api.h"
#include "WM_types.h"
#include "PIL_time.h"

extern "C" {
#  include "MEM_guardedalloc.h"
#  include "IMB_imbuf.h"
#  include "IMB_imbuf_types.h"
#  include "IMB_colormanagement.h"
}


namespace Compositor{
  Output::Output(bNodeTree *node_tree, Node* node, RenderData *rd,
      const char* view_name, const ColorManagedViewSettings *viewSettings,
      const ColorManagedDisplaySettings *displaySettings) {
    this->node_tree = node_tree;
    this->node = node;
    bNode* b_node = node->b_node;
    this->m_image = (Image *)b_node->id;
    this->m_imageUser = (ImageUser *) b_node->storage;

    this->m_rd = rd;
    this->m_viewName = view_name;

    this->m_viewSettings = viewSettings;
    this->m_displaySettings = displaySettings;

    this->width = rd->xsch * rd->size / 100.0f;
    this->height = rd->ysch * rd->size / 100.0f;


    // PREPARE

    init_image();
  }

  void Output::init_image() {
    Image *ima = this->m_image;
    ImageUser iuser = *this->m_imageUser;
    void *lock;
    ImBuf *ibuf;

    /* make sure the image has the correct number of views */
    if (ima && BKE_scene_multiview_is_render_view_first(this->m_rd, this->m_viewName)) {
      BKE_image_verify_viewer_views(this->m_rd, ima, this->m_imageUser);
    }

    BLI_lock_thread(LOCK_DRAW_IMAGE);

    /* local changes to the original ImageUser */
    iuser.multi_index = BKE_scene_multiview_view_id_get(this->m_rd, this->m_viewName);
    ibuf = BKE_image_acquire_ibuf(ima, &iuser, &lock);

    if (!ibuf) {
      BLI_unlock_thread(LOCK_DRAW_IMAGE);
      return;
    }
    if (ibuf->x != this->width || ibuf->y != this->height) {

      imb_freerectImBuf(ibuf);
      imb_freerectfloatImBuf(ibuf);
      IMB_freezbuffloatImBuf(ibuf);
      ibuf->x = width;
      ibuf->y = height;
      /* zero size can happen if no image buffers exist to define a sensible resolution */
      if (ibuf->x > 0 && ibuf->y > 0)
        imb_addrectfloatImBuf(ibuf);
      ima->ok = IMA_OK_LOADED;

      ibuf->userflags |= IB_DISPLAY_BUFFER_INVALID;
    }

    // if (m_doDepthBuffer) {
    //   addzbuffloatImBuf(ibuf);
    // }

    /* now we combine the input with ibuf */
    this->buffer = ibuf->rect_float;

    /* needed for display buffer update */
    this->m_ibuf = ibuf;

    // if (m_doDepthBuffer) {
    //   this->m_depthBuffer = ibuf->zbuf_float;
    // }

    BKE_image_release_ibuf(this->m_image, this->m_ibuf, lock);

    BLI_unlock_thread(LOCK_DRAW_IMAGE);
  }

  void Output::update_subimage(int x_min, int y_min, int x_max, int y_max) {
    IMB_partial_display_buffer_update(this->m_ibuf, this->buffer, NULL, this->width, 0, 0,
                                      this->m_viewSettings, this->m_displaySettings,
                                      x_min, y_min, x_max, y_max
                                      , false);

    if (this->node_tree->update_draw) this->node_tree->update_draw(this->node_tree->udh);
  }

}
