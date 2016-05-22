#include "device.hpp"
namespace Compositor {
  namespace Device {
    Device::~Device() {

    }
    void *Device::thread_execute(void *data) {
      Device *device = (Device*) data;
      Task * task;
      while((task = (Task*)BLI_thread_queue_pop(device->queue))) {
        task->iteration ++;
        device->execute_task(task);
        device->task_finished(task);
        if (task->iteration < task->max_iteration) {
          device->add_task(task);
        }
      }
      return NULL;
    }

    void Device::init(Compositor::Node* node) {

    }

    void Device::add_task(Task* task) {
      if (!task->is_cancelled()) BLI_thread_queue_push(this->queue, task);
    }

    void Device::start() {
      this->queue  = BLI_thread_queue_init();
      BLI_init_threads(&this->threads, thread_execute, 4);
      for (int i = 0 ; i < 4 ; i ++ ) {
        BLI_insert_thread(&this->threads, this);
      }
    }
    void Device::stop() {
      BLI_thread_queue_nowait(this->queue);
    	BLI_end_threads(&this->threads);
    	BLI_thread_queue_free(this->queue);
    }
    void Device::wait() {
      BLI_thread_queue_wait_finish(this->queue);
    }
  }
}
