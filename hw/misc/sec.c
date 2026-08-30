/*
 * 简单的 XOR MMIO 设备
 *
 * 软件写入 DATA1 和 DATA2 后向 CMD 写 1 计算异或结果并触发中断，向 CMD 写 0 清零结果
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/misc/sec.h"
#include "hw/sysbus.h"

#define SEC_MMIO_SIZE 0x400

#define SEC_DATA1  0x00
#define SEC_DATA2  0x04
#define SEC_CMD    0x08
#define SEC_RESULT 0x0c
#define SEC_IRQ_STATUS 0x10

#define SEC_IRQ_PENDING BIT(0)

typedef struct SecState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;
    uint32_t data1;
    uint32_t data2;
    uint32_t cmd;
    uint32_t result;
    uint32_t irq_status;
} SecState;

OBJECT_DECLARE_SIMPLE_TYPE(SecState, SEC_DEVICE)

static uint64_t sec_read(void *opaque, hwaddr offset, unsigned size)
{
    SecState *s = opaque;

    switch (offset) {
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
    default:
        /* 保留寄存器读取为 0，便于后续扩展 register 空间 */
        return 0;
    }
}

static void sec_write(void *opaque, hwaddr offset, uint64_t value,
                      unsigned size)
{
    SecState *s = opaque;

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
    case SEC_RESULT:
        /* RESULT 由设备更新，忽略软件写入 */
        qemu_log_mask(LOG_GUEST_ERROR, "sec: RESULT is read-only\n");
        break;
    case SEC_IRQ_STATUS:
        /* bit 0 使用 W1C，清除后撤销 level-high 中断 */
        s->irq_status &= ~(value & SEC_IRQ_PENDING);
        qemu_set_irq(s->irq, !!s->irq_status);
        break;
    default:
        /* 1 KB 空间中的其余地址保留，忽略软件写入 */
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

static void sec_reset(DeviceState *dev)
{
    SecState *s = SEC_DEVICE(dev);

    s->data1 = 0;
    s->data2 = 0;
    s->cmd = 0;
    s->result = 0;
    s->irq_status = 0;
    qemu_set_irq(s->irq, 0);
}

static int sec_post_load(void *opaque, int version_id)
{
    SecState *s = opaque;

    qemu_set_irq(s->irq, !!s->irq_status);
    return 0;
}

static const VMStateDescription vmstate_sec = {
    .name = TYPE_SEC_DEVICE,
    .version_id = 2,
    .minimum_version_id = 1,
    .post_load = sec_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(data1, SecState),
        VMSTATE_UINT32(data2, SecState),
        VMSTATE_UINT32(cmd, SecState),
        VMSTATE_UINT32(result, SecState),
        VMSTATE_UINT32_V(irq_status, SecState, 2),
        VMSTATE_END_OF_LIST()
    },
};

static void sec_init(Object *obj)
{
    SecState *s = SEC_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &sec_ops, s, TYPE_SEC_DEVICE,
                          SEC_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void sec_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_sec;
    device_class_set_legacy_reset(dc, sec_reset);
}

static const TypeInfo sec_info = {
    .name = TYPE_SEC_DEVICE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SecState),
    .instance_init = sec_init,
    .class_init = sec_class_init,
};

static void sec_register_types(void)
{
    type_register_static(&sec_info);
}

type_init(sec_register_types)
