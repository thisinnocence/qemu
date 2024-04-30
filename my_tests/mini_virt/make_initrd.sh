#!/bin/bash

CURR_DIR=`pwd`
MOUNT_DIR=mnt
BUSYBOX_DIR=busybox

rm initrd.ext4
dd if=/dev/zero of=initrd.ext4 bs=1M count=32
mkfs.ext4 initrd.ext4

mkdir -p $MOUNT_DIR
mount initrd.ext4 $MOUNT_DIR
cp -arf $BUSYBOX_DIR/_install/* $MOUNT_DIR

cd $MOUNT_DIR
mkdir -p etc dev mnt proc sys tmp mnt etc/init.d/

echo "proc /proc proc defaults 0 0" > etc/fstab
echo "tmpfs /tmp tmpfs defaults 0 0" >> etc/fstab
echo "sysfs /sys sysfs defaults 0 0" >> etc/fstab

echo "#!/bin/sh" > etc/init.d/rcS
echo "mount -a" >> etc/init.d/rcS
echo "mount -o remount,rw /" >> etc/init.d/rcS
echo "echo -e \"Welcome to ARM64 Linux\"" >> etc/init.d/rcS
chmod 755 etc/init.d/rcS

echo "::sysinit:/etc/init.d/rcS" > etc/inittab
echo "::respawn:-/bin/sh" >> etc/inittab
echo "::askfirst:-/bin/sh" >> etc/inittab
chmod 755 etc/inittab

cd dev
mknod console c 5 1
mknod null c 1 3
mknod tty1 c 4 1

cd $CURR_DIR
umount $MOUNT_DIR
echo "make initrd ok!"
