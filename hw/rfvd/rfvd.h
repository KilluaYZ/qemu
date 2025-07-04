/*
 * @Author: killuayz 2959243019@qq.com
 * @Date: 2025-04-30 10:38:54
 * @LastEditors: killuayz 2959243019@qq.com
 * @LastEditTime: 2025-05-07 10:21:50
 * @FilePath: /qemu/hw/rfvd/rfvd-pci.h
 * @Description: 定义了rust中的函数
 */

#ifndef RFVD_PCI_H
#define RFVD_PCI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// base
int32_t rfvd_init_logger(void);
#define ERROR_UNCLASSFIED -11
#define ERROR_NO_DEVICE -12
#define ERROR_NO_ELEMENT -13
#define ERROR_NULL_POINTER -14
#define ERROR_INVALID_FORMAT -15
#define ERROR_PARSE_FAILD -16
#define ERROR_OUT_OF_BOUND -17
#define ERROR_FILE_DIR_NOT_FOUND -18
#define ERROR_ENV_MISSING -19
#define ERROR_READ_FAILED -20
#define ERROR_WRITE_FAILED -21
#define ERROR_ATTR_NOT_SET -22

// device
int32_t rfvd_get_romfile(size_t pd, const char *out_str, size_t *out_len);
int32_t rfvd_get_name(size_t pd, const char *out_str, size_t *out_len);
int32_t rfvd_get_desc(size_t pd, const char *out_str, size_t *out_len);
int32_t rfvd_get_mems_num(size_t pd);
int32_t rfvd_load_from_env(void);

int32_t rfvd_load(const char *path);
int32_t rfvd_free(size_t pd);
// int32_t rfvd_check_irq_request(size_t pd);
int32_t rfvd_get_class_id(size_t pd);
int32_t rfvd_get_vid(size_t pd);
int32_t rfvd_get_pid(size_t pd);
int32_t rfvd_get_subvid(size_t pd);
int32_t rfvd_get_subpid(size_t pd);
int32_t rfvd_get_revison(size_t pd);

// 中断相关
#define RFVD_IRQ_GENERAL 1
#define RFVD_IRQ_DMA 2
uint32_t rfvd_get_irq_status(size_t pd);
// qemu端不应该有唤起中断的操作，唤起操作应该是在模拟设备端
// uint32_t rfvd_raise_irq_hw(size_t pd, uint32_t irq_status);
// qemu端只是查看并接受中断信号，进行对应处理之后，把信号掐灭
uint32_t rfvd_lower_irq_hw(size_t pd, uint32_t irq_status);
// uint32_t rfvd_irq_wlock_mask(size_t pd, uint32_t mask);
// uint32_t rfvd_irq_unwlock_mask(size_t pd, uint32_t mask);
// uint32_t rfvd_irq_rlock_mask(size_t pd, uint32_t mask);
// uint32_t rfvd_irq_unrlock_mask(size_t pd, uint32_t mask);

// DMA相关
#define RFVD_PCI_DMA_RUN 0x1
#define RFVD_PCI_DMA_DIR(cmd) (((cmd) & 0x2) >> 1)
#define RFVD_PCI_DMA_TO_PCI 0x0
#define RFVD_PCI_DMA_FROM_PCI 0x1
#define RFVD_PCI_DMA_DIR_TO_PCI 0x2

int64_t rfvd_get_dma_start(size_t pd);
int64_t rfvd_get_dma_size(size_t pd);
int64_t rfvd_get_dma_mask(size_t pd);
int64_t rfvd_get_dma_src(size_t pd);
int64_t rfvd_get_dma_dst(size_t pd);
int64_t rfvd_get_dma_cnt(size_t pd);
int64_t rfvd_get_dma_cmd(size_t pd);
int64_t rfvd_get_dma_buf(size_t pd, const char* out_buf, size_t *out_len);

// cust mems相关
// #define RFVD_CUST_MEMORY_TYPE_IO 1
// #define RFVD_CUST_MEMORY_TYPE_RAM 2
int64_t rfvd_get_mems_base(size_t pd, size_t mmd);
int64_t rfvd_get_mems_size(size_t pd, size_t mmd);
// int32_t rfvd_get_mems_type(size_t pd, size_t mmd);
int32_t rfvd_mems_read(size_t pd, size_t addr, size_t mmd, size_t size,
                       uint64_t *out_data);
int32_t rfvd_mems_write(size_t pd, size_t addr, size_t mmd, size_t size,
                        const uint64_t *in_data);

// pci
int32_t rfvd_pci_read(size_t pd, size_t addr, size_t bar, size_t size,
                      uint64_t *out_data);
int32_t rfvd_pci_write(size_t pd, size_t addr, size_t bar,
                       const uint64_t *val_data, size_t val_len);
int32_t rfvd_pci_get_bar_num(size_t pd);
int32_t rfvd_pci_get_bar_size(size_t pd, size_t bar);
int32_t rfvd_pci_get_bar_type(size_t pd, size_t bar);
int32_t rfvd_pci_get_msix_bar_idx(size_t pd);

#ifdef __cplusplus
}
#endif

#endif
