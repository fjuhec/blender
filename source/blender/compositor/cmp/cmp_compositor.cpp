extern "C" {
  #include "BKE_node.h"
  #include "BLI_threads.h"
}

#include "COM_compositor.h"

#include "cmp_unroll.hpp"
#include "cmp_output.hpp"
#include "cmp_rendercontext.hpp"
#include "device_cpu.hpp"
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

  // UNROLL editingtree
  Compositor::Node* node = Compositor::unroll(editingtree, render_context);
  if (node != NULL) {
    // SELECT DEVICE
    Compositor::Device::Device *device = new Compositor::Device::DeviceCPU();
    device->init(node);

    // ALLOCATE output
    Compositor::Output output(editingtree, node, rd, viewName, viewSettings, displaySettings);

    // Generate Tiles
    const int width = output.width;
    const int height = output.height;
    const int tile_size = editingtree->chunksize;
    int num_x_tiles = (width + tile_size) / tile_size;
    int num_y_tiles = (height + tile_size) / tile_size;
    int num_tiles = num_x_tiles * num_y_tiles;
    Compositor::Device::Task** tasks = new Compositor::Device::Task*[num_x_tiles*num_y_tiles];
    for (int i = 0 ; i < num_tiles; i ++) tasks[i] = NULL;

    int tile_index = 0;
    for (int x = 0 ; x <= width; x += tile_size) {
      int x_max = x+tile_size;
      if (x_max > width) x_max = width;
      for (int y = 0;y <= height; y += tile_size) {
        int y_max = y+tile_size;
        if (y_max > height) y_max = height;

        Compositor::Device::Task* task = new Compositor::Device::Task(node, x, y, x_max, y_max, &output);
        task->max_iteration = 100;
        tasks[tile_index++] = task;
      }
    }
    // Schedule
    device->start();
    for (int i = 0 ; i < num_tiles; i ++) {
      Compositor::Device::Task *task = tasks[i];
      if (task != NULL) {
        device->add_task(task);
      }
    }
    device->wait();
    device->stop();


    // output.update_subimage(0, 0, output.width, output.height);

    delete device;
    for (int i = 0 ; i < num_tiles; i ++) {
      Compositor::Device::Task *task = tasks[i];
      if (task != NULL) {
        delete(task);
      }
    }
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
