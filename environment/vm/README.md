# Experiment VMs

The measurements use two fixed Ubuntu Jammy/ext4 images:

| profile | kernel |
|---|---|
| `reference` | `5.15.0-187-generic` |
| `modern` | `7.2.0-flushmon` |

The modern kernel is the vanilla Linux 7.2 source with local version
`flushmon`. The measured installation is recorded in
`data/raw/modern-kernel-7.2-vm-smoke.txt`.

The repository does not download or modify operating-system images. Keep these
fixed local assets in this directory:

```text
jammy-reference.qcow2
jammy-modern.qcow2
seed.img
id_ed25519
```

They are excluded from Git because the images are large. Archive them together
with the project when moving the experiment to another machine.

Build the small QEMU container once:

```sh
docker build -t eviction-notice-qemu:local environment/vm
```

Start a disposable VM and connect over SSH:

```sh
./environment/vm/start.sh modern
ssh -F /dev/null -i environment/vm/id_ed25519 -p 2222 eviction@127.0.0.1
```

Every run uses QEMU snapshot mode, so the fixed image is not modified. Stop it
with `docker stop eviction-notice-vm-modern` (or `-reference`).
