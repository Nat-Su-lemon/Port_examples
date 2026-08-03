#!/usr/bin/env python3
"""
ESP32 AT-command automation over serial.

Reads AT commands line-by-line from a .txt file, sends each terminated with
\\r\\n, and waits for an 'OK' response before sending the next command.
"""

import sys
import time
import serial


# ---- Hardcoded configuration ----
PORT = "COM3"                # e.g. "COM3" or "/dev/ttyUSB0"
BAUD = 115200
CMD_FILE = "commands.txt"    # path to AT command .txt file
# ---------------------------------


def read_commands(path):
    with open(path, "r") as f:
        # keep non-empty, non-comment lines
        return [
            line.strip()
            for line in f
            if line.strip() and not line.strip().startswith("#")
        ]


def wait_for_ok(ser, timeout=10.0):
    """
    Read lines until 'OK' is seen (success) or 'ERROR' (failure) or timeout.
    Returns (success: bool, collected_lines: list[str]).
    """
    deadline = time.time() + timeout
    lines = []
    while time.time() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        text = raw.decode(errors="replace").strip()
        if text == "":
            continue
        lines.append(text)
        print(f"    < {text}")
        if text == "OK":
            return True, lines
        if text == "ERROR" or text.startswith("+CME ERROR") or text.startswith("+CMS ERROR"):
            return False, lines
    return False, lines  # timed out


def main():
    port, baud, cmd_file = PORT, BAUD, CMD_FILE

    try:
        commands = read_commands(cmd_file)
    except OSError as e:
        print(f"Could not read command file: {e}")
        sys.exit(1)

    if not commands:
        print("No commands found in file.")
        sys.exit(1)

    try:
        ser = serial.Serial(port, baud, timeout=1)
    except serial.SerialException as e:
        print(f"Could not open serial port: {e}")
        sys.exit(1)

    # give the ESP32 a moment after opening (auto-reset on some boards)
    time.sleep(2)
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    print(f"\nRunning {len(commands)} command(s) on {port} @ {baud}\n")

    with ser:
        for i, cmd in enumerate(commands, 1):
            print(f"[{i}/{len(commands)}] > {cmd}")
            ser.reset_input_buffer()
            ser.write((cmd + "\r\n").encode())
            ser.flush()

            success, _ = wait_for_ok(ser)
            if not success:
                print(f"\nStopped: no OK received for '{cmd}'.")
                sys.exit(1)
            print()

    print("All commands completed successfully.")


if __name__ == "__main__":
    main()
