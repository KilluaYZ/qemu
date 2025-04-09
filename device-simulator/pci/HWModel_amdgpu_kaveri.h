///
/// hardware model for amdgpu_kaveri
/// 2022 Yilun Wu<yiluwu@cs.stonybrook.edu>
/// 2021 Tong Zhang<ztong0001@gmail.com>
///

#include "HWModel.h"
class HWModel_amdgpu_kaveri : public HWModel {
public:
  HWModel_amdgpu_kaveri()
      : HWModel("amdgpu_kaveri", 0x1002, 0x1304, 0, 0, 0x30000), probe_len(0) {
    setupBar({{PCI_BAR_TYPE_PIO, 16 * 1024},
              {PCI_BAR_TYPE_MMIO, 8 * 1024 * 1024},
              {PCI_BAR_TYPE_MMIO, 128 * 1024 * 1024},
              {PCI_BAR_TYPE_MMIO, 256 * 1024 * 1024},
              {PCI_BAR_TYPE_MMIO, 4 * 1024 * 1024},
              {PCI_BAR_TYPE_MMIO, 1024 * 1024}});
  }
  virtual ~HWModel_amdgpu_kaveri() {};
  virtual void restart_device() final { probe_len = 0; };

  void set_value(uint8_t *dest, uint64_t value, size_t size) {
    switch (size) {
    case (1):
      *((uint8_t *)dest) = (uint8_t)value;
      break;
    case (2):
      *((uint16_t *)dest) = (uint16_t)value;
      break;
    case (4):
      *((uint32_t *)dest) = (uint32_t)value;
      break;
    case (8):
      *((uint64_t *)dest) = (uint64_t)value;
      break;
    default:
      break;
    }
  }

  virtual int read(uint8_t *dest, uint64_t addr, size_t size) final {
    INFO("HWModel_amdgpu_kaveri::read dest: 0x"
         << std::hex << (uint64_t)dest << std::dec << " addr: 0x" << std::hex
         << addr << std::dec << " size: " << size);
    static int cnt_0x4 = 0;
    switch (addr) {
    case (0x4): {
      switch (cnt_0x4) {
      default:
        set_value(dest, 0x4 << 16 | cnt_0x4, size);
        break;
      }
      cnt_0x4++;
      return size;
    }
    default:
      break;
    }

    set_value(dest, addr, size);

    return size;
  };
  virtual void write(uint64_t data, uint64_t addr, size_t size) final {};

private:
  int probe_len;
};
