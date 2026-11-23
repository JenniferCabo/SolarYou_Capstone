"""Check the test dashboard's labels/config without starting a radio or server."""
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
DASHBOARD = Path(sys.argv[1]).resolve()
spec = importlib.util.spec_from_file_location("esp_only_launcher", ROOT / "tools/run_esp_only_dashboard.py")
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)


class EspOnlyLauncherTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config_file = DASHBOARD / "config.json"
        cls.config_before = cls.config_file.read_bytes() if cls.config_file.exists() else None
        cls.dashboard, cls.config = launcher.prepare_dashboard(DASHBOARD)

    def test_generated_readings_are_visible(self):
        page = self.dashboard.app.test_client().get("/").get_data(as_text=True)
        self.assertIn("GENERATED READINGS", page)
        self.assertIn("The Bluetooth connection is real", page)
        self.assertIn('<div class="rate-control">', page)

    def test_test_device_and_no_automatic_log(self):
        self.assertEqual(self.config["ble_name"], "SolarYou-ESP-TEST")
        self.assertEqual(self.config["ble_characteristic"], "f2d2680c-66bc-4e24-8eac-d8013a99320b")
        self.assertIsNone(self.dashboard.reading_log)
        after = self.config_file.read_bytes() if self.config_file.exists() else None
        self.assertEqual(after, self.config_before)

    def test_launch_selects_real_ble_receiver(self):
        with patch.object(launcher, "prepare_dashboard", return_value=(self.dashboard, self.config)), \
             patch.object(self.dashboard, "port_is_free", return_value=True), \
             patch.object(self.dashboard.socketio, "start_background_task") as start, \
             patch.object(self.dashboard.socketio, "run") as run, \
             patch.object(sys, "argv", ["launcher", "--dashboard", str(DASHBOARD)]):
            launcher.main()
        start.assert_called_once_with(self.dashboard.run_data_source, self.dashboard.ble_data_loop, self.config)
        self.assertEqual(run.call_args.kwargs["host"], "127.0.0.1")
        self.assertFalse(run.call_args.kwargs["use_reloader"])

    def test_marked_readings_reach_the_chart(self):
        with patch.object(self.dashboard.socketio, "emit") as emit:
            self.dashboard.process_reading({"pitch": 1.25, "roll": -2, "power": 0,
                                            "pitch_target": None, "roll_target": None,
                                            "simulated": True})
        emit.assert_called_once()
        self.assertEqual(emit.call_args.args[0], "sensor_update")
        self.assertEqual(emit.call_args.args[1]["pitch"], 1.25)
        self.assertEqual(emit.call_args.args[1]["power"], 0)
        self.assertIsNone(self.dashboard.reading_log)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)
