#!/usr/bin/env python3
"""Host-only PTY end-to-end test for the Milk-V UART diagnostic program."""
import os
import pty
import select
import signal
import subprocess
import termios
import time


def main():
    master, slave = pty.openpty()
    slave_path = os.ttyname(slave)
    # Child sets raw termios; the master emulates STM32.
    binary = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "linux", "uart_probe"))
    proc = subprocess.Popen([binary, slave_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        deadline = time.monotonic() + 3
        received = b""
        while b"$SYS,0*45\r\n" not in received and time.monotonic() < deadline:
            r, _, _ = select.select([master], [], [], .2)
            if r:
                received += os.read(master, 256)
        assert b"$SYS,0*45\r\n" in received, f"SYS not received: {received!r}"
        os.write(master, b"$ENV,00000042,+02450,05310,0*44\r\n")
        deadline = time.monotonic() + 3
        line = ""
        while time.monotonic() < deadline:
            r, _, _ = select.select([proc.stdout], [], [], .2)
            if r:
                line = proc.stdout.readline()
                if "RX ENV,00000042,+02450,05310,0" in line:
                    break
        assert "RX ENV,00000042,+02450,05310,0" in line, f"ENV not parsed: {line!r}"
        print("PASS: virtual serial PTY received valid SYS and ENV, without hardware")
    finally:
        proc.send_signal(signal.SIGINT)
        try:
            proc.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.communicate()
        os.close(master)
        os.close(slave)


if __name__ == "__main__":
    main()
