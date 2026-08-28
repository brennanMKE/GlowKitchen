#!/usr/bin/env python3
"""Persistent, non-resetting serial logger for the dev board.

Two hazards this avoids, both learned the hard way (issue #0020):

1. **It does not reset the board.** pyserial drives DTR/RTS on open, and the
   C3's USB Serial/JTAG peripheral treats those transitions as esptool's
   auto-reset request -- attaching the monitor rebooted the chip every time
   (`rst:0x15 (USB_UART_CHIP_RESET)` in the log), silently destroying the
   uptime and the in-RAM effect state under test. So this opens the tty as a
   plain file, the way `cat` does, and never touches a control line. Baud is
   set with stty, which also leaves DTR alone.

2. **It stays attached.** The firmware fix for #0020 (Serial.setTxTimeoutMs(0))
   means a detached reader no longer freezes the board, but a monitor that
   drops out mid-run still leaves holes in the log exactly where the
   interesting thing happened. Run this once for a whole session.

Usage:
    scripts/monitor_serial.py <logfile> [port]

Lines are timestamped with host wall-clock time and flushed immediately, so
another process can tail the file while this runs.
"""
import os, signal, subprocess, sys, time

logpath = sys.argv[1]
port = sys.argv[2] if len(sys.argv) > 2 else "/dev/cu.usbmodem101"

subprocess.run(["/bin/stty", "-f", port, "115200"], check=True)

running = True
def stop(*_):
    global running
    running = False
signal.signal(signal.SIGTERM, stop)
signal.signal(signal.SIGINT, stop)

fd = os.open(port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
log = open(logpath, "a", buffering=1)
buf = b""
try:
    while running:
        try:
            chunk = os.read(fd, 4096)
        except BlockingIOError:
            time.sleep(0.05)
            continue
        if not chunk:
            time.sleep(0.05)
            continue
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            log.write(f"{time.time():.3f} {line.decode('utf-8', 'replace').rstrip()}\n")
finally:
    os.close(fd)
    log.close()
