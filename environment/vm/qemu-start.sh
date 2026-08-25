#!/bin/sh

test -e /dev/kvm || mknod /dev/kvm c 10 232

exec qemu-system-x86_64 \
	-machine accel=kvm -cpu host -smp 2 -m 2048 -nographic -snapshot \
	-netdev user,id=net0,hostfwd=tcp:0.0.0.0:2222-:22 \
	-device virtio-net-pci,netdev=net0 \
	-drive "if=virtio,format=qcow2,file=$VM_DISK" \
	-drive if=virtio,format=raw,readonly=on,file=/vm/seed.img
