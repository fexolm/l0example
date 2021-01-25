#pragma once
#include "device.h"
#include <level_zero/ze_api.h>
#include <memory>
#include <vector>
#include <llvm/IR/Module.h>

namespace l0 {
class L0MemView {};

class L0Device : public ::Device {
private:
  ze_context_handle_t context_;
  ze_device_handle_t device_;
  ze_command_queue_handle_t command_queue_;

public:
  L0Device(ze_driver_handle_t driver, ze_device_handle_t device);

  L0MemView allocate_device(size_t size);

  L0MemView allocate_pinned(size_t size);

  void execute_kernel() override;

  ~L0Device() override;
};

std::vector<std::shared_ptr<Device>> get_devices();
} // namespace l0

