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

    public:
      virtual ~Device();

      virtual void init(Node* node);

      void add_task(Task* task);
      virtual void execute_task(Task* task) = 0;
      virtual void task_finished(Task* task) = 0;

      virtual void start();
      virtual void stop();
      virtual void wait();
    };
  }
}
#endif
