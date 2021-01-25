#include "l0device.h"
#include <llvm/Pass.h>

#define _SPIRV_SUPPORT_TEXT_FMT
#include <LLVMSPIRVLib/LLVMSPIRVLib.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <sstream>
#include <fstream>

#define L0_SAFE_CALL(call)                                                     \
  {                                                                            \
    auto status = (call);                                                      \
    if (status) {                                                              \
      std::cerr << "L0 error: " << std::hex << (int)status << " ";             \
      std::cerr << std::dec << __FILE__ << ":" << __LINE__ << std::endl;       \
      exit(status);                                                            \
    }                                                                          \
  }

namespace l0 {

L0Device::L0Device(ze_driver_handle_t driver, ze_device_handle_t device)
    : device_(device) {
  ze_context_desc_t ctx_desc = {ZE_STRUCTURE_TYPE_CONTEXT_DESC, nullptr, 0};
  L0_SAFE_CALL(zeContextCreate(driver, &ctx_desc, &context_));
  ze_command_queue_desc_t command_queue_desc = {
      ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC,
      nullptr,
      0,
      0,
      0,
      ZE_COMMAND_QUEUE_MODE_DEFAULT,
      ZE_COMMAND_QUEUE_PRIORITY_NORMAL};
  L0_SAFE_CALL(zeCommandQueueCreate(context_, device_, &command_queue_desc,
                                    &command_queue_));
}

L0Device::~L0Device() {
  L0_SAFE_CALL(zeContextDestroy(context_));
  L0_SAFE_CALL(zeCommandQueueDestroy(command_queue_));
}

void L0Device::execute_kernel() {
  llvm::LLVMContext context;
  auto module = std::make_unique<llvm::Module>("mod", context);
  module->setTargetTriple("spir64-unknown-unknown");
  auto &ctx = module->getContext();

  std::vector<llvm::Type *> args{llvm::Type::getInt32PtrTy(ctx)};
  llvm::FunctionType *func_type =
      llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), args, false);

  llvm::Function *func = llvm::Function::Create(
      func_type, llvm::GlobalValue::LinkageTypes::ExternalLinkage,
      "test_kernel", module.get());
  func->setCallingConv(llvm::CallingConv::SPIR_KERNEL);

  llvm::BasicBlock *entry = llvm::BasicBlock::Create(ctx, ".entry", func);

  llvm::IRBuilder<> b(ctx);
  b.SetInsertPoint(entry);

  llvm::Constant *one = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx),
                                               llvm::APInt(32, 1, true));
  llvm::Value *firstElem = b.CreateLoad(func->args().begin(), "ld");
  llvm::Value *result = b.CreateAdd(firstElem, one, "foo");
  b.CreateStore(result, func->args().begin());
  b.CreateRetVoid();

  std::stringstream ss;
  std::string err;

  auto success = llvm::writeSpirv(module.get(), ss, err);
  if (!success) {
    llvm::errs() << "Spirv translation failed with error: " << err << "\n";
  } else {
    llvm::errs() << "Spirv tranlsation success.\n";
  }

  auto code = ss.str();

  SPIRV::convertSpirv(ss, std::cout, err, false, true);
  
  ze_module_desc_t moduleDesc{
      .stype = ZE_STRUCTURE_TYPE_MODULE_DESC,
      .pNext = nullptr,
      .format = ZE_MODULE_FORMAT_IL_SPIRV,
      .inputSize = code.size(),
      .pInputModule = (const uint8_t *)code.data(),
      .pBuildFlags = "",
      .pConstants = nullptr,
  };
  ze_module_handle_t mod = nullptr;
  L0_SAFE_CALL(zeModuleCreate(context_, device_, &moduleDesc, &mod, nullptr));
  
  ze_kernel_desc_t kernel_desc{
      .stype = ZE_STRUCTURE_TYPE_KERNEL_DESC,
      .pNext = nullptr,
      .flags = 0,
      .pKernelName = "test_kernel",
  };
  ze_kernel_handle_t kernel = nullptr;
  L0_SAFE_CALL(zeKernelCreate(mod, &kernel_desc, &kernel));

  ze_command_list_desc_t command_list_desc = {
      ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC, nullptr, 0,
      0 // flags
  };

  ze_command_list_handle_t command_list;
  L0_SAFE_CALL(zeCommandListCreate(context_, device_, &command_list_desc,
                                   &command_list));

  ze_device_mem_alloc_desc_t alloc_desc{
      .stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC,
      .pNext = nullptr,
      .flags = 0,
      .ordinal = 0,
  };
  int32_t val = 5;

  void *mem;

  // allocate mem
  L0_SAFE_CALL(zeMemAllocDevice(context_, &alloc_desc, sizeof(int32_t),
                                0 /*align*/, device_, &mem));
  L0_SAFE_CALL(zeKernelSetArgumentValue(kernel, 0, sizeof(void *), &mem));

  // host to device
  L0_SAFE_CALL(zeCommandListAppendMemoryCopy(command_list, mem, &val,
                                             sizeof(int), nullptr, 0, nullptr));
  L0_SAFE_CALL(zeCommandListAppendBarrier(command_list, nullptr, 0, nullptr));

  // execute kernel
  L0_SAFE_CALL(zeKernelSetGroupSize(kernel, 1, 1, 1));
  ze_group_count_t dispatchTraits = {1, 1, 1};
  L0_SAFE_CALL(zeCommandListAppendLaunchKernel(
      command_list, kernel, &dispatchTraits, nullptr, 0, nullptr));
  L0_SAFE_CALL(zeCommandListAppendBarrier(command_list, nullptr, 0, nullptr));

  // device to host
  L0_SAFE_CALL(zeCommandListAppendMemoryCopy(command_list, &val, mem,
                                             sizeof(int), nullptr, 0, nullptr));
  L0_SAFE_CALL(zeCommandListAppendBarrier(command_list, nullptr, 0, nullptr));

  // execute command list
  L0_SAFE_CALL(zeCommandListClose(command_list));
  L0_SAFE_CALL(zeCommandQueueExecuteCommandLists(command_queue_, 1,
                                                 &command_list, nullptr));
  L0_SAFE_CALL(zeCommandQueueSynchronize(command_queue_, 10000));

  std::cout << val << std::endl;
}

std::vector<std::shared_ptr<Device>> get_devices() {
  zeInit(0);
  uint32_t driver_count = 0;
  zeDriverGet(&driver_count, nullptr);

  std::vector<ze_driver_handle_t> drivers(driver_count);
  zeDriverGet(&driver_count, drivers.data());

  std::vector<std::shared_ptr<Device>> res;
  for (auto driver : drivers) {
    uint32_t device_count = 0;
    zeDeviceGet(driver, &device_count, nullptr);

    std::vector<ze_device_handle_t> devices(device_count);
    zeDeviceGet(driver, &device_count, devices.data());

    for (auto device : devices) {
      res.push_back(std::make_shared<L0Device>(driver, device));
    }
  }

  return res;
}

} // namespace l0
