"""Generated BLE preview, kept outside the production dashboard patch."""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'dashboard/dashboard'))
import app as dashboard
from ble_simulation import simulation_loop
from flask import request


def source(config):
    def set_link(link):
        dashboard._ble_link = link
    def status(state, reason):
        if state == 'warning':
            dashboard.warn_dropped(reason, 'Simulated BLE')
        else:
            dashboard.set_connection_status(state, reason)
    simulation_loop(config, dashboard.process_reading, status,
                    dashboard.set_rate_status, set_link)


@dashboard.app.after_request
def identify_generated_data(response):
    if request.path == '/' and response.mimetype == 'text/html':
        notice = ('<p class="card-foot" role="status"><strong>SIMULATED BLE:</strong> '
                  'generated readings; no physical Bluetooth connection or motor '
                  'control is being tested.</p>\n    ')
        response.set_data(response.get_data(as_text=True).replace(
            '<section class="hero"', notice + '<section class="hero"', 1))
    return response


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=5012)
    arguments = parser.parse_args()
    if not dashboard.port_is_free(arguments.port):
        parser.error(f'Port {arguments.port} is already in use')
    dashboard.socketio.start_background_task(dashboard.run_data_source, source,
                                             dashboard.load_config())
    dashboard.socketio.run(dashboard.app, host='127.0.0.1', port=arguments.port,
                           debug=False, use_reloader=False, allow_unsafe_werkzeug=True)
