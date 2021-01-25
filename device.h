#pragma once

class Device {
public:
  virtual ~Device() = default;
  virtual void execute_kernel() = 0;
};

class MemView {
public:
  virtual ~MemView() = default;
};
