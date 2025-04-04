///
/// hardware model for amdgpu_sienna_cichlid
/// 2022 Yilun Wu<yiluwu@cs.stonybrook.edu>
/// 2021 Tong Zhang<ztong0001@gmail.com>
///

#include "HWModel.h"
#include <stdint.h>
class HWModel_amdgpu_sienna_cichlid : public HWModel {
public:
  HWModel_amdgpu_sienna_cichlid()
      : HWModel("amdgpu_sienna_cichlid", 0x1002, 0x73A0, 0, 0, 0x30000),
        probe_len(0) {
    setupBar({{PCI_BAR_TYPE_PIO, 16 * 1024},
              {PCI_BAR_TYPE_MMIO, 8 * 1024 * 1024},
              {PCI_BAR_TYPE_MMIO, 128 * 1024 * 1024},
              {PCI_BAR_TYPE_MMIO, 256 * 1024 * 1024},
              {PCI_BAR_TYPE_MMIO, 4 * 1024 * 1024},
              {PCI_BAR_TYPE_MMIO, 1024 * 1024}});
  }
  virtual ~HWModel_amdgpu_sienna_cichlid() {};
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
    LOG_TO_FILE("qemu_run.log", "HWModel_amdgpu_sienna_cichlid::read dest: 0x"
                                    << std::hex << (uint64_t)dest << std::dec
                                    << " addr: 0x" << std::hex << addr
                                    << std::dec << " size: " << size);
    INFO("HWModel_amdgpu_sienna_cichlid::read dest: 0x"
         << std::hex << (uint64_t)dest << std::dec << " addr: 0x" << std::hex
         << addr << std::dec << " size: " << size);
    // if (probe_len > 15)
    //   return 0;
    static int cnt_0x4 = 0;
    switch (addr) {
    case (0x58184):
      set_value(dest, 0x80000000, size);
      return size;
    case (0x4): {
      switch (cnt_0x4) {
      // drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.c:398
      case 0x0:
        set_value(dest, 0x28211407, size);
        break;
      // drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.c:412
      case 0x2:
        set_value(dest, 0x000a0000, size);
        break;
      case 0x4:
      case 0x5:
      case 0x7:
      case 0x9:
      case 0xb:
      case 0xd:
        set_value(dest, 0x00000000, size);
        break;
      case 0x3:
        set_value(dest, 0x00000044, size);
        break;
      // drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.c:426
      case 0x11:
        set_value(dest, 0x53445049, size);
        break;
      // drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.c:432
      case 0x12:
        set_value(dest, 0x00000000, size);
        break;
      // drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.c:1258
      case 0x14:
        set_value(dest, 0x00000001, size);
        break;
      // drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.c:1259
      case 0x15:
        set_value(dest, 0x00000056, size);
        break;
      // drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.c:1273
      case 0x16:
        set_value(dest, 0x000b0007, size);
        break;
      // / drivers / gpu / drm / amd / amdgpu / amdgpu_discovery.c:2220
      case 0x17:
        set_value(dest, 0x04090000, size);
        break;
      case 0x18:
        set_value(dest, 0x00280003, size);
        break;
      // / drivers / gpu / drm / amd / amdgpu / amdgpu_discovery.c:1761
      case 0x19:
        set_value(dest, 0x00060000, size);
        break;
      case 0x1a:
        set_value(dest, 0x00ff0000, size);
        break;
      case 0x1b:
        set_value(dest, 0x00090000, size);
        break;
      case 0x1c:
        set_value(dest, 0x00010000, size);
        break;
      case 0x1d:
        set_value(dest, 0x00090000, size);
        break;
      case 0x1e:
        set_value(dest, 0x002a0000, size);
        break;
      case 0x1f:
        set_value(dest, 0x00040000, size);
        break;
      case 0x20:
        set_value(dest, 0x000c0000, size);
        break;
      case 0x21:
        set_value(dest, 0x00010000, size);
        break;
      case 0x22:
        set_value(dest, 0x000f0000, size);
        break;
      case 0x23:
        set_value(dest, 0x000c0000, size);
        break;
      case 0x24:
        set_value(dest, 0x00000000, size);
        break;
      default:
        set_value(dest, 0x4 << 20 | cnt_0x4, size);
        break;
      }
      cnt_0x4++;
      return size;
    }
    case (0x378c):
      set_value(dest, 0x1, size);
      return size;
    case 0x3794:
      set_value(dest, 0x00000010, size);
      return size;
    default:
      break;
    }

    set_value(dest, addr, size);

    return size;

    // switch (addr) {
    // case (0x98):
    //   *((uint8_t *)dest) = 0x55;
    //   break;
    // case (0xa3):
    //   *((uint8_t *)dest) = 0xaa;
    //   break;
    // case (0x30):
    //   *((uint8_t *)dest) = '7';
    //   break;
    // case (0x31):
    //   *((uint8_t *)dest) = '6';
    //   break;
    // case (0x32):
    //   *((uint8_t *)dest) = '1';
    //   break;
    // case (0x33):
    //   *((uint8_t *)dest) = '2';
    //   break;
    // case (0x34):
    //   *((uint8_t *)dest) = '9';
    //   break;
    // case (0x35):
    //   *((uint8_t *)dest) = '5';
    //   break;
    // case (0x36):
    //   *((uint8_t *)dest) = '5';
    //   break;
    // case (0x37):
    //   *((uint8_t *)dest) = '2';
    //   break;
    // case (0x38):
    //   *((uint8_t *)dest) = '0';
    //   break;
    // case (0xc20c): {
    //   static int cnt;
    //   if (cnt < 2)
    //     *((uint32_t *)dest) = 0x0;
    //   if (cnt == 0)
    //     *((uint32_t *)dest) = 0x0;
    //   cnt++;
    //   break;
    // }
    // case (0xc91c): {
    //   static int cnt;
    //   if (cnt == 0)
    //     *((uint32_t *)dest) = 0x01010101;
    //   if (cnt == 1)
    //     *((uint32_t *)dest) = 0x0;
    //   if (cnt == 2)
    //     *((uint32_t *)dest) = 0x0;
    //   cnt++;
    //   break;
    // }
    // case (0xd200):
    //   *((uint32_t *)dest) = 0x0;
    //   break;
    // case (0xda00):
    //   *((uint32_t *)dest) = 0x0;
    //   break;
    // case (0xd010):
    //   *((uint32_t *)dest) = 0x0;
    //   break;
    // case (0xd848):
    //   *((uint32_t *)dest) = 0x0ff0000;
    //   break;
    // case (0xe60):
    //   *((uint32_t *)dest) = 0x0;
    //   break;
    // case (0xd048): {
    //   static int cnt;
    //   if (cnt == 0)
    //     *((uint32_t *)dest) = 0x0;
    //   cnt++;
    //   break;
    // }
    // default: {
    //   switch (size) {
    //   case (1):
    //     *((uint8_t *)dest) = 1;
    //     break;
    //   case (2):
    //     *((uint16_t *)dest) = 1;
    //     break;
    //   case (4):
    //     *((uint32_t *)dest) = 1;
    //     break;
    //   default:
    //     break;
    //   }
    // }
    // }

    // probe_len++;
    // return size;
  };
  virtual void write(uint64_t data, uint64_t addr, size_t size) final {};

private:
  int probe_len;
};
