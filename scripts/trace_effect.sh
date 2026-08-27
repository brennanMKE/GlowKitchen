#!/bin/zsh

# Read EFFECT_TRACE telemetry from a USB-connected device and summarize what the
# strip is actually doing -- movement, colors, and frame rate -- without anyone
# having to look at it.
#
# Requires a build with -DEFFECT_TRACE (set in [env:esp32c3-debug]).
#
# Usage:
#   scripts/trace_effect.sh [seconds] [port]
#   scripts/trace_effect.sh 10
#   scripts/trace_effect.sh 15 /dev/cu.usbmodem1101
#
# Typical loop:
#   scripts/demo_effect.sh <device> cylon --speed 0
#   scripts/trace_effect.sh 8

SECS=${1:-8}
PORT=${2:-/dev/cu.usbmodem1101}

if [[ ! -e "$PORT" ]]; then
    echo "Error: no device at $PORT. Connected ports:" >&2
    ls /dev/cu.* 2>/dev/null | sed 's/^/  /' >&2
    exit 1
fi

python3 - "$PORT" "$SECS" <<'PY'
import re, sys, time
try:
    import serial
except ImportError:
    sys.exit("pyserial not installed: pip3 install pyserial")

port, secs = sys.argv[1], float(sys.argv[2])
LINE = re.compile(
    r"\[TRACE\] mode=(\d+) phase=(\d+) dir=(-?\d+) sub=(\d+) leds=(\d+) "
    r"speed=(\d+) int=(\d+) bright=(\d+) px=(.*)")
MODES = ["BLEND","FLICKER","CHASE","WIPE","SCAN","SPARKLE","PULSE","STROBE","COLORLOOP"]

try:
    s = serial.Serial(port, 115200, timeout=0.4)
except Exception as e:
    sys.exit(f"could not open {port}: {e}")

samples, end = [], time.time() + secs
while time.time() < end:
    raw = s.readline().decode("utf-8", "replace")
    m = LINE.search(raw)
    if m:
        samples.append((time.time(), m.groups()))
s.close()

if not samples:
    print("No [TRACE] lines seen.")
    print("  - Is the build flashed with -DEFFECT_TRACE (env:esp32c3-debug)?")
    print("  - Is an effect actually rendering? A renderer that returns false never traces.")
    sys.exit(1)

first, last = samples[0][1], samples[-1][1]
mode = int(first[0])
mode_name = MODES[mode] if mode < len(MODES) else f"?{mode}"
span = samples[-1][0] - samples[0][0]

frames = [g[8].split() for _, g in samples]
phases = [int(g[1]) for _, g in samples]
uniq_frames = {tuple(f) for f in frames}
lit = [sum(1 for px in f if px != "000000") for f in frames]

print(f"mode={mode_name}  leds={first[4]}  speed={first[5]}  intensity={first[6]}  brightness={first[7]}")
print(f"samples={len(samples)} over {span:.1f}s  (trace is rate-limited to ~4/s)")
print()
print(f"ANIMATING: {'yes' if len(uniq_frames) > 1 else 'NO — every sampled frame identical'}"
      f"   ({len(uniq_frames)} distinct frames)")
print(f"phase: {min(phases)}..{max(phases)}"
      f"{'  (never changed)' if min(phases) == max(phases) else ''}")
print(f"lit LEDs per frame: min={min(lit)} max={max(lit)}")

colors = {}
for f in frames:
    for px in f:
        if px != "000000":
            colors[px] = colors.get(px, 0) + 1
print(f"\ntop colors (hex RGB, sample count):")
for px, n in sorted(colors.items(), key=lambda kv: -kv[1])[:6]:
    r, g, b = int(px[0:2],16), int(px[2:4],16), int(px[4:6],16)
    mx, mn = max(r,g,b), min(r,g,b)
    sat = 0 if mx == 0 else round((mx-mn)*100/mx)
    print(f"  #{px}  sat={sat:>3}%  val={round(mx*100/255):>3}%   x{n}")

print("\nlast 5 frames:")
for _, g in samples[-5:]:
    print(f"  phase={g[1]:>3} dir={g[2]:>2}  {g[8]}")
PY
