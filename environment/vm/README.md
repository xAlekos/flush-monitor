# Matched VMs

Both profiles are copy-on-write overlays of the same Ubuntu image. They use the
same userspace, ext4 filesystem, QEMU machine, host CPU exposure, virtio devices,
2 vCPU, 2 GiB RAM and 4 GiB virtual disk. Only the guest kernel and its modules
differ:

- `reference`: Ubuntu kernel `5.15.0-1105-kvm`, compatible with the paper's
  non-destructive `preadv2(..., RWF_NOWAIT)` monitor;
- `modern`: host kernel `7.0.3-arch1-2`, used for the two modern alternatives.

The modern kernel is copied from the host Arch package into the otherwise
unchanged Ubuntu guest. Results therefore compare complete kernel conditions,
not isolated syscall overhead across identical kernel builds.

The VM runs through the local `eviction-notice-qemu:local` Docker image so QEMU
does not need to be installed on the host. Docker is only packaging QEMU; the
experiment executes under the guest kernel.

Start one profile and connect:

```sh
./environment/vm/start.sh reference
# or: ./environment/vm/start.sh modern
ssh -i environment/vm/id_ed25519 -p 2222 eviction@127.0.0.1
```

Normal runs discard runtime writes. `--persist` is only for provisioning an
overlay. Stop with `docker stop eviction-notice-vm-reference` or
`docker stop eviction-notice-vm-modern`; use `sudo poweroff` inside a persistent
guest so its ext4 filesystem is shut down cleanly.

The M3A target is `/var/tmp/eviction-monitor-target.bin`: one 4096-byte page,
mode `0644`, SHA-256
`ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7` in
both overlays. The benchmark binary was identical in both guests.

The verified Ubuntu image has SHA-256
`b405f78ac83209e09edc0798e81ea39f590d0047e72abd99ef24b627ad62b2e3`.
Generated images, keys, seed data, and static binaries are ignored by Git.
