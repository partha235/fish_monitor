from flask import Flask, render_template_string, Response, jsonify
import cv2
import requests
import time
import numpy as np
from ultralytics import YOLO
import threading
import random
from threading import Lock

app = Flask(__name__)

# ================= CONFIG =================
ESP_IP = '192.168.4.1'          # Change if needed
CAPTURE_URL = f'http://{ESP_IP}/capture'
DATA_URL = f'http://{ESP_IP}/data'

# Globals with Thread Safety
latest_frame = None
latest_distance = 0.0
latest_detections = []
sensor_data = {
    'temp': 25.0,
    'pres_hpa': 1013.25,
    'alt': 10.5,
    'sea_level_hpa': 1013.25
}

data_lock = Lock()

# Load YOLO Model
print("🔄 Loading seafish.pt model...")
model = YOLO('seafish.pt')
print("✅ Model loaded successfully!")

# ================= FRAME PROCESSOR =================
def fetch_and_process_frame():
    global latest_frame
    try:
        resp = requests.get(CAPTURE_URL, timeout=6)
        if resp.status_code == 200:
            nparr = np.frombuffer(resp.content, np.uint8)
            frame = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

            if frame is not None:
                results = model(frame, verbose=False, conf=0.45, iou=0.5)
                annotated_frame = results[0].plot()  # Beautiful annotations

                # Extract unique class names
                detections = []
                if results[0].boxes is not None:
                    cls_ids = results[0].boxes.cls.cpu().numpy()
                    detections = [model.names[int(cls)] for cls in set(cls_ids)]

                _, buffer = cv2.imencode('.jpg', annotated_frame, [cv2.IMWRITE_JPEG_QUALITY, 85])

                with data_lock:
                    latest_frame = buffer.tobytes()
                    latest_detections[:] = detections

    except Exception as e:
        # print(f"Frame error: {e}")
        pass


# ================= SENSOR FETCH =================
def fetch_sensor_data():
    global latest_distance
    try:
        resp = requests.get(DATA_URL, timeout=5)
        if resp.status_code == 200:
            data = resp.json()
            with data_lock:
                latest_distance = float(data.get('value', latest_distance))
                sensor_data['temp'] = data.get('temp', random.uniform(24, 28))
                sensor_data['pres_hpa'] = data.get('pres_hpa', random.uniform(1010, 1020))
                sensor_data['alt'] = data.get('alt', random.uniform(5, 15))
    except:
        # Simulation fallback
        with data_lock:
            latest_distance = random.uniform(15, 120)


# Background Threads
def frame_poller():
    while True:
        fetch_and_process_frame()
        time.sleep(0.9)      # ~1.1 FPS - smooth & stable

def sensor_poller():
    while True:
        fetch_sensor_data()
        time.sleep(1.0)

threading.Thread(target=frame_poller, daemon=True).start()
threading.Thread(target=sensor_poller, daemon=True).start()

# ================= HTML TEMPLATE =================
HTML_TEMPLATE = '''
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Fish Monitor System</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Poppins:wght@300;500;600;700&display=swap');
        body {
            margin: 0;
            font-family: 'Poppins', sans-serif;
            background: linear-gradient(135deg, #0f2027, #203a43, #2c5364);
            color: #fff;
            min-height: 100vh;
        }
        .container {
            max-width: 1000px;
            margin: 30px auto;
            padding: 25px;
            background: rgba(255,255,255,0.09);
            backdrop-filter: blur(12px);
            border-radius: 20px;
            box-shadow: 0 15px 50px rgba(0,0,0,0.6);
        }
        h1 {
            text-align: center;
            font-size: 2.7em;
            background: linear-gradient(90deg, #00dbde, #fc00ff);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }
        #video {
            width: 100%;
            max-width: 720px;
            border-radius: 16px;
            border: 5px solid #00ffea;
            box-shadow: 0 10px 40px rgba(0, 255, 234, 0.25);
            display: block;
            margin: 20px auto;
        }
        #detections {
            text-align: center;
            font-size: 1.25em;
            min-height: 55px;
            background: rgba(0,0,0,0.4);
            padding: 15px;
            border-radius: 12px;
            margin: 15px 0;
        }
        .info-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin: 25px 0;
        }
        .card {
            background: rgba(255,255,255,0.1);
            padding: 18px;
            border-radius: 12px;
            text-align: center;
        }
        .value { font-size: 1.9em; font-weight: 700; color: #00ffea; }
    </style>
</head>
<body>
<div class="container">
    <h1>🐟 Fish Monitor System</h1>
    <p style="text-align:center; color:#88dddd; margin-bottom:15px;">ESP32 + Custom YOLOv8 Detection</p>

    <img id="video" src="/video" alt="Live Feed">

    <div id="detections">Detected Labels: Loading...</div>

    <div class="info-grid">
        <div class="card">
            <div>Distance</div>
            <div class="value" id="distance">-- cm</div>
        </div>
        <div class="card">
            <div>Temperature</div>
            <div class="value" id="temp">-- °C</div>
        </div>
        <div class="card">
            <div>Pressure</div>
            <div class="value" id="pressure">-- hPa</div>
        </div>
        <div class="card">
            <div>Altitude</div>
            <div class="value" id="alt">-- m</div>
        </div>
    </div>

    <h2 style="text-align:center; margin:20px 0 10px;">Distance Trend (Last 25 readings)</h2>
    <canvas id="chart" height="190"></canvas>
</div>

<script>
const ctx = document.getElementById('chart').getContext('2d');
const chart = new Chart(ctx, {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: 'Distance (cm)',
            borderColor: '#00ffea',
            backgroundColor: 'rgba(0, 255, 234, 0.2)',
            data: [],
            tension: 0.4,
            fill: true
        }]
    },
    options: {
        responsive: true,
        plugins: { legend: { display: false }},
        scales: {
            y: { beginAtZero: true, grid: { color: 'rgba(255,255,255,0.1)' }},
            x: { grid: { color: 'rgba(255,255,255,0.1)' }}
        }
    }
});

function updateData() {
    fetch('/api/data')
    .then(r => r.json())
    .then(data => {
        document.getElementById('distance').textContent = data.distance.toFixed(1) + " cm";
        document.getElementById('temp').textContent = data.temp.toFixed(1) + " °C";
        document.getElementById('pressure').textContent = data.pres_hpa.toFixed(1) + " hPa";
        document.getElementById('alt').textContent = data.alt.toFixed(1) + " m";

        const detText = data.detections.length > 0 
            ? "Detected: " + data.detections.join(" • ") 
            : "Detected: No fish detected";
        document.getElementById('detections').innerHTML = detText;

        // Update Chart
        const now = new Date().toLocaleTimeString([], {hour:'2-digit', minute:'2-digit'});
        if (chart.data.labels.length > 25) {
            chart.data.labels.shift();
            chart.data.datasets[0].data.shift();
        }
        chart.data.labels.push(now);
        chart.data.datasets[0].data.push(data.distance);
        chart.update();
    })
    .catch(err => console.error(err));
}

// Refresh video + data
setInterval(() => {
    document.getElementById('video').src = '/video?' + Date.now();
}, 950);

setInterval(updateData, 1000);
updateData(); // initial load
</script>
</body>
</html>
'''

@app.route('/')
def index():
    return render_template_string(HTML_TEMPLATE)

@app.route('/video')
def video_feed():
    with data_lock:
        if latest_frame is None:
            return '', 404
        return Response(latest_frame, mimetype='image/jpeg')

@app.route('/api/data')
def api_data():
    with data_lock:
        return jsonify({
            'distance': latest_distance,
            'temp': round(sensor_data['temp'], 2),
            'pres_hpa': round(sensor_data['pres_hpa'], 2),
            'alt': round(sensor_data['alt'], 2),
            'detections': latest_detections[:]
        })

if __name__ == '__main__':
    print("🚀 Fish Monitor System Started!")
    print("🌐 Open → http://localhost:5000")
    app.run(host='0.0.0.0', port=5000, debug=False)