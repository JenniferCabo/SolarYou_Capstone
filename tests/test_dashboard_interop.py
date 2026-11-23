"""Feed the production C sender's bytes to Zack's unmodified BLE receiver.

Usage: python tests/test_dashboard_interop.py DASHBOARD_DIRECTORY PROTOCOL_DLL
Only the Python standard library is required; no board or radio is involved.
"""
import ctypes
import importlib.util
import json
from pathlib import Path
import sys
import unittest

dashboard_path, library_path = map(Path, sys.argv[1:3])
spec = importlib.util.spec_from_file_location("zack_ble_transport", dashboard_path / "ble_transport.py")
receiver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(receiver)
sender = ctypes.CDLL(str(library_path.resolve()))


class Sample(ctypes.Structure):
    _fields_ = [(name, ctypes.c_float) for name in
                ("pitch", "roll", "power", "pitch_target", "roll_target")]
    _fields_ += [(name, ctypes.c_bool) for name in
                 ("imu_valid", "power_valid", "targets_valid", "simulated")]
    _fields_ += [("captured_ms", ctypes.c_uint32)]


sender.sy_telemetry_format.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(Sample)]
sender.sy_telemetry_format.restype = ctypes.c_size_t
sender.sy_ble_fragment_count.argtypes = [ctypes.c_size_t, ctypes.c_size_t]
sender.sy_ble_fragment_count.restype = ctypes.c_size_t
sender.sy_ble_fragment.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint16,
                                   ctypes.c_size_t, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
sender.sy_ble_fragment.restype = ctypes.c_size_t


def packets(sample, mtu_payload=20, sequence=42):
    message = ctypes.create_string_buffer(256)
    size = sender.sy_telemetry_format(message, len(message), ctypes.byref(sample))
    assert size
    result = []
    for index in range(sender.sy_ble_fragment_count(size, mtu_payload)):
        packet = ctypes.create_string_buffer(512)
        count = sender.sy_ble_fragment(message, size, sequence, mtu_payload, index, packet, len(packet))
        assert count
        result.append(packet.raw[:count])
    return result


class SenderReceiverCompatibility(unittest.TestCase):
    def setUp(self):
        self.sample = Sample(1.25, -2, 0, 0, 5, True, True, True, False, 0)

    def decode(self, pieces):
        decoder = receiver.NotificationDecoder()
        result = None
        for piece in pieces:
            result = decoder.feed(piece)
        return result

    def test_default_mtu_split_message(self):
        pieces = packets(self.sample)
        self.assertGreater(len(pieces), 1)
        self.assertTrue(all(len(piece) <= 20 for piece in pieces))
        self.assertEqual(self.decode(pieces), {
            "pitch": 1.25, "roll": -2, "power": 0,
            "pitch_target": 0, "roll_target": 5})

    def test_large_mtu_whole_message(self):
        pieces = packets(self.sample, mtu_payload=182)
        self.assertEqual(len(pieces), 1)
        self.assertEqual(self.decode(pieces)["power"], 0)

    def test_unavailable_values(self):
        self.sample.imu_valid = self.sample.power_valid = self.sample.targets_valid = False
        self.assertEqual(self.decode(packets(self.sample)), {
            key: None for key in ("pitch", "roll", "power", "pitch_target", "roll_target")})

    def test_esp_only_readings_are_marked(self):
        self.sample.simulated = True
        decoded = self.decode(packets(self.sample))
        self.assertIs(decoded['simulated'], True)
        self.assertEqual(decoded['pitch'], 1.25)

    def test_disconnect_discards_partial_message(self):
        decoder = receiver.NotificationDecoder()
        pieces = packets(self.sample)
        self.assertIsNone(decoder.feed(pieces[0]))
        decoder.reset()  # The real receiver does this when the connection drops.
        with self.assertRaises(ValueError):
            decoder.feed(pieces[1])
        result = None
        for piece in packets(self.sample, sequence=43):
            result = decoder.feed(piece)
        self.assertEqual(result["pitch"], 1.25)

    def test_dropped_fragment_recovers_on_next_reading(self):
        decoder = receiver.NotificationDecoder()
        pieces = packets(self.sample)
        decoder.feed(pieces[0])
        with self.assertRaises(ValueError):
            decoder.feed(pieces[2])
        result = None
        for piece in packets(self.sample, sequence=44):
            result = decoder.feed(piece)
        self.assertEqual(result["roll"], -2)

    def test_rate_reply_is_not_a_sensor_reading(self):
        readings = []
        link = receiver.BleLink({"ble_name": "SolarYou-ASSTP", "ble_characteristic": "test"},
                                readings.append, lambda *_: None, lambda *_: None)
        link.handle_notification(None, b'{"rate_hz":5}')
        self.assertEqual(readings, [])
        for piece in packets(self.sample):
            link.handle_notification(None, piece)
        self.assertEqual(len(readings), 1)
        self.assertEqual(readings[0]["power"], 0)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)
