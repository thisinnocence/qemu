# Build Linux

## Build Linux kernel

1. Down kernel source:  <https://www.kernel.org>
2. Build arm64 linux kernel machine

```bash
make CROSS_COMPILE=aarch64-linux-gnu- ARCH=arm64 O=build menuconfig -j32
  Device Drivers > Block devices
    <*>   RAM block device support
      (16)    Default number of RAM disks (NEW)
      (65536) Default RAM disk size (kbytes)

make CROSS_COMPILE=aarch64-linux-gnu- ARCH=arm64 O=build -j32
file build/arch/arm64/boot/Image
```

## Build initrd rootfs

1. Download busybox:  <https://busybox.net>
2. Build arm64 busybox

```bash
make CROSS_COMPILE=aarch64-linux-gnu- ARCH=arm64 menuconfig -j32
    Settings
    [*] Build static binary (no shared libs)

make CROSS_COMPILE=aarch64-linux-gnu- ARCH=arm64 install -j32
file _install/bin/busybox
```

Then, use `make_initrd.sh` to make initrd.
