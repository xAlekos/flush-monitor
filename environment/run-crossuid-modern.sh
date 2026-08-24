#!/bin/sh
set -eu

attacker=m3a_attacker
victim=m3a_victim
target=/var/tmp/m3a-crossuid/target.bin

if ! id "$attacker" >/dev/null 2>&1; then
    sudo useradd --system --no-create-home --shell /usr/sbin/nologin "$attacker"
fi
if ! id "$victim" >/dev/null 2>&1; then
    sudo useradd --system --no-create-home --shell /usr/sbin/nologin "$victim"
fi

sudo install -d -m 0755 -o "$victim" -g "$victim" /var/tmp/m3a-crossuid
sudo -u "$victim" dd if=/dev/zero of="$target" bs=4096 count=1 status=none
sudo chmod 0644 "$target"
sync

uname -r
id "$attacker"
id "$victim"
stat -c 'target=%n owner=%U:%G mode=%a size=%s' "$target"

for mode in flush dontcache; do
    for trial in 1 2 3; do
        printf '\nmode=%s state=miss trial=%s\n' "$mode" "$trial"
        sudo -u "$attacker" /tmp/flush "$target" 0 >/dev/null
        printf 'after-attacker-flush '
        sudo -u "$victim" /tmp/cache-state "$target"
        sudo -u "$attacker" /tmp/monitor-modern-once "$mode" "$target"
        printf 'after-cleanup-immediate '
        sudo -u "$victim" /tmp/cache-state "$target"
        sleep 0.05
        printf 'after-cleanup-50ms '
        sudo -u "$victim" /tmp/cache-state "$target"

        printf '\nmode=%s state=hit trial=%s\n' "$mode" "$trial"
        sudo -u "$attacker" /tmp/flush "$target" 0 >/dev/null
        printf 'after-attacker-flush '
        sudo -u "$victim" /tmp/cache-state "$target"
        sudo -u "$victim" dd if="$target" of=/dev/null bs=1 count=1 status=none
        printf 'after-victim-read '
        sudo -u "$victim" /tmp/cache-state "$target"
        sudo -u "$attacker" /tmp/monitor-modern-once "$mode" "$target"
        printf 'after-cleanup '
        sudo -u "$victim" /tmp/cache-state "$target"
    done
done
