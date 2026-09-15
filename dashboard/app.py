"""
Team 11 - Active Stabilization and Solar Tracking Platform
Dashboard server: serves the live view and pushes sensor data to any
connected browser over Socket.IO.

Currently running on mock data while waiting on the confirmed JSON schema and real hardware endpoint.
Swapping to real data means replacing mock_data_loop()'s data source
with a websocket-client connection to the ESP32
"""

import math
import random
import threading
import time

from flask import Flask, render_template
from flask_socketio import SocketIO

app = Flask(__name__)
app.config["SECRET_KEY"] = "dev"  # for local/demo use, not for production
socketio = SocketIO(app)

UPDATE_INTERVAL_SECONDS = 1.0


def generate_mock_reading(t):
    """
    Produces fake reading: a damped oscillation
    settling toward zero, similar in shape to what a stabilizing platform
    correcting toward a target angle should look like, rather than pure
    random noise. This is a placeholder for real IMU/power data.
    """
    decay = math.exp(-0.05 * t)
    pitch = 8 * decay * math.sin(0.5 * t) + random.uniform(-0.3, 0.3)
    roll = 6 * decay * math.cos(0.4 * t) + random.uniform(-0.3, 0.3)
    power = 1.2 + 0.1 * math.sin(0.1 * t) + random.uniform(-0.05, 0.05)

    return {
        "pitch": round(pitch, 2),
        "roll": round(roll, 2),
        "power": round(power, 2),
    }


def mock_data_loop():
    """Background thread: stands in for a real ESP32 WebSocket connection."""
    t = 0
    while True:
        reading = generate_mock_reading(t)
        socketio.emit("sensor_update", reading)
        t += UPDATE_INTERVAL_SECONDS
        time.sleep(UPDATE_INTERVAL_SECONDS)


@app.route("/")
def index():
    return render_template("index.html")


if __name__ == "__main__":
    # Port 5001, not 5000: macOS AirPlay Receiver uses 5000 by default.
    # host 0.0.0.0: makes this reachable from other devices on the same
    # network (e.g. a phone browser), not just this machine.
    threading.Thread(target=mock_data_loop, daemon=True).start()
    # allow_unsafe_werkzeug=True: fine here since this only ever runs
    # locally for development/demo purposes, never a public deployment.
    socketio.run(app, debug=True, host="0.0.0.0", port=5001,
                 allow_unsafe_werkzeug=True)
