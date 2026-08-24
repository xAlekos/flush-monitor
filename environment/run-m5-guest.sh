#!/bin/sh
set -eu

if [ "$#" -ne 1 ] || { [ "$1" != "reference" ] && [ "$1" != "modern" ]; }; then
    echo "usage: $0 reference|modern" >&2
    exit 2
fi

condition=$1
attacker=m5_attacker
victim=m5_victim
workdir=/var/tmp/m5
target=$workdir/target.bin
truth=$workdir/ground-truth-$condition.csv
attacker_log=$workdir/attacker-$condition.txt
export_prefix=/tmp/m5-$condition

for account in "$attacker" "$victim"; do
    if ! id -u "$account" >/dev/null 2>&1; then
        useradd --system --no-create-home --shell /usr/sbin/nologin "$account"
    fi
done

install -d -o "$victim" -g "$victim" -m 0755 "$workdir"
install -o "$victim" -g "$victim" -m 0644 /tmp/target.bin "$target"
# The freshly copied target is dirty in page cache; make it evictable before trial 1.
sync -f "$target"
install -o "$attacker" -g "$attacker" -m 0644 /dev/null "$attacker_log"
rm -f "$truth"

run_trial() {
    primitive=$1
    trial=$2
    event=$3

    sudo -u "$attacker" /tmp/flush "$target" 0 >/dev/null
    sudo -u "$victim" /tmp/victim-static run "$target" "$truth" "$trial" "$event" >/dev/null
    printf 'primitive=%s trial=%s\n' "$primitive" "$trial" |
        sudo -u "$attacker" tee -a "$attacker_log" >/dev/null

    if [ "$primitive" = "upstream" ]; then
        sudo -u "$attacker" /tmp/monitor-preadv2 "$target" 0 |
            sudo -u "$attacker" tee -a "$attacker_log" >/dev/null
    else
        sudo -u "$attacker" /tmp/monitor-modern-once "$primitive" "$target" |
            sudo -u "$attacker" tee -a "$attacker_log" >/dev/null
    fi
}

if [ "$condition" = "reference" ]; then
    run_trial upstream 1 0
    run_trial upstream 2 1
    run_trial upstream 3 0
    run_trial upstream 4 1
else
    run_trial flush 101 0
    run_trial flush 102 1
    run_trial flush 103 0
    run_trial flush 104 1
    run_trial dontcache 201 0
    run_trial dontcache 202 1
    run_trial dontcache 203 0
    run_trial dontcache 204 1
fi

if sudo -u "$attacker" test -r "$truth"; then
    ground_truth_readable=yes
else
    ground_truth_readable=no
fi

{
    printf 'condition=%s\n' "$condition"
    printf 'kernel=%s\n' "$(uname -r)"
    id "$attacker"
    id "$victim"
    stat -c 'target_owner=%U:%G target_mode=%a target_size=%s' "$target"
    stat -c 'truth_owner=%U:%G truth_mode=%a' "$truth"
    printf 'attacker_ground_truth_readable=%s\n' "$ground_truth_readable"
    sha256sum /tmp/victim-static /tmp/target.bin /tmp/flush
    if [ "$condition" = "reference" ]; then
        sha256sum /tmp/monitor-preadv2
    else
        sha256sum /tmp/monitor-modern-once
    fi
} > "$export_prefix-setup.txt"

if [ "$ground_truth_readable" != "no" ]; then
    echo "ground truth separation failed" >&2
    exit 1
fi

# Export only after the attacker-side experiment and access check have ended.
install -m 0644 "$attacker_log" "$export_prefix-attacker.txt"
install -m 0644 "$truth" "$export_prefix-ground-truth.csv"
