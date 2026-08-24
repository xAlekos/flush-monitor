#!/bin/sh
set -eu

vm_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
profile=${1:-reference}
persistence=${2:-ephemeral}

case "$profile" in
    reference|modern) ;;
    *)
        echo "usage: $0 [reference|modern] [--persist]" >&2
        exit 2
        ;;
esac

case "$persistence" in
    ephemeral) snapshot=on ;;
    --persist) snapshot=off ;;
    *)
        echo "usage: $0 [reference|modern] [--persist]" >&2
        exit 2
        ;;
esac

disk="jammy-$profile.qcow2"
if [ ! -f "$vm_dir/$disk" ]; then
    docker run --rm --pull=never \
        --mount "type=bind,src=$vm_dir,dst=/vm" \
        --workdir /vm \
        eviction-notice-qemu:local \
        qemu-img create -f qcow2 -F qcow2 -b jammy-base.img "$disk"
fi

docker run -d --rm --pull=never \
    --name "eviction-notice-vm-$profile" \
    --privileged \
    -p 127.0.0.1:2222:2222 \
    -e "VM_DISK=/vm/$disk" \
    -e "VM_SNAPSHOT=$snapshot" \
    --mount "type=bind,src=$vm_dir,dst=/vm" \
    eviction-notice-qemu:local \
    /vm/qemu-start.sh
