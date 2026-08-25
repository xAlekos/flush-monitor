#!/bin/sh
set -eu

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
profile=${1:-modern}

case "$profile" in
	reference|modern) ;;
	*) echo "usage: $0 [reference|modern]" >&2; exit 2 ;;
esac

disk="$dir/jammy-$profile.qcow2"
test -f "$disk" || { echo "missing $disk" >&2; exit 1; }
test -f "$dir/seed.img" || { echo "missing $dir/seed.img" >&2; exit 1; }
test -c /dev/kvm || { echo "/dev/kvm is unavailable" >&2; exit 1; }

docker run -d --rm --pull=never \
	--name "eviction-notice-vm-$profile" \
	--privileged --security-opt label=disable \
	-p 127.0.0.1:2222:2222 \
	-e "VM_DISK=/vm/jammy-$profile.qcow2" \
	--mount "type=bind,src=$dir,dst=/vm" \
	eviction-notice-qemu:local /vm/qemu-start.sh
