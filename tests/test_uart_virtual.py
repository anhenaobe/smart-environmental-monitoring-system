#!/usr/bin/env python3
"""Host-only PTY tests, never touch a physical UART or invoke envmond."""
import os
import pty
import select
import signal
import subprocess
import termios
import time
import unittest
from pathlib import Path

BINARY = Path(__file__).resolve().parents[1] / "linux" / "uart_probe"


def frame(payload):
    data = payload.encode("ascii")
    checksum = 0
    for byte in data:
        checksum ^= byte
    return b"$" + data + b"*%02X\r\n" % checksum


class Session:
    def __init__(self, *args):
        self.master, self.slave = pty.openpty()
        self.initial = termios.tcgetattr(self.slave)
        self.received = b""
        self.proc = subprocess.Popen(
            [str(BINARY), "--confirm-data-uart", *args, os.ttyname(self.slave)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

    def pump(self, duration, target=None):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            if target is not None and target in self.received:
                break
            r, _, _ = select.select([self.master], [], [], min(0.1, max(0, deadline - time.monotonic())))
            if r:
                self.received += os.read(self.master, 256)
        return self.received

    def send(self, data):
        os.write(self.master, data)

    def finish(self, timeout=5, signal_number=None):
        if signal_number is not None:
            self.proc.send_signal(signal_number)
        out, err = self.proc.communicate(timeout=timeout)
        return self.proc.returncode, out, err

    def close(self):
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.communicate(timeout=2)
        if self.master is not None:
            os.close(self.master)
        os.close(self.slave)


class UARTTests(unittest.TestCase):
    def session(self, *args):
        session = Session(*args)
        self.addCleanup(session.close)
        return session

    def check_expected_sys(self, s, payload="SYS,0"):
        self.assertIn(frame(payload), s.pump(3, target=frame(payload)))
        t = termios.tcgetattr(s.slave)
        self.assertEqual(t[4:6], [termios.B115200, termios.B115200])
        self.assertEqual(t[2] & termios.CSIZE, termios.CS8)
        self.assertFalse(t[2] & (termios.PARENB | termios.CSTOPB))
        self.assertFalse(t[0] & (termios.IXON | termios.IXOFF))
        self.assertFalse(t[3] & (termios.ICANON | termios.ECHO))

    def test_receive_and_default_no_ack(self):
        s = self.session("--duration", "3")
        self.check_expected_sys(s)
        # Partial frame, garbage prefix, damaged checksum, malformed fields, unknown type.
        s.send(b"stale-non-frame" + frame("ENV,99999999,+02450,05310,0")[:13])
        time.sleep(0.02)
        s.send(frame("ENV,99999999,+02450,05310,0")[13:])
        s.send(frame("ENV,00000000,-00520,00000,0"))
        s.send(frame("ENV,00000002,,,1"))
        s.send(frame("ENV,00000003,+02450,10001,0"))  # impossible RH
        s.send(frame("ENV,00000003,+02450,05310,2"))  # nonempty values on error
        s.send(frame("ENV,00000003,,,2"))
        s.send(b"$ENV,00000004,+02450,05310,0*00\r\n")
        s.send(frame("STA,00000003,0,0,0.2.0,00850,02048"))
        s.send(frame("XYZ,0"))
        s.send(frame("BTN,RST"))
        self.assertNotIn(frame("ACK,RST"), s.pump(1.3))
        rc, out, err = s.finish(timeout=5)
        self.assertEqual(rc, 0, err)
        self.assertIn("RX ENV,99999999,+02450,05310,0", out)
        self.assertIn("RX ENV,00000003,,,2", out)
        self.assertIn("RX STA,00000003,0,0,0.2.0,00850,02048", out)
        self.assertIn("RX BTN,RST", out)
        for expected in (
            "env_ok=2", "env_sensor_error=2", "sta=1", "btn=1",
            "invalid_frames=1", "malformed_payloads=2", "unknown_payloads=1",
            "sequence_jumps=1", "simulated_ack=0",
        ):
            self.assertIn(expected, err)
        self.assertIn("no ACK, no reset", err)
        self.assertEqual(termios.tcgetattr(s.slave), s.initial)

    def test_ack_opt_in_and_sys_state(self):
        s = self.session("--sys-state", "2", "--simulate-reset-ack")
        self.check_expected_sys(s, "SYS,2")
        s.send(frame("BTN,BAD") + b"$BTN,RST*00\r\n")  # never ACK these
        self.assertNotIn(frame("ACK,RST"), s.pump(0.3))
        s.send(frame("BTN,RST"))
        self.assertIn(frame("ACK,RST"), s.pump(3, target=frame("ACK,RST")))
        # Next heartbeat is independent of the ACK.
        self.assertIn(frame("SYS,2"), s.pump(1.3))
        self.assertGreaterEqual(s.received.count(frame("SYS,2")), 2)
        rc, _, err = s.finish(signal_number=signal.SIGINT)
        self.assertEqual(rc, 0, err)
        self.assertIn("SIMULATED ONLY; no reset.pending, no factory reset", err)
        self.assertIn("simulated_ack=1", err)
        self.assertIn("btn=1", err)
        self.assertEqual(termios.tcgetattr(s.slave), s.initial)

    def test_timeout_without_env_returns_failure(self):
        s = self.session("--duration", "1")
        self.check_expected_sys(s)
        rc, _, err = s.finish(timeout=4)
        self.assertNotEqual(rc, 0)
        self.assertIn("did not observe any valid ENV", err)
        self.assertEqual(termios.tcgetattr(s.slave), s.initial)

    def test_hangup_reports_failure(self):
        s = self.session()
        self.check_expected_sys(s)
        os.close(s.master)
        s.master = None
        rc, _, err = s.finish(timeout=3)
        self.assertNotEqual(rc, 0)
        self.assertRegex(err, r"poll error/hangup|write SYS|read UART")

    def test_isolated_loopback(self):
        s = self.session("--loopback", "--duration", "2")
        for index in range(2):
            expected = frame(f"LPB,{index:08d}")
            self.assertIn(expected, s.pump(3, target=expected))
            s.send(expected)
        rc, out, err = s.finish(timeout=4)
        self.assertEqual(rc, 0, err)
        self.assertIn("ECHO LPB,00000000", out)
        self.assertIn("ECHO LPB,00000001", out)
        self.assertIn("LOOPBACK tx=2 echo=2 other=0 invalid_frames=0", err)
        self.assertEqual(termios.tcgetattr(s.slave), s.initial)

    def test_loopback_without_echo_fails(self):
        s = self.session("--loopback", "--duration", "1")
        self.assertIn(frame("LPB,00000000"), s.pump(3, target=frame("LPB,00000000")))
        rc, _, err = s.finish(timeout=4)
        self.assertNotEqual(rc, 0)
        self.assertIn("LOOPBACK tx=1 echo=0", err)

    def test_cli_refusals(self):
        for args in (
            ["/dev/tty"],  # no explicit confirmation
            ["--confirm-data-uart", "/dev/tty"],  # console pseudo-device
            ["--confirm-data-uart", "--duration", "0", "/dev/tty"],
            ["--confirm-data-uart", "--loopback", "--simulate-reset-ack", "/dev/tty"],
            ["--confirm-data-uart", "--sys-state", "4", "/dev/tty"],
            ["--confirm-data-uart", "--duration", "1x", "/dev/tty"],
        ):
            proc = subprocess.run([str(BINARY), *args], capture_output=True, timeout=2)
            self.assertNotEqual(proc.returncode, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
