#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Put a "plant" in front of the simulated rover, so the front-arc avoidance can
# be watched in Wokwi without hardware.
#
#   ./scripts/sim-obstacle.sh 24                 # plant 24 cm dead ahead
#   ./scripts/sim-obstacle.sh front 24 left 18   # ahead 24, left beam 18, right untouched
#   ./scripts/sim-obstacle.sh right 20           # plant only at the front-right corner
#   ./scripts/sim-obstacle.sh --reset            # back to the committed scene
#   ./scripts/sim-obstacle.sh --show             # print the current distances
#
# Wokwi reads each HC-SR04 distance from diagram.json (a sensor does not ray-cast
# in the simulator), so this edits exactly those three values - and writes the
# file back byte-for-byte otherwise, which keeps the git diff to one number.
#
#   front -> ultrasonic1 (ECHO 32)     left -> ultrasonic2 (ECHO 23)
#   right -> ultrasonic3 (ECHO 39/VP)  1-400 cm, 400 = "nothing in range"
#
# The three sensors sit on the same brackets as the real rover: one centre plus
# the two side ones splayed outwards, exactly like the printed mounts.
# ---------------------------------------------------------------------------
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIAGRAM="$ROOT/wokwi-esp32-project/diagram.json"

# Committed scene (see git): centre 49, left 197, right 400 cm.
DEFAULT_FRONT=49
DEFAULT_LEFT=197
DEFAULT_RIGHT=400

declare -A SENSOR_ID=( [front]=ultrasonic1 [left]=ultrasonic2 [right]=ultrasonic3 )

show() {
    python3 - "$DIAGRAM" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
names = {"ultrasonic1": "front", "ultrasonic2": "left ", "ultrasonic3": "right"}
for p in d["parts"]:
    if p["id"] in names:
        print(f"  {names[p['id']]}  {p['attrs'].get('distance')} cm")
PY
}

set_distance() { # $1 = role (front|left|right), $2 = cm
    local role="$1" cm="$2" id="${SENSOR_ID[$1]}"
    python3 - "$DIAGRAM" "$id" "$cm" <<'PY'
import json, re, sys
path, sensor_id, cm = sys.argv[1], sys.argv[2], sys.argv[3]
with open(path, "r", encoding="utf-8") as fh:
    lines = fh.readlines()

# Find the part block for this sensor id, then its "distance" property, and
# rewrite ONLY that value so the rest of the file stays byte-for-byte identical.
start = None
for i, line in enumerate(lines):
    if re.search(r'"id"\s*:\s*"%s"' % re.escape(sensor_id), line):
        start = i
        break
if start is None:
    sys.exit(f"sensor {sensor_id} not found in {path}")
end = start
for j in range(start + 1, min(start + 40, len(lines))):
    if re.search(r'"id"\s*:', lines[j]):   # next part begins
        break
    end = j
pat = re.compile(r'("distance"\s*:\s*)"[0-9]+"')
for i in range(start, end + 1):
    new, n = pat.subn(rf'\g<1>"{cm}"', lines[i])
    if n:
        lines[i] = new
        break
else:
    sys.exit(f"{sensor_id} has no distance attribute to change")
with open(path, "w", encoding="utf-8") as fh:
    fh.writelines(lines)
json.load(open(path))          # must still be valid JSON
print(f"  {sensor_id} -> {cm} cm")
PY
}

validate() { # $1 = cm
    case "$1" in
        ''|*[!0-9]*) echo "distance must be a whole number of cm (1-400)" >&2; exit 2 ;;
    esac
    if [ "$1" -lt 1 ] || [ "$1" -gt 400 ]; then
        echo "distance must be between 1 and 400 cm" >&2; exit 2
    fi
}

[ -f "$DIAGRAM" ] || { echo "diagram.json not found at $DIAGRAM" >&2; exit 1; }

if [ "$#" -eq 0 ]; then
    echo "usage: $0 [front|left|right] <cm> [front|left|right] <cm> ... | --reset | --show" >&2
    exit 2
fi

case "${1:-}" in
    --show|-s)
        echo "simulated distances:"; show; exit 0 ;;
    --reset|-r)
        echo "restoring the committed scene:"
        set_distance front "$DEFAULT_FRONT"
        set_distance left  "$DEFAULT_LEFT"
        set_distance right "$DEFAULT_RIGHT"
        exit 0 ;;
esac

# No role given -> "plant dead ahead".
if [[ "${1:-}" =~ ^[0-9]+$ ]]; then
    validate "$1"
    echo "plant dead ahead:"
    set_distance front "$1"
    exit 0
fi

echo "plant(s):"
while [ "$#" -gt 0 ]; do
    role="${1:-}"; cm="${2:-}"
    case "$role" in
        front|left|right) ;;
        *) echo "unknown sensor '$role' (use front|left|right)" >&2; exit 2 ;;
    esac
    [ -n "$cm" ] || { echo "missing cm after '$role'" >&2; exit 2; }
    validate "$cm"
    set_distance "$role" "$cm"
    shift 2
done
echo
echo "now:"
show
echo
echo "Wokwi reloads diagram.json on save - keep the sim window open."
