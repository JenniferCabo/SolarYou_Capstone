// Live dashboard: listens for "sensor_update" events pushed by the Flask
// server and updates both the chart and the raw readout values.
//
// Chart update pattern follows Chart.js's standard live-data approach:
// push new data into the dataset, drop the oldest point once we exceed
// MAX_POINTS, then call chart.update(). This keeps a rolling time window
// rather than growing the dataset forever.

const MAX_POINTS = 30; // ~30 seconds of history at a 1s update interval

const ctx = document.getElementById("orientationChart").getContext("2d");
const orientationChart = new Chart(ctx, {
    type: "line",
    data: {
        labels: [],
        datasets: [
            {
                label: "Pitch",
                data: [],
                borderColor: "#2563eb",
                borderWidth: 2,
                pointRadius: 0,
                tension: 0.3,
            },
            {
                label: "Roll",
                data: [],
                borderColor: "#dc2626",
                borderWidth: 2,
                pointRadius: 0,
                tension: 0.3,
            },
        ],
    },
    options: {
        animation: false, // avoids visual lag when updating once per second
        responsive: true,
        scales: {
            y: {
                title: { display: true, text: "Degrees" },
            },
        },
    },
});

function updateChart(reading) {
    const label = new Date().toLocaleTimeString();

    orientationChart.data.labels.push(label);
    orientationChart.data.datasets[0].data.push(reading.pitch);
    orientationChart.data.datasets[1].data.push(reading.roll);

    if (orientationChart.data.labels.length > MAX_POINTS) {
        orientationChart.data.labels.shift();
        orientationChart.data.datasets[0].data.shift();
        orientationChart.data.datasets[1].data.shift();
    }

    orientationChart.update();
}

function updateReadout(reading) {
    document.getElementById("pitchValue").innerText = `${reading.pitch}°`;
    document.getElementById("rollValue").innerText = `${reading.roll}°`;
    document.getElementById("powerValue").innerText = `${reading.power} W`;
}

// Connect to the Flask-SocketIO server and wire incoming data to both
// update functions above. This is the only part that changes when the
// server starts sending real ESP32 data instead of mock data -- the
// event name and payload shape (pitch/roll/power) stay the same either way.
const socket = io();
socket.on("sensor_update", (reading) => {
    updateChart(reading);
    updateReadout(reading);
});
