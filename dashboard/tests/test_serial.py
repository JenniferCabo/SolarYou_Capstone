"""
Checks for the serial helpers: line splitting, picking the board's port,
and turning port errors into readable messages. No hardware needed.

    python -m unittest discover -s tests -v
"""

import os
import sys
import unittest
from types import SimpleNamespace

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import app as dashboard  # noqa: E402


def port(device, vid, description="USB device"):
    return SimpleNamespace(device=device, vid=vid, description=description)


class SerialLinesTests(unittest.TestCase):
    def test_lines_split_across_chunks(self):
        lines = dashboard.SerialLines()
        self.assertEqual(lines.feed(b'{"pitch":1'), [])
        self.assertEqual(lines.feed(b'}\r\n{"roll":2}\n{"po'), [b'{"pitch":1}\r', b'{"roll":2}'])
        self.assertEqual(lines.feed(b'wer":3}\n'), [b'{"power":3}'])

    def test_runaway_line_is_dropped(self):
        lines = dashboard.SerialLines(max_line=100)
        lines.feed(b"x" * 500)
        self.assertEqual(lines.feed(b'tail\n{"pitch":1}\n'), [b"tail", b'{"pitch":1}'])

    def test_crlf_lines_still_parse(self):
        line = dashboard.SerialLines().feed(b'{"pitch":1}\r\n')[0]
        self.assertEqual(dashboard.json_from_serial_line(line), b'{"pitch":1}')


class PickPortTests(unittest.TestCase):
    def test_single_espressif_board(self):
        ports = [port("/dev/cu.Bluetooth-Incoming-Port", None), port("/dev/cu.usbmodem1101", 0x303A)]
        self.assertEqual(dashboard.pick_serial_port(ports)[0], "/dev/cu.usbmodem1101")

    def test_single_usb_serial_chip(self):
        self.assertEqual(dashboard.pick_serial_port([port("COM5", 0x10C4)])[0], "COM5")

    def test_board_with_both_usb_ports_plugged_in_prefers_native(self):
        ports = [port("/dev/ttyUSB0", 0x10C4), port("/dev/ttyACM0", 0x303A)]
        self.assertEqual(dashboard.pick_serial_port(ports)[0], "/dev/ttyACM0")

    def test_two_boards_is_ambiguous(self):
        found, why = dashboard.pick_serial_port([port("COM3", 0x303A), port("COM4", 0x303A)])
        self.assertIsNone(found)
        self.assertIn("several possible boards", why)

    def test_unknown_chip_used_when_it_is_the_only_usb_port(self):
        self.assertEqual(dashboard.pick_serial_port([port("COM7", 0x2341)])[0], "COM7")

    def test_nothing_plugged_in(self):
        found, why = dashboard.pick_serial_port([port("/dev/ttyS0", None)])
        self.assertIsNone(found)
        self.assertIn("no ESP32 board found", why)


class ErrorMessageTests(unittest.TestCase):
    def test_port_in_use(self):
        msg = dashboard.explain_serial_error(
            Exception("Could not exclusively lock port /dev/ttyACM0: [Errno 11] Resource temporarily unavailable"),
            "/dev/ttyACM0")
        self.assertIn("another program", msg)

    def test_unplugged(self):
        msg = dashboard.explain_serial_error(
            Exception("[Errno 2] could not open port /dev/ttyACM0: [Errno 2] No such file or directory"),
            "/dev/ttyACM0")
        self.assertIn("not there any more", msg)

    def test_disconnect_while_reading(self):
        msg = dashboard.explain_serial_error(Exception(
            "device reports readiness to read but returned no data (device disconnected or multiple access on port?)"),
            "/dev/ttyACM0")
        self.assertIn("unplugged or reset", msg)

    def test_other_errors_pass_through(self):
        self.assertEqual(dashboard.explain_serial_error(Exception("something odd"), "COM3"), "something odd")


    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux permission hint")
    def test_permission_hint_names_the_group_that_owns_the_port(self):
        import grp
        import tempfile
        with tempfile.NamedTemporaryFile() as fake_port:
            group = grp.getgrgid(os.stat(fake_port.name).st_gid).gr_name
            message = dashboard.explain_serial_error(PermissionError(13, "Permission denied"), fake_port.name)
        self.assertIn("usermod -aG " + group + " ", message)

    def test_port_group_falls_back_to_dialout(self):
        self.assertEqual(dashboard.port_group("/dev/not-a-real-port"), "dialout")


class ChoosePortTests(unittest.TestCase):
    def test_hand_picked_port_that_exists_is_used(self):
        here = os.path.abspath(__file__)  # any path that exists
        self.assertEqual(dashboard.choose_serial_port(here, [])[0], here)

    @unittest.skipIf(os.name == "nt", "port names aren't file paths on Windows")
    def test_hand_picked_port_gone_finds_the_board_under_its_new_name(self):
        found, how = dashboard.choose_serial_port("/dev/ttyACM0", [port("/dev/ttyACM1", 0x303A)])
        self.assertEqual(found, "/dev/ttyACM1")
        self.assertEqual(how, "was /dev/ttyACM0")

    @unittest.skipIf(os.name == "nt", "port names aren't file paths on Windows")
    def test_hand_picked_port_gone_and_no_board_keeps_trying_it(self):
        self.assertEqual(dashboard.choose_serial_port("/dev/ttyACM0", [])[0], "/dev/ttyACM0")

    def test_network_ports_are_never_replaced(self):
        url = "rfc2217://localhost:4000"
        self.assertEqual(dashboard.choose_serial_port(url, [port("/dev/ttyACM1", 0x303A)])[0], url)


class FakePort:
    """A serial port that plays back a script: bytes to return, then silence or an error."""

    def __init__(self, name, script):
        self.name = name
        self.script = list(script)
        self.in_waiting = 0
        self.closed = False

    def read(self, size):
        if not self.script:
            return b""
        step = self.script.pop(0)
        if isinstance(step, Exception):
            raise step
        return step

    def close(self):
        self.closed = True


class StopLoop(Exception):
    pass


@unittest.skipIf(os.name == "nt", "port names aren't file paths on Windows")
class ReconnectTests(unittest.TestCase):
    """Runs serial_data_loop against fake ports to check it recovers after a board reset."""

    def run_loop(self, config, ports_by_attempt, scripts, clock_step=0.0, max_sleeps=3):
        import serial as real_serial
        import serial.tools.list_ports  # noqa: F401  (loads real_serial.tools)

        opened = []
        attempts = {"n": 0}
        fake_now = {"t": 0.0}

        def comports():
            index = min(attempts["n"], len(ports_by_attempt) - 1)
            return ports_by_attempt[index]

        def open_port(serial_module, name, baud):
            attempts["n"] += 1
            if not scripts:
                raise StopLoop()
            fake = FakePort(name, scripts.pop(0))
            opened.append(fake)
            return fake

        def sleep(seconds):
            sleeps["n"] += 1
            if sleeps["n"] > max_sleeps:
                raise StopLoop()

        def monotonic():
            fake_now["t"] += clock_step
            return fake_now["t"]

        sleeps = {"n": 0}
        readings = []
        saved = (dashboard.open_serial, dashboard.process_reading, dashboard.set_connection_status,
                 dashboard.time.sleep, dashboard.time.monotonic, real_serial.tools.list_ports.comports)
        dashboard.open_serial = open_port
        dashboard.process_reading = readings.append
        dashboard.set_connection_status = lambda *a, **k: None
        dashboard.time.sleep = sleep
        dashboard.time.monotonic = monotonic
        real_serial.tools.list_ports.comports = comports
        printed = []
        dashboard.print = lambda *a, **k: printed.append(" ".join(str(x) for x in a))
        try:
            dashboard.serial_data_loop(config)
        except StopLoop:
            pass
        finally:
            (dashboard.open_serial, dashboard.process_reading, dashboard.set_connection_status,
             dashboard.time.sleep, dashboard.time.monotonic,
             real_serial.tools.list_ports.comports) = saved
            del dashboard.print
        return opened, readings, printed

    def test_reset_renames_the_port_and_the_dashboard_follows(self):
        import serial
        line = b'{"pitch":1.0,"roll":2.0}\n'
        lost = serial.SerialException("device reports readiness to read but returned no data")
        opened, readings, printed = self.run_loop(
            {"serial_port": "/dev/ttyACM0"},
            ports_by_attempt=[[port("/dev/ttyACM1", 0x303A)]],
            scripts=[[line, lost], [line, line, lost]],
        )
        self.assertEqual([p.name for p in opened], ["/dev/ttyACM1", "/dev/ttyACM1"])
        self.assertEqual(len(readings), 3)
        self.assertTrue(all(p.closed for p in opened))
        self.assertTrue(any("(was /dev/ttyACM0)" in p for p in printed))

    def test_silent_port_after_data_is_reopened(self):
        line = b'{"pitch":1.0,"roll":2.0}\n'
        opened, readings, printed = self.run_loop(
            {"serial_port": "auto", "serial_idle_timeout": 10},
            ports_by_attempt=[[port("/dev/ttyACM0", 0x303A)]],
            scripts=[[line] + [b""] * 50, [line]],
            clock_step=1.0,
            max_sleeps=1,
        )
        self.assertGreaterEqual(len(opened), 2)
        self.assertTrue(opened[0].closed)
        self.assertTrue(any("no data for 10 seconds" in p for p in printed))

    def test_quiet_board_that_never_sent_data_is_not_reopened(self):
        opened, readings, printed = self.run_loop(
            {"serial_port": "auto"},
            ports_by_attempt=[[port("/dev/ttyACM0", 0x303A)]],
            scripts=[[b"boot log line\n"] + [b""] * 30 + [StopLoop()]],
            clock_step=1.0,
        )
        self.assertEqual(len(opened), 1)
        self.assertFalse(any("reopening" in p for p in printed))


if __name__ == "__main__":
    unittest.main()
