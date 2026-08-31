#ifndef HW_MISC_SEC_H
#define HW_MISC_SEC_H

#include "hw/qdev-core.h"
#include "qemu/typedefs.h"

#define TYPE_SEC_DEVICE "sec"

void sec_set_dma_address_space(DeviceState *dev, AddressSpace *as);

#endif
