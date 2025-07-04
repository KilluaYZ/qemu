/*
 * QEMU RFuzzer Virtual Device PCI
 *
 * Copyright (c) 2012 Red Hat Inc.
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * @Author: killuayz 2959243019@qq.com
 * @Date: 2025-04-30 00:31:15
 * @LastEditors: killuayz 2959243019@qq.com
 * @LastEditTime: 2025-05-05 10:50:12
 * @FilePath: /qemu/hw/rfvd/rfvd-pci.c
 * @Description: RFuzzer Virtual Device PCI模拟设备
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "glib.h"
#include "qemu/osdep.h"
#include "hw/qdev-core.h"
#include "hw/pci/pci.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "rfvd.h"
#include "hw/irq.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"
#include "qemu/event_notifier.h"
#include "qemu/main-loop.h" // 添加BQL相关函数声明
#include "qemu/module.h"
#include "qemu/thread-posix.h"
#include "qemu/thread.h"
#include "qemu/timer.h"
#include "qemu/typedefs.h"
#include "qom/object.h"
#include "system/dma.h"
#include "system/kvm.h"
#include "qemu/log.h"

#define TYPE_RFVD_PCI_DEVICE "rfvd"
#define MAX_ROMFILE_LEN 4096
#define MAX_DEV_NAME_LEN 256
#define MAX_DEV_DESC_LEN 256
static int pd = -1;
#define PCI_PRODUCT_ID_HAPS_HSOTG 0xabc0
#define RFVD_BAR_CNT 6
#define DEV_NAME "RFVD_PCI"
#define DEV_DESC "RFVD PCI"

static int RFVD_ENABLE_DEBUG_LOG = false;
static void rfvd_debug_report(const char *fmt, ...) {
  if (RFVD_ENABLE_DEBUG_LOG) {
    va_list ap;
    va_start(ap, fmt);
    info_vreport(fmt, ap);
    va_end(ap);
  }
}

static void update_debug_log_flag_from_env() {
    const char *env = getenv("RFVD_ENABLE_QEMU_LOG");
    if (env) {
        RFVD_ENABLE_DEBUG_LOG = atoi(env);
    } else {
        RFVD_ENABLE_DEBUG_LOG = false;
    }
}

typedef struct RfvdBar {
  void *parent;
  int baridx;
  struct MemoryRegion iomem;
  uint8_t *bar;
} RfvdBar;

typedef struct RfvdPciState {
  PCIDevice pdev;

  // BAR定义
  RfvdBar bars[RFVD_BAR_CNT];

  // DMA定义
  struct dma_state {
    dma_addr_t src;
    dma_addr_t dst;
    dma_addr_t cnt;
    dma_addr_t cmd;
  } dma;
  uint64_t dma_start;
  uint64_t dma_size;
  QEMUTimer dma_timer;
  uint64_t dma_mask;
  uint8_t *dma_buf;
} RfvdPciState;

static const VMStateDescription rfvd_vmstate = {
    .name = TYPE_RFVD_PCI_DEVICE,
    .unmigratable = 1,
};

#define RFVD_PCI(obj) OBJECT_CHECK(RfvdPciState, (obj), TYPE_RFVD_PCI_DEVICE)

static void rfvd_dma_check_range(uint64_t xfer_start, uint64_t xfer_size,
                                 uint64_t dma_start, uint64_t dma_size) {
  uint64_t xfer_end = xfer_start + xfer_size;
  uint64_t dma_end = dma_start + dma_size;
  /*
   * 1. ensure we aren't overflowing
   * 2. ensure that xfer is within dma address range
   */
  if (dma_end >= dma_start && xfer_end >= xfer_start &&
      xfer_start >= dma_start && xfer_end <= dma_end) {
    return;
  }

  qemu_log_mask(LOG_GUEST_ERROR,
                "RFVD PCI: DMA range 0x%016" PRIx64 "-0x%016" PRIx64
                " out of bounds (0x%016" PRIx64 "-0x%016" PRIx64 ")!",
                xfer_start, xfer_end - 1, dma_start, dma_end - 1);
}

static dma_addr_t rfvd_dma_clamp_addr(RfvdPciState *rfvd, dma_addr_t addr) {
  // 虽然不知道这个dma_mask作用是什么，但是留着吧，先设置为全1
  dma_addr_t res = addr & rfvd->dma_mask;

  if (addr != res) {
    qemu_log_mask(LOG_GUEST_ERROR,
                  "RFVD PCI: clamping DMA 0x%016" PRIx64 " to 0x%016" PRIx64
                  "!",
                  addr, res);
    error_report("RFVD PCI: clamping DMA 0x%016" PRIx64 " to 0x%016" PRIx64 "!",
                 addr, res);
  }

  return res;
}

static int rfvd_pci_update_dma(RfvdPciState *rfvd) {
  int64_t dma_start = rfvd_get_dma_start(pd);
  if (dma_start == ERROR_NO_DEVICE) {
    error_report("[DMA] get dma_start failed : %ld", dma_start);
    return -1;
  }
  int64_t dma_size = rfvd_get_dma_size(pd);
  if (dma_size == ERROR_NO_DEVICE) {
    error_report("[DMA] get dma_size failed : %ld", dma_size);
    return -1;
  }
  int64_t dma_mask = rfvd_get_dma_mask(pd);
  if (dma_mask == ERROR_NO_DEVICE) {
    error_report("[DMA] get dma_mask failed : %ld", dma_mask);
    return -1;
  }
  int64_t dma_src = rfvd_get_dma_src(pd);
  if (dma_src == ERROR_NO_DEVICE) {
    error_report("[DMA] get dma_src failed : %ld", dma_src);
    return -1;
  }
  int64_t dma_dst = rfvd_get_dma_dst(pd);
  if (dma_dst == ERROR_NO_DEVICE) {
    error_report("[DMA] get dma_dst failed : %ld", dma_dst);
    return -1;
  }
  int64_t dma_cnt = rfvd_get_dma_cnt(pd);
  if (dma_cnt == ERROR_NO_DEVICE) {
    error_report("[DMA] get dma_cnt failed : %ld", dma_cnt);
    return -1;
  }
  int64_t dma_cmd = rfvd_get_dma_cnt(pd);
  if (dma_cmd == ERROR_NO_DEVICE) {
    error_report("[DMA] get dma_cmd failed : %ld", dma_cmd);
    return -1;
  }
  char *out_buf = malloc(dma_cnt + 5);
  if (out_buf < 0) {
    error_report("[DMA] malloc dma_buf failed");
    return -1;
  }
  size_t out_len = 0;
  int64_t ret = rfvd_get_dma_buf(pd, out_buf, &out_len);
  if (ret < 0) {
    error_report("[DMA] get dma_buf failed : %ld", ret);
    return -1;
  }

  // 确保获取成功，然后开始更新配置
  rfvd->dma_start = dma_start;
  rfvd->dma_size = dma_size;
  rfvd->dma_mask = dma_mask;
  rfvd->dma.cmd = dma_cmd;
  rfvd->dma.cnt = dma_cnt;
  rfvd->dma.dst = dma_dst;
  rfvd->dma.src = dma_src;

  if (rfvd->dma_buf) {
    free(rfvd->dma_buf);
    rfvd->dma_buf = NULL;
  }
  rfvd->dma_buf = (uint8_t *)out_buf;
  return 0;
}

// 会定时调用，用来处理dma读写
static void rfvd_dma_handler(RfvdPciState *rfvd) {
  info_report("rfvd raise dma irq");
  // 更新rfvd中的dma数据
  int ret = rfvd_pci_update_dma(rfvd);
  if (ret < 0) {
    error_report("rfvd update dma failed");
    return;
  }
  info_report("update dma object");

  if (RFVD_PCI_DMA_DIR(rfvd->dma.cmd) == RFVD_PCI_DMA_FROM_PCI) {
    uint64_t dst = rfvd->dma.dst;
    rfvd_dma_check_range(dst, rfvd->dma.cnt, rfvd->dma_start, rfvd->dma_size);
    dst -= rfvd->dma_start;
    MemTxResult ret =
        pci_dma_read(&rfvd->pdev, rfvd_dma_clamp_addr(rfvd, rfvd->dma.src),
                     rfvd->dma_buf + dst, rfvd->dma.cnt);
    info_report("pci_dma_read ret = %d", ret);
  } else {
    uint64_t src = rfvd->dma.src;
    rfvd_dma_check_range(src, rfvd->dma.cnt, rfvd->dma_start, rfvd->dma_size);
    src -= rfvd->dma_start;
    MemTxResult ret =
        pci_dma_write(&rfvd->pdev, rfvd_dma_clamp_addr(rfvd, rfvd->dma.dst),
                      rfvd->dma_buf + src, rfvd->dma.cnt);
    info_report("pci_dma_write ret = %d", ret);
  }

  info_report("finish handling dma");
}

static bool rfvd_msi_enabled(RfvdPciState *rfvd) {
  return msi_enabled(&rfvd->pdev);
}

static void rfvd_raise_irq(RfvdPciState *rfvd) {
  if (rfvd_msi_enabled(rfvd)) {
    msi_notify(&rfvd->pdev, 0);
  } else {
    pci_set_irq(&rfvd->pdev, 1);
  }
}

static void rfvd_lower_irq(RfvdPciState *rfvd, uint32_t val) {
  // 清除指定中断状态位
  rfvd_lower_irq_hw(pd, val);

  // 获取除了val标记的中断位上的读锁，因为val中断位已经有写锁了，因此不需要再次上锁
  // 由于rfvd_lower_irq调用频率低很多，所以获取整个irq的锁是可以接受的
  // rfvd_irq_rlock_mask(pd, ~val);
  // 从这里获取status
  uint32_t irq_status = rfvd_get_irq_status(pd);
  // 如果中断状态位为0并且msi没有启动，那么就使用pci_set_irq函数取消irq请求
  if (!irq_status && !rfvd_msi_enabled(rfvd)) {
    pci_set_irq(&rfvd->pdev, 0);
  }
  // rfvd_irq_unrlock_mask(pd, ~val);
}

static void rfvd_irq_handler_task(RfvdPciState *rfvd, uint32_t irq_task,
                                  void(cb)(RfvdPciState *)) {
  // 在执行中断处理程序之前，先为这个中断位上写锁
  // 表示，设备已经开始处理这个中断了，期间中断位不允许发生任何变化
  // rfvd_irq_wlock_mask(pd, irq_task);
  cb(rfvd);
  // 执行完中断处理程序之后，将中断位置0
  rfvd_lower_irq(rfvd, irq_task);
  // 执行结束之后，释放中断位上的写锁
  // rfvd_irq_unwlock_mask(pd, irq_task);
}

// 定时处理中断
static void rfvd_irq_handler(RfvdPciState *rfvd) {
  // 检查是否有中断，这里需要上锁
  uint32_t irq_status = rfvd_get_irq_status(pd);
  if (irq_status) {
    // 先启动pci的中断
    rfvd_raise_irq(rfvd);
    // 匹配中断响应程序并执行
    if (irq_status & RFVD_IRQ_DMA) {
      rfvd_irq_handler_task(rfvd, RFVD_IRQ_DMA, rfvd_dma_handler);
    }
  }
}

typedef struct RfvdWatchdogThreadDesc {
  RfvdPciState *rfvd;
  char *name;
  void (*cb)(RfvdPciState *);
  QemuThread *thread;
} RfvdWatchdogThreadDesc;

#define MAX_WATCHDOG_THREAD_SIZE 64
typedef struct RfvdWatchdogThreadManager {
  RfvdWatchdogThreadDesc *descrptions[MAX_WATCHDOG_THREAD_SIZE];
  int cnt;
} RfvdWatchdogThreadManager;

RfvdWatchdogThreadManager rfvd_watchdog_manager = {0};

static void *rfvd_watchdog_thread_handler(void *opaque) {
  RfvdWatchdogThreadDesc *desc = opaque;
  info_report("start watchdog process...");
  while (1) {
    usleep(1000);
    desc->cb(desc->rfvd);
  }
  return NULL;
}

static void start_rfvd_watchdog_thread(RfvdWatchdogThreadDesc desc) {
  info_report("enter start_rfvd_watchdog_thread");
  if (rfvd_watchdog_manager.cnt > MAX_WATCHDOG_THREAD_SIZE) {
    error_report(
        "you have started too many watchdog thread, it should be less than %d",
        MAX_WATCHDOG_THREAD_SIZE);
    return;
  }

  RfvdWatchdogThreadDesc *irq_desc =
      (RfvdWatchdogThreadDesc *)malloc(sizeof(RfvdWatchdogThreadDesc));
  irq_desc->rfvd = desc.rfvd;
  irq_desc->cb = desc.cb;
  irq_desc->name = desc.name;
  irq_desc->thread = (QemuThread *)malloc(sizeof(QemuThread));
  rfvd_watchdog_manager.descrptions[rfvd_watchdog_manager.cnt++] = irq_desc;
  info_report("watchdog manager cnt = %d", rfvd_watchdog_manager.cnt);
  info_report(
      "current start thread info : rfvd->%#x, cb->%#x, name->%s, thread->%#x",
      (void *)irq_desc->rfvd, (void *)irq_desc->cb, irq_desc->name,
      (void *)irq_desc->thread);

  qemu_thread_create(irq_desc->thread, irq_desc->name,
                     rfvd_watchdog_thread_handler, (void *)irq_desc,
                     QEMU_THREAD_DETACHED);
}

// 读写DMA寄存器，不过我应该会将其集成到device_simulator里面
// static void dma_reg_rw(RfvdPciState *rfvd, bool write, dma_addr_t *val,
//                    dma_addr_t *dma, bool timer) {
//   // 如果是DMA写，并且DMA这在运行，那么就直接返回，不进行任何处理
//   if (write && (rfvd->dma.cmd & RFVD_PCI_DMA_RUN)) {
//     return ;
//   }

//   if (write) {
//     *dma = *val;
//   } else {
//     *val = *dma;
//   }

//   if (timer) {
//     timer_mod(&rfvd->dma.timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
//   }
// }

// 自定义内存区域的读写函数
static uint64_t rfvd_custmem_read(void *opaque, hwaddr addr, unsigned size) {
  int mmd = (int)(uintptr_t)opaque;
  uint64_t val = 0;
  int ret = rfvd_mems_read(pd, addr, mmd, size, &val);
  if (ret < 0) {
    rfvd_debug_report("RFVD CUST MEM read error %d mem[%d] addr=%#lx", ret, mmd,
                 addr);
    return 0;
  }
  rfvd_debug_report("RFVD CUST MEM read mem[%d] addr=%#lx size=%d val=%#lx", mmd,
              addr, size, val);
  return val;
}

static void rfvd_custmem_write(void *opaque, hwaddr addr, uint64_t data,
                               unsigned size) {
  int mmd = (int)(uintptr_t)opaque;
  int ret = rfvd_mems_write(pd, addr, mmd, size, &data);
  if (ret < 0) {
    rfvd_debug_report("RFVD CUST MEM write error %d mem[%d] addr=%#lx", ret, mmd,
                 addr);
  }
  rfvd_debug_report("RFVD CUST MEM write mem[%d] addr=%#lx size=%d data=%#lx", mmd,
              addr, size, data);
}

static const MemoryRegionOps rfvd_custmem_ops = {
    .read = rfvd_custmem_read,
    .write = rfvd_custmem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl =
        {
            .min_access_size = 1,
            .max_access_size = 8,
        },
};

#define MMIO_CB(X)                                                             \
  static uint64_t rfvd_mmio_read##X(void *opaque, hwaddr addr,                 \
                                    unsigned size) {                           \
    RfvdBar *rfvdbar = opaque;                                                 \
    uint64_t read_val = 0;                                                     \
    int ret = rfvd_pci_read(pd, addr, X, size, &read_val);                     \
    rfvd_debug_report("MMIO read bar[%d] addr=%#lx size=%u val=%#lx",                \
                rfvdbar->baridx, addr, size, read_val);                        \
    if (ret < 0) {                                                             \
      rfvd_debug_report("RFVD PCI read error %d bar[%d] addr=%#lx", ret,            \
                   rfvdbar->baridx, addr);                                     \
      return 0;                                                                \
    }                                                                          \
    return read_val;                                                           \
  }                                                                            \
  static void rfvd_mmio_write##X(void *opaque, hwaddr addr, uint64_t data,     \
                                 unsigned size) {                              \
    RfvdBar *rfvdbar = opaque;                                                 \
    rfvd_debug_report("MMIO write bar[%d] addr=%#lx size=%u data=%#lx",              \
                rfvdbar->baridx, addr, size, data);                            \
    int ret = rfvd_pci_write(pd, addr, X, &data, size);                        \
    if (ret < 0) {                                                             \
      rfvd_debug_report("RFVD PCI write error %d bar[%d] addr=%#lx", ret,           \
                   rfvdbar->baridx, addr);                                     \
      return;                                                                  \
    }                                                                          \
  }

MMIO_CB(0)
MMIO_CB(1)
MMIO_CB(2)
MMIO_CB(3)
MMIO_CB(4)
MMIO_CB(5)

static const MemoryRegionOps rfvd_mmio_ops[] = {
    {.read = rfvd_mmio_read0,
     .write = rfvd_mmio_write0,
     .endianness = DEVICE_LITTLE_ENDIAN,
     .impl =
         {
             .min_access_size = 1,
             .max_access_size = 8,
         }},
    {.read = rfvd_mmio_read1,
     .write = rfvd_mmio_write1,
     .endianness = DEVICE_LITTLE_ENDIAN,
     .impl =
         {
             .min_access_size = 1,
             .max_access_size = 8,
         }},
    {.read = rfvd_mmio_read2,
     .write = rfvd_mmio_write2,
     .endianness = DEVICE_LITTLE_ENDIAN,
     .impl =
         {
             .min_access_size = 1,
             .max_access_size = 8,
         }},
    {.read = rfvd_mmio_read3,
     .write = rfvd_mmio_write3,
     .endianness = DEVICE_LITTLE_ENDIAN,
     .impl =
         {
             .min_access_size = 1,
             .max_access_size = 8,
         }},
    {.read = rfvd_mmio_read4,
     .write = rfvd_mmio_write4,
     .endianness = DEVICE_LITTLE_ENDIAN,
     .impl =
         {
             .min_access_size = 1,
             .max_access_size = 8,
         }},
    {.read = rfvd_mmio_read5,
     .write = rfvd_mmio_write5,
     .endianness = DEVICE_LITTLE_ENDIAN,
     .impl =
         {
             .min_access_size = 1,
             .max_access_size = 8,
         }},
};

// static void *rfvd_irq_handler(void *opaque) {
//   RfvdPciState *rfvd = opaque;
//   int irq_count = 0;
//   bool last_irq_state = false;
//   bool locked = false;

//   while (!rfvd->pdev.qdev.hotplugged) {
//     bool irq_requested = rfvd_check_irq_request(pd) > 0;

//     /* Only process if IRQ rfvd changed */
//     if (irq_requested != last_irq_state) {
//       if (!bql_locked()) {
//         bql_lock();
//         locked = true;
//       }

//       if (irq_requested) {
//         if (irq_count++ % 100 == 0) {
//           info_report("IRQ triggered (count=%d)", irq_count);
//         }
//         rfvd_raise_irq(rfvd, 0x1);
//       } else {
//         rfvd_lower_irq(rfvd, 0x1);
//       }

//       if (locked) {
//         bql_unlock();
//         locked = false;
//       }

//       last_irq_state = irq_requested;
//     }

//     /* Reduce polling frequency when no IRQ pending */
//     g_usleep(irq_requested ? 1000 : 10000);
//   }
//   info_report("IRQ handler exiting");
//   return NULL;
// }

static void rfvd_pci_realize(PCIDevice *pci_dev, Error **errp) {
  info_report("start rfvd_pci_realize ...");
  // 确保PCI配置空间已正确初始化
  pci_dev->config[PCI_STATUS] =
      (uint8_t)(PCI_STATUS_FAST_BACK | PCI_STATUS_DEVSEL_MEDIUM);
  pci_dev->config[PCI_CACHE_LINE_SIZE] = 0x08;
  pci_dev->config[PCI_INTERRUPT_LINE] = 0xff; /* unknown IRQ */
  pci_dev->config[PCI_CAPABILITY_LIST] = 0x00;
  if (pci_bus_is_express(pci_get_bus(pci_dev))) {
    pci_dev->config[PCI_CAPABILITY_LIST] = 0x80;
  }
  RfvdPciState *rfvd = RFVD_PCI(pci_dev);

  // 配置BAR
  info_report("Config BAR...");
  int pci_dev_bar_num = rfvd_pci_get_bar_num(pd);
  info_report("BAR num = %d", pci_dev_bar_num);
  if (pci_dev_bar_num < 0) {
    error_report("get bar num failed : %d", pci_dev_bar_num);
    exit(-1);
  }
  for (int i = 0; i < pci_dev_bar_num; i++) {
    RfvdBar *rfvdbar = &(rfvd->bars[i]);
    int bar_size = rfvd_pci_get_bar_size(pd, i);
    if (bar_size < 0) {
      error_report("get bar size failed : %d", bar_size);
      exit(-1);
    }
    if (bar_size == 0)
      continue;
    uint8_t *bar = (uint8_t *)calloc(bar_size, 1);
    if (!bar) {
      error_report("bar %d allocation failed!", i);
      exit(-1);
    }
    rfvdbar->parent = rfvd;
    rfvdbar->baridx = i;
    rfvdbar->bar = bar;
    int bartype = rfvd_pci_get_bar_type(pd, i);
    if (bartype < 0) {
      error_report("get bar type failed : %d", bartype);
      exit(-1);
    }
    info_report("rfvd allocated %s bar[%d] %d bytes",
                bartype == 0 ? "PIO" : "MMIO", i, bar_size);
    char name[64];
    sprintf(name, "rfvd-%d", i);
    memory_region_init_io(&rfvdbar->iomem, OBJECT(rfvd), &rfvd_mmio_ops[i],
                          rfvdbar, name, bar_size);

    if (bartype != 0) {
      // mmio
      pci_register_bar(pci_dev, i, PCI_BASE_ADDRESS_SPACE_MEMORY,
                       &rfvdbar->iomem);
    } else {
      // pio
      pci_register_bar(pci_dev, i, PCI_BASE_ADDRESS_SPACE_IO, &rfvdbar->iomem);
    }
  }

  // 配置自定义memory
  info_report("Config Custom Memory");
  int device_mems_num = rfvd_get_mems_num(pd);
  info_report("Cust mem num = %d", device_mems_num);
  // device_mems_num = 0;
  if (device_mems_num < 0) {
    error_report("get device_mems_num error code %d", device_mems_num);
  } else {
    info_report("Found %d device custom memories", device_mems_num);
    for (int mmd = 0; mmd < device_mems_num; mmd++) {
      int64_t cust_mem_size = rfvd_get_mems_size(pd, mmd);
      if (cust_mem_size < 0) {
        error_report("Device custom memory %d : get size failed %ld", mmd,
                     cust_mem_size);
        continue;
      }
      uint64_t cust_mem_base = rfvd_get_mems_base(pd, mmd);
      if ((int64_t)cust_mem_base == ERROR_NO_DEVICE ||
          (int64_t)cust_mem_base == ERROR_NO_ELEMENT) {
        error_report("Device custom memory %d : get base failed %ld", mmd,
                     (int64_t)cust_mem_base);
        continue;
      }
      // int32_t cust_mem_type = rfvd_get_mems_type(pd, mmd);
      // if (cust_mem_type < 0) {
      //   error_report("Device custom memory %d : get type failed %d", mmd,
      //                cust_mem_type);
      //   continue;
      // }
      // if (cust_mem_type > 2) {
      //   error_report("Device custom memory %d : unrecognized mem type %d",
      //   mmd,
      //                cust_mem_type);
      //   continue;
      // }
      MemoryRegion *cust_mem = g_new(MemoryRegion, 1);
      char name[32];
      snprintf(name, sizeof(name), "rfvd-custom-mem-%d", mmd);
      /* For both IO and RAM types, use memory_region_init_io to ensure all
       * accesses are hooked */
      memory_region_init_io(cust_mem, OBJECT(rfvd), &rfvd_custmem_ops,
                            (void *)(uintptr_t)mmd, name, cust_mem_size);

      // 为我们的custmem设置高优先级
      memory_region_add_subregion_overlap(get_system_memory(), cust_mem_base,
                                          cust_mem, INT32_MAX);
      info_report(
          "Successfully registed custom memory - name=%s base=%#lx size=%#lx",
          name, cust_mem_base, cust_mem_size);
    }
  }

  info_report("Config PCI Class");
  uint8_t *pci_conf = pci_dev->config;
  uint32_t class_id = rfvd_get_class_id(pd);
  if ((int64_t)class_id < 0) {
    error_report("get class id failed : %d", (int64_t)class_id);
    exit(-1);
  }
  info_report("RFVD PCI Class = %#x", class_id);
  uint8_t progif = class_id & 0xff;
  uint16_t pciclass = class_id >> 8;
  pci_config_set_class(pci_conf, pciclass);
  pci_config_set_prog_interface(pci_conf, progif);
  pci_config_set_interrupt_pin(pci_conf, 1);

  uint16_t rfvd_vid = rfvd_get_vid(pd);
  info_report("vid = %#x", rfvd_vid);
  if ((int32_t)rfvd_vid < 0) {
    error_report("get vid failed : %d", (int16_t)rfvd_vid);
    exit(-1);
  }
  uint16_t rfvd_pid = rfvd_get_pid(pd);
  info_report("pid = %#x", rfvd_pid);
  if ((int32_t)rfvd_pid < 0) {
    error_report("get pid failed : %d", (int16_t)rfvd_pid);
    exit(-1);
  }

  int32_t rfvd_revision = rfvd_get_revison(pd);
  info_report("revision = %#x", rfvd_revision);
  if (rfvd_revision < 0) {
    error_report("get revision failed : %d", rfvd_revision);
    exit(-1);
  }

  pci_config_set_vendor_id(pci_conf, rfvd_vid);
  pci_config_set_device_id(pci_conf, rfvd_pid);
  pci_config_set_revision(pci_conf, rfvd_revision);

  // rfvd->irq = pci_allocate_irq(pci_dev);
  if (msi_enabled(pci_dev)) {
    int ret = msi_init(pci_dev, 0xd0, 1, true, false, errp);
    if (ret && ret != -ENOTSUP) {
      error_report("Failed to initialize MSI: %d", ret);
      return;
    }
    info_report("MSI initialized successfully");
  } else {
    info_report("MSI is not enabled for this device");
    // 确保传统中断已正确设置
    pci_dev->config[PCI_INTERRUPT_PIN] = 1;
    pci_dev->config[PCI_INTERRUPT_LINE] = 0x01;
  }

  info_report("Create PCI-Express Setup");
  if (pci_bus_is_express(pci_get_bus(pci_dev))) {
    pci_dev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    assert(pcie_endpoint_cap_init(pci_dev, 0x80) > 0);
  } else {
    info_report(
        "RFVD is not connected to PCI Express bus, capability is limited");
  }

  info_report("Start irq handler thread");
  RfvdWatchdogThreadDesc irq_desc = {.rfvd = rfvd,
                                     .cb = rfvd_irq_handler,
                                     .thread = NULL,
                                     .name = "rfvd-irq-handler"};
  start_rfvd_watchdog_thread(irq_desc);

  info_report("PCI Realization Done");
  info_report("finish rfvd_pci_realize ...");
}

static volatile bool rfvd_thread_running = true;
static void rfvd_exit(PCIDevice *pci_dev) {
  info_report("start rfvd_exit ...");
  RfvdPciState *rfvd = RFVD_PCI(pci_dev);

  // 设置线程退出标志
  rfvd->pdev.qdev.hotplugged = true;
  rfvd_thread_running = false;

  // 释放所有分配的内存
  for (int i = 0; i < RFVD_BAR_CNT; i++) {
    RfvdBar *rfvdbar = &(rfvd->bars[i]);
    if (rfvdbar->bar) {
      free(rfvdbar->bar);
      rfvdbar->bar = NULL;
    }
  }

  // 如果使用了MSI，需要清理
  msi_uninit(pci_dev);

  if (rfvd->dma_buf) {
    free(rfvd->dma_buf);
    rfvd->dma_buf = NULL;
  }

  for (int i = 0; i < rfvd_watchdog_manager.cnt; i++) {
    RfvdWatchdogThreadDesc *desc = rfvd_watchdog_manager.descrptions[i];

    // if (desc->name) {
    //   free(desc->name);
    // }

    if (desc->thread) {
      free(desc->thread);
    }
  }

  info_report("finish rfvd_exit ...");
}

static void rfvd_class_init(ObjectClass *oc, void *data) {
  info_report("start rfvd_class_init ...");
  int init_logger_ret = rfvd_init_logger();
  if (init_logger_ret < 0) {
    error_report("init logger failed");
  } else {
    info_report("init logger success");
  }
  // 从环境变量中确定是否需要打印debug信息
  update_debug_log_flag_from_env();
  pd = rfvd_load_from_env();
  info_report("pd = %d", pd);
  if (pd < 0) {
    error_report("load pci device failed : %d", pd);
    exit(-1);
  }
  DeviceClass *dc = DEVICE_CLASS(oc);
  PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);
  uint16_t rfvd_vid = rfvd_get_vid(pd);
  info_report("vid = %#x", rfvd_vid);
  if ((int32_t)rfvd_vid < 0) {
    error_report("get vid failed : %d", (int32_t)rfvd_vid);
    exit(-1);
  }
  uint16_t rfvd_pid = rfvd_get_pid(pd);
  info_report("pid = %#x", rfvd_pid);
  if ((int32_t)rfvd_pid < 0) {
    error_report("get pid failed : %d", (int32_t)rfvd_pid);
    exit(-1);
  }

  pc->realize = rfvd_pci_realize;
  pc->exit = rfvd_exit;
  char *romfile_buf = malloc(MAX_ROMFILE_LEN);
  memset((void *)romfile_buf, 0, MAX_ROMFILE_LEN);
  size_t romfile_buf_len = 0;
  int romfile_ret = rfvd_get_romfile(pd, romfile_buf, &romfile_buf_len);
  info_report("romfile ret = %d len = %zu, romfile = %s", romfile_ret,
              romfile_buf_len, romfile_buf);
  if (romfile_ret < 0) {
    error_report("get romfile failed : %d", romfile_ret);
    if (romfile_ret != ERROR_ATTR_NOT_SET)
      exit(-1);
  }
  if (romfile_buf_len >= MAX_ROMFILE_LEN) {
    error_report("romfile path too long. exceed the limit %d", MAX_ROMFILE_LEN);
    exit(-1);
  }

  if (romfile_buf_len > 0) {
    pc->romfile = romfile_buf;
  } else {
    pc->romfile = NULL;
  }
  pc->vendor_id = rfvd_vid;
  pc->device_id = rfvd_pid;
  pc->revision = rfvd_get_revison(pd);
  info_report("revision = %#x", pc->revision);
  if ((int32_t)pc->revision < 0) {
    error_report("get revision failed : %d", (int32_t)pc->revision);
    exit(-1);
  }
  pc->subsystem_vendor_id = rfvd_get_subvid(pd);
  info_report("sub_vid = %#x", pc->subsystem_vendor_id);
  if ((int32_t)pc->subsystem_vendor_id < 0) {
    error_report("get sub_vid failed : %d", (int32_t)pc->subsystem_vendor_id);
    exit(-1);
  }
  pc->subsystem_id = rfvd_get_subpid(pd);
  info_report("sub_pid = %#x", pc->subsystem_id);
  if ((int32_t)pc->subsystem_id < 0) {
    error_report("get sub_pid failed : %d", (int32_t)pc->subsystem_id);
    exit(-1);
  }
  set_bit(DEVICE_CATEGORY_MISC, dc->categories);
  // char *dev_name = malloc(MAX_DEV_NAME_LEN);
  // memset((void *)dev_name, 0, MAX_DEV_NAME_LEN);
  // size_t dev_name_len = -1;
  // int dev_name_ret = rfvd_get_name(pd, dev_name, &dev_name_len);
  // info_report("devname ret = %d len = %zu, devname = %s", dev_name_ret,
  //             dev_name_len, dev_name);
  // if (dev_name_ret < 0) {
  //   error_report("get dev name failed : %d ", dev_name_ret);
  //   exit(-1);
  // }
  // if (dev_name_len >= MAX_DEV_NAME_LEN) {
  //   error_report("dev name too long. exceed the limit %d", MAX_DEV_NAME_LEN);
  //   exit(-1);
  // }
  // dc->name = DEV_NAME;
  // const char *dev_desc = malloc(MAX_DEV_DESC_LEN);
  // size_t dev_desc_len = 0;
  // int dev_desc_ret = rfvd_get_desc(pd, dev_desc, &dev_desc_len);
  // info_report("devdesc ret = %d len = %zu, desc = %s", dev_desc_ret,
  //             dev_desc_len, dev_desc);
  // if (dev_desc_ret < 0) {
  //   error_report("get dev desc failed : %d ", dev_desc_ret);
  //   exit(-1);
  // }
  // if (dev_desc_len >= MAX_DEV_NAME_LEN) {
  //   error_report("dev desc too long. exceed the limit %d", MAX_DEV_DESC_LEN);
  //   exit(-1);
  // }
  dc->desc = DEV_DESC;
  dc->vmsd = &rfvd_vmstate;
  info_report("finish rfvd_class_init ...");
}

static void rfvd_instance_init(Object *obj) {
  // info_report("start rfvd_instance_init ...");
  // // 初始化实例的属性
  // RfvdPciState *rfvd = RFVD_PCI(obj);
  // // 确保所有成员被正确初始化
  // memset(rfvd->bars, 0, sizeof(rfvd->bars));
  // info_report("finish rfvd_instance_init ...");
}

static void rfvd_pci_register_types(void) {
  static InterfaceInfo interfaces[] = {
      {INTERFACE_PCIE_DEVICE},
      {},
  };

  static const TypeInfo rfvd_info = {
      .name = TYPE_RFVD_PCI_DEVICE,
      .parent = TYPE_PCI_DEVICE,
      .instance_size = sizeof(RfvdPciState),
      .class_init = rfvd_class_init,
      .instance_init = rfvd_instance_init,
      .interfaces = interfaces,
  };

  type_register_static(&rfvd_info);
  printf("RFVD device registered successfully\n");
}

type_init(rfvd_pci_register_types);
