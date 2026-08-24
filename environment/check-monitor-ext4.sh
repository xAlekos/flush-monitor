#!/bin/sh
set -eu

if [ ! -e /dev/loop-control ]; then
    mknod /dev/loop-control c 10 237
fi
if [ ! -e /dev/loop0 ]; then
    mknod /dev/loop0 b 7 0
fi

mkdir -p /mnt/eviction-ext4
mount -o loop /work/environment/ext4-monitor.img /mnt/eviction-ext4
trap 'umount /mnt/eviction-ext4' EXIT

dd if=/dev/urandom of=/mnt/eviction-ext4/target.bin bs=4096 count=64 conv=fsync status=none

/work/environment/bin/flush /mnt/eviction-ext4/target.bin 0
for sample in 1 2 3 4; do
    echo "sample=$sample"
    /work/environment/bin/monitor-preadv2 /mnt/eviction-ext4/target.bin 0
done
