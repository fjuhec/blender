namespace Compositor {
  namespace Device {
    class Device;
  }
}

#ifndef CMP_DEVICE_DEVICE_HPP
#define CMP_DEVICE_DEVICE_HPP

#include "device_task.hpp"
#include "cmp_node.hpp"

#include "DNA_listBase.h"
extern "C" {
#include "BLI_threads.h"
}


namespace Compositor {
  namespace Device {

    class Device {
    private:
      ThreadQueue *queue;
      ListBase threads;
      static void *thread_execute(void *data);
      int num_workers;

    protected:
      void set_num_workers(int num_workers);
      int get_num_workers();

    public:
      virtual ~Device();

      virtual void init(Node* node);

      void add_task(Task* task);
      virtual void execute_task(Task* task) = 0;
      virtual void task_finished(Task* task) = 0;

      virtual void start();
      virtual void stop();
      virtual void wait();

      /**
       * Create a device that is capable to calculate the given node(tree).
       * DeviceGLSL is (more) limited in Memory and number of textures.
       * this function counts the number of needed texture slots and try to reserve the
       * space for it. When it worked this DeviceGLSL will be returned.
       * otherwise a DeviceCPU instance will be returned.
       */
      static Device* create_device(Node* node);
      static void destroy_device(Device* device);
    };
  }
}
#endif
