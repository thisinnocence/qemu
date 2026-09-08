/*
 * 包含独立 VF 的 XOR 和 DMA MMIO 设备
 *
 * PIO path 计算 XOR
 * DMA path 经 machine 提供的 AddressSpace 复制小块数据
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/misc/sec.h"
#include "hw/sysbus.h"
#include "system/dma.h"


#define SEC_DATA1  0x00
#define SEC_DATA2  0x04
#define SEC_CMD    0x08
#define SEC_RESULT 0x0c
#define SEC_IRQ_STATUS 0x10
#define SEC_DMA_SRC_LO 0x14
#define SEC_DMA_SRC_HI 0x18
#define SEC_DMA_DST_LO 0x1c
#define SEC_DMA_DST_HI 0x20
#define SEC_DMA_LEN 0x24
#define SEC_DMA_CMD 0x28
#define SEC_DMA_STATUS 0x2c
#define SEC_VF_ID 0x30
#define SEC_SID 0x34
#define SEC_RESET 0x38

#define SEC_IRQ_PENDING BIT(0)
#define SEC_DMA_DONE BIT(0)
#define SEC_DMA_ERROR BIT(1)
#define SEC_DMA_MAX_LEN 256

typedef struct SecVF {
    uint32_t id;
    uint32_t sid;
    MemoryRegion mmio;
    qemu_irq irq;
    uint32_t data1;
    uint32_t data2;
    uint32_t cmd;
    uint32_t result;
    uint32_t irq_status;
    uint64_t dma_src;
    uint64_t dma_dst;
    uint32_t dma_len;
    uint32_t dma_cmd;
    uint32_t dma_status;
    AddressSpace *dma_as;
} SecVF;

typedef struct SecState {
    SysBusDevice parent_obj;
    uint32_t num_vfs;
    SecVF vf[SEC_MAX_VFS];
} SecState;

OBJECT_DECLARE_SIMPLE_TYPE(SecState, SEC_DEVICE)

void sec_set_dma_address_space(DeviceState *dev, unsigned vf, uint32_t sid,
                               AddressSpace *as)
{
    SecState *s = SEC_DEVICE(dev);

    assert(!dev->realized && vf < SEC_MAX_VFS);
    s->vf[vf].dma_as = as;
    s->vf[vf].sid = sid;
}

static void sec_vf_reset(SecVF *s);

static void sec_dma_copy(SecVF *s)
{
    g_autofree uint8_t *buf = NULL;
    MemTxResult result;

    s->dma_status = 0;
    if (!s->dma_as || !s->dma_len || s->dma_len > SEC_DMA_MAX_LEN) {
        s->dma_status = SEC_DMA_ERROR;
        goto out;
    }

    buf = g_malloc(s->dma_len);
    result = dma_memory_read(s->dma_as, s->dma_src, buf, s->dma_len,
                             MEMTXATTRS_UNSPECIFIED);
    if (result != MEMTX_OK) {
        s->dma_status = SEC_DMA_ERROR;
        goto out;
    }

    result = dma_memory_write(s->dma_as, s->dma_dst, buf, s->dma_len,
                              MEMTXATTRS_UNSPECIFIED);
    s->dma_status = result == MEMTX_OK ? SEC_DMA_DONE : SEC_DMA_ERROR;

out:
    s->irq_status |= SEC_IRQ_PENDING;
    qemu_set_irq(s->irq, 1);
}

static uint64_t sec_read(void *opaque, hwaddr offset, unsigned size)
{
    SecVF *s = opaque;

    switch (offset) {
    case SEC_VF_ID:
        return s->id;
    case SEC_SID:
        return s->sid;
    case SEC_DATA1:
        return s->data1;
    case SEC_DATA2:
        return s->data2;
    case SEC_CMD:
        return s->cmd;
    case SEC_RESULT:
        return s->result;
    case SEC_IRQ_STATUS:
        return s->irq_status;
    case SEC_DMA_SRC_LO:
        return extract64(s->dma_src, 0, 32);
    case SEC_DMA_SRC_HI:
        return extract64(s->dma_src, 32, 32);
    case SEC_DMA_DST_LO:
        return extract64(s->dma_dst, 0, 32);
    case SEC_DMA_DST_HI:
        return extract64(s->dma_dst, 32, 32);
    case SEC_DMA_LEN:
        return s->dma_len;
    case SEC_DMA_CMD:
        return s->dma_cmd;
    case SEC_DMA_STATUS:
        return s->dma_status;
    default:
        /* 保留寄存器读取为 0，便于后续扩展 register 空间 */
        return 0;
    }
}

static void sec_write(void *opaque, hwaddr offset, uint64_t value,
                      unsigned size)
{
    SecVF *s = opaque;

    switch (offset) {
    case SEC_DATA1:
        s->data1 = value;
        break;
    case SEC_DATA2:
        s->data2 = value;
        break;
    case SEC_CMD:
        s->cmd = value;
        if (s->cmd == 1) {
            s->result = s->data1 ^ s->data2;
            s->irq_status |= SEC_IRQ_PENDING;
            qemu_set_irq(s->irq, 1);
        } else if (s->cmd == 0) {
            s->result = 0;
        }
        break;
    case SEC_RESET:
        if (value == 1) {
            sec_vf_reset(s);
        }
        break;
    case SEC_VF_ID:
    case SEC_SID:
    case SEC_RESULT:
        /* 结果与身份寄存器由设备提供，忽略软件写入 */
        qemu_log_mask(LOG_GUEST_ERROR, "sec: register is read-only\n");
        break;
    case SEC_IRQ_STATUS:
        /* bit 0 使用 W1C，清除后撤销 level-high 中断 */
        s->irq_status &= ~(value & SEC_IRQ_PENDING);
        qemu_set_irq(s->irq, !!s->irq_status);
        break;
    case SEC_DMA_SRC_LO:
        s->dma_src = deposit64(s->dma_src, 0, 32, value);
        break;
    case SEC_DMA_SRC_HI:
        s->dma_src = deposit64(s->dma_src, 32, 32, value);
        break;
    case SEC_DMA_DST_LO:
        s->dma_dst = deposit64(s->dma_dst, 0, 32, value);
        break;
    case SEC_DMA_DST_HI:
        s->dma_dst = deposit64(s->dma_dst, 32, 32, value);
        break;
    case SEC_DMA_LEN:
        s->dma_len = value;
        break;
    case SEC_DMA_CMD:
        s->dma_cmd = value;
        if (s->dma_cmd == 1) {
            sec_dma_copy(s);
        }
        break;
    case SEC_DMA_STATUS:
        s->dma_status &= ~value;
        break;
    default:
        /* 4 KB 空间中的其余地址保留，忽略软件写入 */
        break;
    }
}

static const MemoryRegionOps sec_ops = {
    .read = sec_read,
    .write = sec_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void sec_vf_reset(SecVF *s)
{
    s->data1 = 0;
    s->data2 = 0;
    s->cmd = 0;
    s->result = 0;
    s->irq_status = 0;
    s->dma_src = 0;
    s->dma_dst = 0;
    s->dma_len = 0;
    s->dma_cmd = 0;
    s->dma_status = 0;
    qemu_set_irq(s->irq, 0);
}

static int sec_post_load(void *opaque, int version_id)
{
    SecVF *s = opaque;

    qemu_set_irq(s->irq, !!s->irq_status);
    return 0;
}

static const VMStateDescription vmstate_sec_vf = {
    .name = "sec/vf",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = sec_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_EQUAL(id, SecVF, NULL),
        VMSTATE_UINT32_EQUAL(sid, SecVF, NULL),
        VMSTATE_UINT32(data1, SecVF),
        VMSTATE_UINT32(data2, SecVF),
        VMSTATE_UINT32(cmd, SecVF),
        VMSTATE_UINT32(result, SecVF),
        VMSTATE_UINT32(irq_status, SecVF),
        VMSTATE_UINT64(dma_src, SecVF),
        VMSTATE_UINT64(dma_dst, SecVF),
        VMSTATE_UINT32(dma_len, SecVF),
        VMSTATE_UINT32(dma_cmd, SecVF),
        VMSTATE_UINT32(dma_status, SecVF),
        VMSTATE_END_OF_LIST()
    },
};

/* 多 VF 改变了板级硬件 ABI，不接受旧版单 VF migration stream */
static const VMStateDescription vmstate_sec = {
    .name = TYPE_SEC_DEVICE,
    .version_id = 4,
    .minimum_version_id = 4,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_EQUAL(num_vfs, SecState, NULL),
        VMSTATE_STRUCT_ARRAY(vf, SecState, SEC_MAX_VFS, 4,
                             vmstate_sec_vf, SecVF),
        VMSTATE_END_OF_LIST()
    },
};

static void sec_reset(DeviceState *dev)
{
    SecState *s = SEC_DEVICE(dev);

    for (unsigned i = 0; i < s->num_vfs; i++) {
        sec_vf_reset(&s->vf[i]);
    }
}

static void sec_realize(DeviceState *dev, Error **errp)
{
    SecState *s = SEC_DEVICE(dev);

    if (!s->num_vfs || s->num_vfs > SEC_MAX_VFS) {
        error_setg(errp, "sec: num-vfs must be between 1 and %u", SEC_MAX_VFS);
        return;
    }
    for (unsigned i = 0; i < s->num_vfs; i++) {
        if (!s->vf[i].dma_as) {
            error_setg(errp, "sec: VF %u has no DMA AddressSpace", i);
            return;
        }
    }
    for (unsigned i = 0; i < s->num_vfs; i++) {
        SecVF *vf = &s->vf[i];
        g_autofree char *name = g_strdup_printf("sec-vf%u", i);

        vf->id = i;
        memory_region_init_io(&vf->mmio, OBJECT(dev), &sec_ops, vf, name,
                              SEC_VF_MMIO_SIZE);
        sysbus_init_mmio(SYS_BUS_DEVICE(dev), &vf->mmio);
        sysbus_init_irq(SYS_BUS_DEVICE(dev), &vf->irq);
    }
}

static const Property sec_properties[] = {
    DEFINE_PROP_UINT32("num-vfs", SecState, num_vfs, 4),
};

static void sec_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sec_realize;
    dc->user_creatable = false;
    device_class_set_props(dc, sec_properties);
    dc->vmsd = &vmstate_sec;
    device_class_set_legacy_reset(dc, sec_reset);
}

static const TypeInfo sec_info = {
    .name = TYPE_SEC_DEVICE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SecState),
    .class_init = sec_class_init,
};

static void sec_register_types(void)
{
    type_register_static(&sec_info);
}

type_init(sec_register_types)
