#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
upstream="$root/artifact/Eviction-Notice/primitives"
tmp=$(mktemp -d /tmp/flush-monitor-check.XXXXXX)
trap 'rm -rf "$tmp"' EXIT

make -C "$root/victim"
make -C "$upstream" flush monitor-preadv2 read

for source in "$root"/environment/*.c; do
	name=${source##*/}
	gcc -O2 -Wall -Wextra -Werror "$source" -o "$tmp/${name%.c}"
done
for source in "$root"/demo/*.c; do
	name=${source##*/}
	gcc -O2 -Wall -Wextra -Werror "$source" -o "$tmp/${name%.c}"
done
for script in "$root"/environment/*.sh "$root"/environment/vm/*.sh \
	"$root"/demo/*.sh; do
	sh -n "$script"
done
PYTHONPYCACHEPREFIX="$tmp/pycache" python3 -m py_compile "$root"/experiments/*.py

MPLCONFIGDIR="$tmp" python3 "$root/experiments/analyze_sweep.py" \
	"$root/data/raw" "$tmp" "$tmp" >/dev/null
python3 "$root/experiments/analyze.py" "$root/data/raw" "$tmp" >/dev/null
python3 "$root/experiments/analyze_mitigation.py" \
	"$root/data/raw" "$tmp" >/dev/null

target="$root/victim/target.bin"
"$upstream/build/flush" "$target" 0 >/dev/null
"$root/victim/victim" run "$target" "$tmp/normal.csv" 1 0 >/dev/null
"$tmp/cache-state" "$target" | grep -q 'cache=0'

"$upstream/build/flush" "$target" 0 >/dev/null
"$root/victim/victim-constant" run "$target" "$tmp/constant.csv" 1 0 >/dev/null
"$tmp/cache-state" "$target" | grep -q 'cache=1'

echo "local check passed ($(uname -r))"
