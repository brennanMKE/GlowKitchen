#!/bin/zsh
# Run a demo preset and report exactly how long the device took to apply it.
# Usage: scripts/demo_timed.sh <device> <preset> [seconds]
SCRIPT_DIR="${0:A:h}"
python3 - "$SCRIPT_DIR" "$@" <<'PY'
import subprocess, sys, time, threading
sd, args = sys.argv[1], sys.argv[2:]
hits = []
def watch():
    try:
        import serial
        s = serial.Serial('/dev/cu.usbmodem1101', 115200, timeout=0.3)
    except Exception as e:
        print(f"(serial unavailable: {e})"); return
    end = time.time() + 30
    while time.time() < end and not hits:
        l = s.readline().decode('utf-8', 'replace')
        if 'SET_EFFECT applied' in l or 'rejected' in l:
            hits.append((time.time(), l.strip().split('MAIN] ')[-1]))
    s.close()
t = threading.Thread(target=watch, daemon=True); t.start()
time.sleep(1.0)
t0 = time.time()
subprocess.run([f"{sd}/demo_effect.sh", *args], stdout=subprocess.DEVNULL)
print(f"published after {time.time()-t0:.2f}s")
for _ in range(200):
    if hits: break
    time.sleep(0.1)
if hits:
    print(f"DEVICE APPLIED {hits[0][0]-t0:.2f}s after publish")
    print(f"  {hits[0][1]}")
else:
    print("device never reported applying it within 20s")
PY
