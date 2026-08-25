#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
attacker_image=flush-monitor-demo-attacker:local
victim_image=flush-monitor-demo-victim:local
attacker_name=flush-monitor-demo-attacker-$$
victim_name=flush-monitor-demo-victim-$$
state=$(mktemp -d /tmp/flush-monitor-cross-container.XXXXXX)
target="$root/victim/target.bin"
truth="$state/ground-truth.csv"

cleanup()
{
	docker stop "$attacker_name" "$victim_name" >/dev/null 2>&1 || true
	rm -rf -- "$state"
}
trap cleanup EXIT HUP INT TERM

echo "Preparing static binaries and container images..."
make -C "$root/victim" static-container >/dev/null 2>&1
"$root/environment/build-guest-tools.sh" >/dev/null 2>&1
cp "$root/victim/victim-static" "$root/victim/victim-constant-static" \
	"$root/environment/bin/flush" \
	"$root/environment/bin/monitor-modern-once" \
	"$root/environment/bin/hold" "$state/"
cp "$root/demo/cross-container.Dockerfile" "$state/Dockerfile"

docker build --target victim -t "$victim_image" "$state" >/dev/null 2>&1
docker build --target attacker -t "$attacker_image" "$state" >/dev/null 2>&1
sync -f "$target"

common="--rm --network none --read-only --cap-drop ALL"
docker run -d $common --name "$attacker_name" \
	--security-opt no-new-privileges --security-opt label=disable \
	--user 10001:10001 --mount "type=bind,src=$target,dst=/target.bin,readonly" \
	"$attacker_image" >/dev/null
docker run -d $common --name "$victim_name" \
	--security-opt no-new-privileges --security-opt label=disable \
	--user "$(id -u):$(id -g)" \
	--mount "type=bind,src=$target,dst=/target.bin,readonly" \
	--mount "type=bind,src=$state,dst=/evidence" \
	"$victim_image" >/dev/null

inode=$(stat -c '%d:%i' "$target")

echo "CROSS-CONTAINER FLUSH+MONITOR — kernel $(uname -r)"
echo "ATTACKER: $attacker_name (UID 10001, no network, target read-only)"
echo "VICTIM:   $victim_name (UID $(id -u), private ground truth)"
echo "SHARED:   target inode $inode"
echo
printf '%-10s %-7s %-12s %-10s\n' POLICY EVENT OBSERVATION RESULT

trial=0
for policy in normal constant; do
	for event in 0 1; do
		trial=$((trial + 1))
		docker exec "$attacker_name" /flush /target.bin 0 >/dev/null
		binary=/victim
		expected=$event
		if test "$policy" = constant; then
			binary=/victim-constant
			expected=1
		fi
		docker exec "$victim_name" "$binary" run /target.bin \
			/evidence/ground-truth.csv "$trial" "$event" >/dev/null
		output=$(docker exec "$attacker_name" \
			/monitor flush /target.bin)
		case "$output" in
		*classification=hit*) prediction=1; observation=hit ;;
		*classification=miss*) prediction=0; observation=miss ;;
		*) echo "invalid Monitor output: $output" >&2; exit 1 ;;
		esac
		result=PASS
		test "$prediction" -eq "$expected" || result=FAIL
		printf '%-10s %-7s %-12s %-10s\n' "$policy" "$event" \
			"$observation" "$result"
		test "$result" = PASS
	done
done

echo
echo "Baseline leaks EVENT (miss/hit); constant access is always hit."
echo "The attacker container never receives the ground-truth mount."
echo "Victim ground truth:"
sed 's/^/  /' "$truth"
