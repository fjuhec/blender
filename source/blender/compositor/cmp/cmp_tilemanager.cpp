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

    int2 tile_size = make_int2(output->node_tree->chunksize, output->node_tree->chunksize);
    const int hilbert_size = (max(tile_size.x, tile_size.y) <= 12)? 8: 4;
    int tile_w = (tile_size.x >= width)? 1: (width + tile_size.x - 1)/tile_size.x;
    int tile_h = (tile_size.y >= height)? 1: (height + tile_size.y - 1)/tile_size.y;
    int2 center = make_int2(width/2, height/2);
    const int num = 1; // fixed number of devices.
    int tile_per_device = (tile_w * tile_h + num -1) / num;

    int2 block_size = tile_size * make_int2(hilbert_size, hilbert_size);
    /* Number of blocks to fill the image */
		int blocks_x = (block_size.x >= width)? 1: (width + block_size.x - 1)/block_size.x;
		int blocks_y = (block_size.y >= height)? 1: (height + block_size.y - 1)/block_size.y;
		int n = max(blocks_x, blocks_y) | 0x1; /* Side length of the spiral (must be odd) */
		/* Offset of spiral (to keep it centered) */
    int2 offset = make_int2((width - n*block_size.x)/2, (height - n*block_size.y)/2);
		offset = (offset / tile_size) * tile_size; /* Round to tile border. */

    int2 block = make_int2(0, 0); /* Current block */
    SpiralDirection prev_dir = DIRECTION_UP, dir = DIRECTION_UP;
    for(int i = 0;;) {
      /* Generate the tiles in the current block. */
			for(int hilbert_index = 0; hilbert_index < hilbert_size*hilbert_size; hilbert_index++) {
				int2 tile, hilbert_pos = hilbert_index_to_pos(hilbert_size, hilbert_index);
				/* Rotate block according to spiral direction. */
				if(prev_dir == DIRECTION_UP && dir == DIRECTION_UP) {
					tile = make_int2(hilbert_pos.y, hilbert_pos.x);
				}
				else if(dir == DIRECTION_LEFT || prev_dir == DIRECTION_LEFT) {
					tile = hilbert_pos;
				}
				else if(dir == DIRECTION_DOWN) {
					tile = make_int2(hilbert_size-1-hilbert_pos.y, hilbert_size-1-hilbert_pos.x);
				}
				else {
					tile = make_int2(hilbert_size-1-hilbert_pos.x, hilbert_size-1-hilbert_pos.y);
				}

				int2 pos = block*block_size + tile*tile_size + offset;
				/* Only add tiles which are in the image (tiles outside of the image can be generated since the spiral is always square). */
				if(pos.x >= 0 && pos.y >= 0 && pos.x < width && pos.y < height) {
					int w = min(tile_size.x, width - pos.x);
					int h = min(tile_size.y, height - pos.y);

          Compositor::Device::Task* task = new Compositor::Device::Task(node, pos.x, pos.y, pos.x+w, pos.y+h, this->output);
          tiles.push_front(task);
				}
			}

			/* Stop as soon as the spiral has reached the center block. */
			if(block.x == (n-1)/2 && block.y == (n-1)/2)
				break;

			/* Advance to next block. */
			prev_dir = dir;
			switch(dir) {
				case DIRECTION_UP:
					block.y++;
					if(block.y == (n-i-1)) {
						dir = DIRECTION_LEFT;
					}
					break;
				case DIRECTION_LEFT:
					block.x++;
					if(block.x == (n-i-1)) {
						dir = DIRECTION_DOWN;
					}
					break;
				case DIRECTION_DOWN:
					block.y--;
					if(block.y == i) {
						dir = DIRECTION_RIGHT;
					}
					break;
				case DIRECTION_RIGHT:
					block.x--;
					if(block.x == i+1) {
						dir = DIRECTION_UP;
						i++;
					}
					break;
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
