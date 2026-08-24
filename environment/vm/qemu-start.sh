#!/bin/sh
set -eu

if [ ! -e /dev/kvm ]; then
    mknod /dev/kvm c 10 232
fi

vm_disk=${VM_DISK:-/vm/jammy-base.img}
case "${VM_SNAPSHOT:-on}" in
    on) snapshot_option=-snapshot ;;
    off) snapshot_option= ;;
    *)
        echo "VM_SNAPSHOT must be 'on' or 'off'" >&2
        exit 2
        ;;
esac

exec qemu-system-x86_64 \
    -machine accel=kvm \
    -cpu host \
    -smp 2 \
    -m 2048 \
    -nographic \
    $snapshot_option \
    -netdev user,id=net0,hostfwd=tcp:0.0.0.0:2222-:22 \
    -device virtio-net-pci,netdev=net0 \
    -drive "if=virtio,format=qcow2,file=$vm_disk" \
    -drive if=virtio,format=raw,readonly=on,file=/vm/seed.img
