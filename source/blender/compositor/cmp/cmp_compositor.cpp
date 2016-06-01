extern "C" {
  #include "BKE_node.h"
  #include "BLI_threads.h"
}

#include "COM_compositor.h"

#include "cmp_unroll.hpp"
#include "cmp_output.hpp"
#include "cmp_rendercontext.hpp"
#include "cmp_tilemanager.hpp"
#include "device.hpp"
#include <iostream>

static ThreadMutex s_compositorMutex;
static bool is_compositorMutex_init = false;

extern "C" {

void COM_execute(RenderData *rd, Scene *scene, bNodeTree *editingtree, int rendering,
                 const ColorManagedViewSettings *viewSettings, const ColorManagedDisplaySettings *displaySettings,
                 const char *viewName) {
   /* initialize mutex, TODO this mutex init is actually not thread safe and
    * should be done somewhere as part of blender startup, all the other
    * initializations can be done lazily */
  if (editingtree->test_break(editingtree->tbh)) { return; }
   if (is_compositorMutex_init == false) {
     BLI_mutex_init(&s_compositorMutex);
     is_compositorMutex_init = true;
   }

   BLI_mutex_lock(&s_compositorMutex);


  // Create render context
  Compositor::RenderContext *render_context = new Compositor::RenderContext();
  render_context->view_name = viewName;

  int default_iteration = 1;
  int default_xy_subsamples = 8;

  if (!rendering) {
    switch (editingtree->edit_quality) {
      case NTREE_QUALITY_LOW:
        default_iteration = 2;
        default_xy_subsamples = 2;
        break;
  
      case NTREE_QUALITY_MEDIUM:
        default_iteration = 4;
        default_xy_subsamples = 4;
        break;

      case NTREE_QUALITY_HIGH:
        default_iteration = 8;
        default_xy_subsamples = 8;
        break;
    }
  }

  // UNROLL editingtree
  Compositor::Node* node = Compositor::unroll(editingtree, render_context);
  if (node != NULL) {
    // SELECT DEVICE
    Compositor::Device::Device *device = Compositor::Device::Device::create_device(node);

    // ALLOCATE output
    Compositor::Output output(editingtree, node, rd, viewName, viewSettings, displaySettings);

    // Generate Tiles
    Compositor::TileManager tile_manager(&output);
    std::list<Compositor::Device::Task*> tiles;
    tile_manager.generate_tiles(tiles);

    // Schedule
    device->start();
    for (std::list<Compositor::Device::Task*>::iterator it=tiles.begin(); it != tiles.end(); ++it) {
      Compositor::Device::Task *task = *it;
      task->max_iteration = default_iteration;
      task->xy_subsamples = default_xy_subsamples;
      device->add_task(task);
    }
    device->wait();
    device->stop();


    // output.update_subimage(0, 0, output.width, output.height);
    Compositor::Device::Device::destroy_device(device);
    tile_manager.delete_tiles(tiles);
  }

  delete render_context;
  BLI_mutex_unlock(&s_compositorMutex);
}


void COM_deinitialize(void) {
  if (is_compositorMutex_init) {
    BLI_mutex_lock(&s_compositorMutex);
    is_compositorMutex_init = false;
    BLI_mutex_unlock(&s_compositorMutex);
    BLI_mutex_end(&s_compositorMutex);
  }

}
void COM_startReadHighlights(void) {}
int COM_isHighlightedbNode(bNode *) { return false;}

}
