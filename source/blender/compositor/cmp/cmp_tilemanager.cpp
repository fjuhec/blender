#include "cmp_tilemanager.hpp"

#define CMP_DEVICE_CPU
#define __cvm_inline inline
#include "kernel_functions.h"

namespace Compositor {
  inline int2 hilbert_index_to_pos(int n, int d)
  {
  	int2 r, xy = make_int2(0, 0);
  	for(int s = 1; s < n; s *= 2) {
  		r.x = (d >> 1) & 1;
  		r.y = (d ^ r.x) & 1;
  		if(!r.y) {
  			if(r.x) {
  				xy = make_int2(s-1, s-1) - xy;
  			}
  			swap(xy.x, xy.y);
  		}
  		xy += r*make_int2(s, s);
  		d >>= 2;
  	}
  	return xy;
  }

  enum SpiralDirection {
  	DIRECTION_UP,
  	DIRECTION_LEFT,
  	DIRECTION_DOWN,
  	DIRECTION_RIGHT,
  };


  TileManager::TileManager(Output* output) {
    this->output = output;
  }

  void TileManager::generate_tiles(std::list<Compositor::Device::Task*>& tiles) {
    Node* node = output->node;
    const int width = output->width;
    const int height = output->height;
    const int tile_size = output->node_tree->chunksize;

    for (int x = 0 ; x <= width; x += tile_size) {
      int x_max = x+tile_size;
      if (x_max > width) x_max = width;
      for (int y = 0;y <= height; y += tile_size) {
        int y_max = y+tile_size;
        if (y_max > height) y_max = height;

        Compositor::Device::Task* task = new Compositor::Device::Task(node, x, y, x_max, y_max, this->output);
        tiles.push_back(task);
      }
    }
  }
  void TileManager::delete_tiles(std::list<Compositor::Device::Task*>& tiles) {
    while (!tiles.empty())
    {
      Compositor::Device::Task *task = tiles.front();
      tiles.pop_front();
      delete(task);
    }
  }
}
