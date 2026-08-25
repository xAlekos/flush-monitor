#!/bin/sh
set -eu

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
profile=${1:-modern}

disk="$dir/jammy-$profile.qcow2"

docker run -d --rm --pull=never \
	--name "eviction-notice-vm-$profile" \
	--privileged --security-opt label=disable \
	-p 127.0.0.1:2222:2222 \
	-e "VM_DISK=/vm/jammy-$profile.qcow2" \
	--mount "type=bind,src=$dir,dst=/vm" \
	eviction-notice-qemu:local /vm/qemu-start.sh
