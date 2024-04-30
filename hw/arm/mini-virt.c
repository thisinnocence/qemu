#include "qemu/osdep.h"
#include "qemu/typedefs.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qapi/qmp/qlist.h"
#include "exec/address-spaces.h"
#include "hw/qdev-properties.h"
#include "hw/boards.h"
#include "hw/arm/bsa.h"   // Common definitions for Arm Base System Architecture (BSA) platforms
#include "hw/arm/boot.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/char/pl011.h"
#include "sysemu/sysemu.h"
#include "target/arm/cpu.h"
#include "target/arm/internals.h"

#define NUM_IRQS 256  // Number of external interrupt lines to configure the GIC with

enum {VIRT_MEM, VIRT_UART, VIRT_GIC_DIST, VIRT_GIC_REDIST};

static MemMapEntry memmap[] = {
    [VIRT_MEM]        = { GiB, 4 * GiB},
    [VIRT_UART]       = { 0x09000000, 0x00001000 },
    [VIRT_GIC_DIST]   = { 0x08000000, 0x00010000 },
    [VIRT_GIC_REDIST] = { 0x080A0000, 0x00F60000 },
};

static const int irqmap[] = {
    [VIRT_UART] = 1
};

struct MiniVirtMachineClass {
    MachineClass parent;
};
struct MiniVirtMachineState {
    MachineState parent;
    Notifier machine_done;
    MemMapEntry *memmap;
    const int *irqmap;
    DeviceState *gic;
    MemoryRegion ram;
    struct arm_boot_info bootinfo;
};

#define TYPE_MINI_VIRT_MACHINE   MACHINE_TYPE_NAME("mini-virt")
OBJECT_DECLARE_TYPE(MiniVirtMachineState, MiniVirtMachineClass, MINI_VIRT_MACHINE)

static void create_gic(MiniVirtMachineState *vms, MemoryRegion *mem)
{
    MachineState *ms = MACHINE(vms);
    unsigned int smp_cpus = ms->smp.cpus;

    vms->gic = qdev_new(gicv3_class_name());
    qdev_prop_set_uint32(vms->gic, "revision", 3);
    qdev_prop_set_uint32(vms->gic, "num-cpu", smp_cpus);
    qdev_prop_set_uint32(vms->gic, "num-irq", NUM_IRQS + 32); // 0~31 is SGI/PPI

    uint32_t redist0_capacity = (vms->memmap[VIRT_GIC_REDIST].size / GICV3_REDIST_SIZE);
    uint32_t redist0_count = MIN(smp_cpus, redist0_capacity);
    QList *redist_region_count = qlist_new();
    qlist_append_int(redist_region_count, redist0_count);
    qdev_prop_set_array(vms->gic, "redist-region-count", redist_region_count);

    SysBusDevice *gicbusdev = SYS_BUS_DEVICE(vms->gic);
    sysbus_realize_and_unref(gicbusdev, &error_fatal);
    sysbus_mmio_map(gicbusdev, 0, vms->memmap[VIRT_GIC_DIST].base);
    sysbus_mmio_map(gicbusdev, 1, vms->memmap[VIRT_GIC_REDIST].base);

    for (int i = 0; i < smp_cpus; i++) {
        DeviceState *cpudev = DEVICE(qemu_get_cpu(i));
        int intidbase = NUM_IRQS + i * GIC_INTERNAL;
        qdev_connect_gpio_out(cpudev, 0, qdev_get_gpio_in(vms->gic, intidbase + ARCH_TIMER_NS_EL1_IRQ)); // arch timer
        sysbus_connect_irq(gicbusdev, i, qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
    }
}

static void create_cpu(MachineState *machine)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    unsigned int max_cpus = machine->smp.max_cpus;
    const CPUArchIdList *possible_cpus = mc->possible_cpu_arch_ids(machine);
    assert(possible_cpus->len == max_cpus);
    for (int i = 0; i < possible_cpus->len; i++) {
        Object *cpuobj = object_new(possible_cpus->cpus[i].type);
        object_property_set_int(cpuobj, "mp-affinity", possible_cpus->cpus[i].arch_id, NULL);
        CPUState *cs = CPU(cpuobj);
        cs->cpu_index = i;
        qdev_realize(DEVICE(cpuobj), NULL, &error_fatal);
        object_unref(cpuobj);
    }
}

static void create_ram(MiniVirtMachineState *vms, MemoryRegion *sysmem)
{
    memory_region_init_ram(&vms->ram, NULL, "ram", memmap[VIRT_MEM].size, &error_fatal);
    memory_region_add_subregion(sysmem, memmap[VIRT_MEM].base, &vms->ram);
}

static void create_uart(const MiniVirtMachineState *vms, MemoryRegion *sysmem)
{
    hwaddr base = vms->memmap[VIRT_UART].base;
    int irq = vms->irqmap[VIRT_UART];
    DeviceState *dev = qdev_new(TYPE_PL011);
    qdev_prop_set_chr(dev, "chardev", serial_hd(0));

    SysBusDevice *s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, base);
    sysbus_connect_irq(s, 0, qdev_get_gpio_in(vms->gic, irq));
}

static const CPUArchIdList *virt_possible_cpu_arch_ids(MachineState *ms)
{
    unsigned int max_cpus = ms->smp.max_cpus;
    if (ms->possible_cpus) {
        assert(ms->possible_cpus->len == max_cpus);
        return ms->possible_cpus;
    }
    ms->possible_cpus = g_malloc0(sizeof(CPUArchIdList) + sizeof(CPUArchId) * max_cpus);
    ms->possible_cpus->len = max_cpus;
    for (int i = 0; i < ms->possible_cpus->len; i++) {
        ms->possible_cpus->cpus[i].type = ms->cpu_type;
        ms->possible_cpus->cpus[i].arch_id = arm_cpu_mp_affinity(i, GICV3_TARGETLIST_BITS);
    }
    return ms->possible_cpus;
}

static void virt_machine_done(Notifier *notifier, void *data)
{
    MiniVirtMachineState *vms = container_of(notifier, MiniVirtMachineState, machine_done);
    MachineState *ms = MACHINE(vms);
    ARMCPU *cpu = ARM_CPU(first_cpu);
    struct arm_boot_info *info = &vms->bootinfo;
    AddressSpace *as = arm_boot_address_space(cpu, info);
    int ret = arm_load_dtb(info->dtb_start, info, info->dtb_limit, as, ms);
    assert(ret > 0);
}

static void mach_virt_init(MachineState *machine)
{
    MiniVirtMachineState *vms = MINI_VIRT_MACHINE(machine);
    vms->memmap = memmap;
    vms->irqmap = irqmap;
    MemoryRegion *sysmem = get_system_memory();

    create_cpu(machine);
    create_ram(vms, sysmem);
    create_gic(vms, sysmem);
    create_uart(vms, sysmem);

    vms->bootinfo.ram_size = machine->ram_size;
    vms->bootinfo.loader_start = vms->memmap[VIRT_MEM].base;
    vms->bootinfo.skip_dtb_autoload = true;
    vms->bootinfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC; // for boot secondary cpu-core

    arm_load_kernel(ARM_CPU(first_cpu), machine, &vms->bootinfo); // load kernel
    vms->machine_done.notify = virt_machine_done; // load dtb
    qemu_add_machine_init_done_notifier(&vms->machine_done);
}

static void virt_instance_init(Object *obj) {}

static void virt_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->init = mach_virt_init;
    mc->desc = "mini-virt ARM Machine";
    mc->max_cpus = 512;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a57");
    mc->possible_cpu_arch_ids = virt_possible_cpu_arch_ids;
}

static const TypeInfo mini_virt_machine_info = {
    .name          = TYPE_MINI_VIRT_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(MiniVirtMachineState),
    .class_size    = sizeof(MiniVirtMachineClass),
    .class_init    = virt_machine_class_init,
    .instance_init = virt_instance_init,
};

static void mach_virt_machine_init(void)
{
    type_register_static(&mini_virt_machine_info);
}
type_init(mach_virt_machine_init);
