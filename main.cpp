#include "l0device.h"

int main() {
  auto devices = l0::get_devices();
  auto gpu = devices[0];

  gpu->execute_kernel();
}
