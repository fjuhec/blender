#ifndef CMP_TILEMANAGER_HPP
#define CMP_TILEMANAGER_HPP
#include <list>
#include "device_task.hpp"
#include "cmp_output.hpp"

namespace Compositor {
  struct TileManager {
    Output* output;

    TileManager(Output* output);

    /**
     * Fills an empty list of tiles with tiles that needs to be calculated.
     * list will be in order..
     */
    void generate_tiles(std::list<Compositor::Device::Task*>& result);

    void delete_tiles(std::list<Compositor::Device::Task*>& tiles);
  };
}
#endif
