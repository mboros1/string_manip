#!/usr/bin/env python3

# Builds and flashes an ESP-IDF app (the tests or the benchmarks), resets the
# board and relays its serial output between the FAF_<APP>_BEGIN and
# FAF_<APP>_END lines, also saving it to obj/<app>-<target>.log. For the
# tests, exits 0 only if every suite passed.
#
# Needs the ESP-IDF environment (idf.py, pyserial): run it through
# `make esp32_test` or `make esp32_bench`, which source export.sh first.

import argparse
import glob
import os
import re
import subprocess
import sys
import time

import serial
from serial.tools import list_ports

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APPS = {"tests": os.path.join(ROOT, "tests", "esp32"),
        "bench": os.path.join(ROOT, "bench", "esp32")}

# A panic or an unexpected reboot ends the run: the tests would never finish
CRASH = re.compile(r"Guru Meditation|abort\(\) was called|Backtrace:|rst:0x")


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbserial*") + glob.glob("/dev/cu.usbmodem*") +
                   glob.glob("/dev/cu.wchusbserial*") + glob.glob("/dev/cu.SLAB_USBtoUART*") +
                   glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))
    if len(ports) != 1:
        sys.exit(f"esp32: found {len(ports)} serial ports ({', '.join(ports) or 'none'}); "
                 "pick one with ESPPORT=...")
    return ports[0]


def is_usb_jtag(port):
    """The chip's own USB serial/JTAG interface (ESP32-S3, C3, C6, ...)."""
    return any(p.device == port and (p.vid, p.pid) == (0x303A, 0x1001)
               for p in list_ports.comports())


def open_port(port):
    # DTR and RTS low from the start: on the chip's own USB interface they
    # act as the boot-mode and reset lines, and pyserial raises both on open
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, 115200, 0.5
    ser.dtr = ser.rts = False
    ser.open()
    return ser


def reset_and_open(port):
    if is_usb_jtag(port):
        # A reset through these lines keeps the chip in the ROM downloader;
        # a watchdog reset makes it boot the app
        subprocess.run(["esptool", "--port", port, "--after", "watchdog-reset",
                        "chip-id"], check=True, capture_output=True)
        for _ in range(20):  # the port comes back after the reset
            try:
                return open_port(port)
            except (serial.SerialException, OSError):
                time.sleep(0.25)
        sys.exit(f"esp32: {port} did not come back after the reset")
    # USB-serial bridge: EN is wired to RTS, IO0 to DTR (kept high)
    ser = open_port(port)
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False
    return ser


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", choices=sorted(APPS), default="tests")
    parser.add_argument("--port", default=os.environ.get("ESPPORT") or None)
    parser.add_argument("--target", default="esp32",
                        help="chip, e.g. esp32, esp32s3, esp32c3 (default: esp32)")
    parser.add_argument("--timeout", type=float, default=300,
                        help="seconds to wait for the tests to finish")
    parser.add_argument("--groups", default="",
                        help="bench only: space-separated groups (default: all)")
    parser.add_argument("--defines", default="",
                        help="extra compile definitions, e.g. 'FAF_ARENAS=0'")
    parser.add_argument("--no-flash", action="store_true",
                        help="run what is already on the board")
    args = parser.parse_args()
    port = args.port or find_port()

    # a build directory per app and chip, with the generated sdkconfig in it
    build_dir = os.path.join(ROOT, "obj", f"{args.app}-{args.target}")
    marker = f"FAF_{args.app.upper()}"
    log_path = build_dir + ".log"
    if not args.no_flash:
        cmd = ["idf.py", "-C", APPS[args.app], "-B", build_dir,
               f"-DIDF_TARGET={args.target}",
               f"-DSDKCONFIG={os.path.join(build_dir, 'sdkconfig')}",
               f"-DFAF_BENCH_GROUPS={args.groups}",
               f"-DFAF_DEFINES={args.defines}",
               "-p", port, "build", "flash"]
        print("→", " ".join(cmd), flush=True)
        if subprocess.call(cmd) != 0:
            sys.exit("esp32: build or flash failed")

    ser = reset_and_open(port)
    started = time.monotonic()
    begun, buf = False, b""
    log = open(log_path, "w")
    try:
        while time.monotonic() - started < args.timeout:
            try:
                buf += ser.read(4096)
            except (serial.SerialException, OSError):
                # native USB ports can drop for a moment when the chip resets
                ser.close()
                time.sleep(0.5)
                try:
                    ser = open_port(port)
                except (serial.SerialException, OSError):
                    pass
                continue
            *lines, buf = buf.split(b"\n")
            for raw in lines:
                line = raw.decode("utf-8", "replace").rstrip("\r")
                if line.startswith(marker + "_BEGIN"):
                    begun = True
                elif begun and CRASH.search(line):
                    print(line)
                    sys.exit("esp32: the board crashed or rebooted during the tests")
                if begun:
                    print(line, flush=True)
                    log.write(line + "\n")
                if not line.startswith(marker + "_END"):
                    continue
                took = time.monotonic() - started
                m = re.match(r"FAF_TESTS_END suites=(\d+) failed=(\d+)", line)
                if m:
                    suites, failed = int(m.group(1)), int(m.group(2))
                    print(f"esp32: {suites - failed}/{suites} test files passed "
                          f"on {port} in {took:.1f} s (log: {log_path})")
                    sys.exit(1 if failed else 0)
                print(f"esp32: {args.app} finished on {port} in {took:.1f} s "
                      f"(log: {log_path})")
                sys.exit(0)
    finally:
        ser.close()
        log.close()
    sys.exit(f"esp32: no {marker}_END within {args.timeout:.0f} s"
             + ("" if begun else f" (and no {marker}_BEGIN: is the app flashed?)"))


if __name__ == "__main__":
    main()
