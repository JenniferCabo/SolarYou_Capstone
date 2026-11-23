"""Run Zack's BLE dashboard for ESP-only testing.

Selects the test device, labels generated readings, and disables CSV logging.
Readings arrive from the ESP over Bluetooth. Dashboard source and config files
are unchanged.
"""
import argparse
import importlib.util
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
TEST_NAME = "SolarYou-ESP-TEST"
NOTICE = ('<div role="status" style="margin:12px;padding:14px;border:2px solid #9a6700;'
          'border-radius:8px;background:#fff1c2;color:#3b2900;text-align:center">'
          '<strong>ESP-only Bluetooth test — GENERATED READINGS</strong><br>'
          'The Bluetooth connection is real. Pitch, roll and power are generated; '
          'no sensors or motors are being tested.</div>')


def prepare_dashboard(directory):
    directory = Path(directory).resolve()
    sys.path.insert(0, str(directory))
    spec = importlib.util.spec_from_file_location("esp_only_dashboard", directory / "app.py")
    dashboard = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = dashboard
    spec.loader.exec_module(dashboard)
    dashboard.app.config["DATA_SOURCE"] = "ble"

    @dashboard.app.after_request
    def label_generated_readings(response):
        if response.mimetype == "text/html" and not response.direct_passthrough:
            html = re.sub(r"(<body\b[^>]*>)", lambda match: match.group(1) + NOTICE,
                          response.get_data(as_text=True), count=1, flags=re.IGNORECASE)
            response.set_data(html)
        return response

    config = dashboard.load_config()
    config.update({"ble_name": TEST_NAME,
                   "ble_characteristic": "f2d2680c-66bc-4e24-8eac-d8013a99320b",
                   "ble_rate_characteristic": "f2d2680c-66bc-4e24-8eac-d8013a99320c"})
    return dashboard, config


def main():
    candidates = (ROOT.parent / "dashboard", ROOT / ".validation-dashboard/dashboard")
    default_directory = next((path for path in candidates if (path / "app.py").is_file()), None)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dashboard", type=Path, default=default_directory)
    parser.add_argument("--port", type=int, default=5001)
    args = parser.parse_args()
    if args.dashboard is None or not (args.dashboard / "app.py").is_file():
        parser.error("Pass --dashboard with the dashboard directory from Zack's branch.")
    dashboard, config = prepare_dashboard(args.dashboard)
    if not dashboard.port_is_free(args.port):
        parser.error(f"Port {args.port} is already in use. Choose another --port.")
    print(f"ESP-only test: connecting to {TEST_NAME} over real Bluetooth.")
    print("Readings are GENERATED. Persistent CSV logging is disabled for this test.")
    print(f"Dashboard: http://localhost:{args.port}")
    dashboard.socketio.start_background_task(dashboard.run_data_source, dashboard.ble_data_loop, config)
    dashboard.socketio.run(dashboard.app, host="127.0.0.1", port=args.port,
                           debug=False, use_reloader=False, allow_unsafe_werkzeug=True)


if __name__ == "__main__":
    main()
