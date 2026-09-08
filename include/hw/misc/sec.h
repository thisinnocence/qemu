#ifndef HW_MISC_SEC_H
#define HW_MISC_SEC_H

#include "hw/qdev-core.h"
#include "qemu/typedefs.h"

#define TYPE_SEC_DEVICE "sec"

#define SEC_MAX_VFS 4
#define SEC_VF_MMIO_SIZE 0x1000

void sec_set_dma_address_space(DeviceState *dev, unsigned vf, uint32_t sid,
                               AddressSpace *as);

#endif
