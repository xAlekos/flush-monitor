#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
image=flush-monitor-victim-builder:local
output="$root/environment/bin"

if ! docker image inspect "$image" >/dev/null 2>&1; then
    docker build -t "$image" "$root/victim"
fi
mkdir -p "$output"

docker run --rm --pull=never --security-opt label=disable \
    --user "$(id -u):$(id -g)" \
    --mount "type=bind,src=$root,dst=/work" \
    --workdir /work "$image" sh -eu -c '
        primitive=/work/artifact/Eviction-Notice/primitives
        common="$primitive/utils/GeneralUtils.c /work/artifact/Eviction-Notice/utilities/AlignedPage.c"
        includes="-I$primitive/include -I/work/artifact/Eviction-Notice/utilities/include"
        cc -O2 -Wall -Wextra -pedantic -Wno-unused-parameter -static \
            -DFLUSH_THRESHOLD=0.000000900 $includes \
            "$primitive/flush/flush.c" $common -o /work/environment/bin/flush
        cc -O2 -Wall -Wextra -pedantic -Wno-unused-parameter -static \
            -DMON_PREADV2 $includes "$primitive/monitor/preadv2.c" \
            "$primitive/monitor/monitor.c" $common \
            -o /work/environment/bin/monitor-preadv2
		for name in monitor-modern-once window-sweep-worker; do
			cc -O2 -Wall -Wextra -static "/work/environment/$name.c" \
				-o "/work/environment/bin/$name"
		done
        cc -O2 -Wall -Wextra -static /work/demo/hold.c \
            -o /work/environment/bin/hold
    '
