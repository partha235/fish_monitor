from flask import Flask, render_template_string, Response, jsonify
import requests
import numpy as np
import cv2
import time
import threading
from threading import Lock
import torch
from ultralytics import YOLO

app = Flask(__name__)

# ================= CONFIG =================
ESP_IP = '192.168.1.13'
STREAM_URL = f'http://{ESP_IP}/stream'
DATA_URL = f'http://{ESP_IP}/sensors'

# Globals with thread safety
latest_detections = []
sensor_data = {
    'distance': 0.0,
    'waterTemp': -99.0,
    'surfaceTemp': -99.0,
    'pressure': -1.0,
    'altitude': -1.0,
    'turbidity': 0,
    'quality': "Unknown"
}

data_lock = Lock()
fps_counter = 0
last_fps_time = time.time()

# ================= LOAD YOLO MODEL =================
print("Loading YOLO model...")
model = YOLO('seafish.pt')

# Use GPU if available
if torch.cuda.is_available():
    model.to('cuda')
    print("✅ YOLO model loaded on GPU")
else:
    print("✅ YOLO model loaded on CPU")

# ================= MJPEG STREAM =================
def generate_frames():
    global latest_detections, fps_counter, last_fps_time
    
    retry_count = 0
    
    while True:
        try:
            stream = requests.get(STREAM_URL, stream=True, timeout=8)
            stream.raise_for_status()
            retry_count = 0
            bytes_data = b''
            
            print("✅ Connected to video stream")

            for chunk in stream.iter_content(chunk_size=8192):
                if not chunk:
                    continue
                    
                bytes_data += chunk
                a = bytes_data.find(b'\xff\xd8')
                b = bytes_data.find(b'\xff\xd9')

                if a != -1 and b != -1 and b > a:
                    jpg = bytes_data[a:b+2]
                    bytes_data = bytes_data[b+2:]

                    frame = cv2.imdecode(np.frombuffer(jpg, dtype=np.uint8), cv2.IMREAD_COLOR)
                    if frame is None:
                        continue

                    # YOLO Detection
                    results = model(frame, verbose=False, conf=0.45, iou=0.5)

                    labels = []
                    for r in results:
                        for box in r.boxes:
                            cls_id = int(box.cls[0])
                            label = model.names[cls_id]
                            conf = float(box.conf[0])
                            labels.append(label)

                            x1, y1, x2, y2 = map(int, box.xyxy[0])
                            cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 255, 100), 2)
                            cv2.putText(frame, f"{label} {conf:.2f}", 
                                        (x1, y1 - 10),
                                        cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 100), 2)

                    # Update shared data safely
                    with data_lock:
                        latest_detections = list(set(labels))

                    # FPS Calculation
                    fps_counter += 1
                    if time.time() - last_fps_time > 1.0:
                        with data_lock:
                            global_fps = fps_counter
                        fps_counter = 0
                        last_fps_time = time.time()

                    # Encode and stream
                    _, buffer = cv2.imencode('.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, 82])
                    frame_bytes = buffer.tobytes()

                    yield (b'--frame\r\n'
                           b'Content-Type: image/jpeg\r\n\r\n' + frame_bytes + b'\r\n')

        except Exception as e:
            retry_count += 1
            print(f"Stream error (retry {retry_count}/5): {e}")
            time.sleep(min(retry_count * 1.5, 5))


# ================= SENSOR POLLER =================
def sensor_poller():
    global sensor_data
    while True:
        try:
            r = requests.get(DATA_URL, timeout=5)
            if r.status_code == 200:
                data = r.json()
                with data_lock:
                    sensor_data.update(data)
        except Exception as e:
            pass  # Silent fail, retry
        time.sleep(2.5)


# Start background thread
threading.Thread(target=sensor_poller, daemon=True).start()

# ================= ROUTES =================
@app.route('/video')
def video():
    return Response(generate_frames(),
                    mimetype='multipart/x-mixed-replace; boundary=frame')


@app.route('/api/data')
def data():
    with data_lock:
        return jsonify({
            **sensor_data,
            "detections": latest_detections.copy(),
            "timestamp": time.time()
        })


@app.route('/')
def index():
    return render_template_string("""
<!DOCTYPE html>
<html>
<head>
<title>AI Marine Dashboard</title>
<link href="https://fonts.googleapis.com/css2?family=Orbitron:wght@400;700;900&family=Share+Tech+Mono&display=swap" rel="stylesheet">
<style>
    /* (Your original beautiful CSS - kept unchanged for brevity) */
    *{box-sizing:border-box;margin:0;padding:0}
    :root{
        --ocean:#00e5ff;--ocean-dim:#006e7f;--deep:#020c1b;
        --surface:#071428;--card:#0a1f3a;--glow:rgba(0,229,255,0.12);
        --warn:#ff9f1c;--safe:#00f5a0;--text:#cce8f4;--muted:#4a7a96;
    }
    body{
        margin:0;font-family:'Share Tech Mono',monospace;
        background:var(--deep);color:var(--text);overflow:hidden;
    }
    body::after{
        content:'';position:fixed;inset:0;
        background:repeating-linear-gradient(0deg,transparent,transparent 2px,rgba(0,0,0,0.06) 2px,rgba(0,0,0,0.06) 4px);
        pointer-events:none;z-index:999;
    }
    .container{display:grid;grid-template-columns:1fr 380px;height:100vh;}
    .header-bar{
        grid-column:1/-1;background:var(--surface);border-bottom:1px solid var(--ocean-dim);
        display:flex;align-items:center;justify-content:space-between;padding:0 20px;height:48px;
    }
    .logo{font-family:'Orbitron',sans-serif;font-size:15px;font-weight:900;color:var(--ocean);letter-spacing:4px;}
    .video-section{padding:16px;display:flex;flex-direction:column;gap:10px;flex:1;}
    .video-wrapper{
        flex:1;position:relative;border-radius:10px;overflow:hidden;
        border:1px solid var(--ocean-dim);background:#000;
    }
    .video-wrapper img{width:100%;height:100%;object-fit:cover;}
    .panel{
        background:var(--surface);border-left:1px solid var(--ocean-dim);
        padding:14px 12px;overflow-y:auto;
    }
    .card{
        background:var(--card);border:1px solid rgba(0,229,255,0.15);
        border-radius:8px;padding:12px 14px;margin-bottom:10px;
    }
    .badge{
        background:rgba(0,229,255,0.1);color:var(--ocean);
        border:1px solid rgba(0,229,255,0.35);padding:4px 10px;
        border-radius:4px;font-size:11px;
    }
    .data-value{font-family:'Orbitron',monospace;font-size:15px;font-weight:700;color:var(--ocean);}
    .green{background:var(--safe);box-shadow:0 0 8px var(--safe);}
    .red{background:#ff3b3b;box-shadow:0 0 8px #ff3b3b;}
</style>
</head>
<body>
<div class="main-wrap">
    <div class="header-bar">
        <div class="logo">▲ MARINEAI</div>
        <div style="font-size:11px;color:#4a7a96">DEEP SEA INTELLIGENCE SYSTEM</div>
        <div class="clock" id="clock">--:--:--</div>
    </div>

    <div style="display:flex;height:calc(100vh - 48px)">
        <!-- Video -->
        <div class="video-section">
            <div style="font-family:Orbitron;font-size:11px;color:var(--ocean);letter-spacing:2px;margin-bottom:8px">
                LIVE FEED — CAM-01 <span id="fps" style="color:#00ff9d"></span>
            </div>
            <div class="video-wrapper">
                <img src="/video" alt="Live Feed">
            </div>
        </div>

        <!-- Sidebar -->
        <div class="panel">
            <div style="text-align:center;font-family:Orbitron;font-size:12px;letter-spacing:4px;color:var(--ocean);margin-bottom:15px">
                SHIP AI SYSTEM
            </div>

            <!-- Detections -->
            <div class="card">
                <div style="font-size:10px;color:var(--muted);margin-bottom:8px">DETECTED MARINE LIFE</div>
                <div id="detections" style="display:flex;flex-wrap:wrap;gap:6px"></div>
            </div>

            <!-- Sensor Data -->
            <div class="card">
                <div style="font-size:10px;color:var(--muted);margin-bottom:10px">ENVIRONMENTAL SENSORS</div>
                <div id="sensor-data"></div>
            </div>
        </div>
    </div>
</div>

<script>
function updateClock() {
    document.getElementById('clock').textContent = new Date().toLocaleTimeString('en-GB', {hour12:false});
}
setInterval(updateClock, 1000);
updateClock();

async function fetchData() {
    try {
        const res = await fetch('/api/data');
        const d = await res.json();

        // Detections
        const detDiv = document.getElementById('detections');
        if (d.detections && d.detections.length > 0) {
            detDiv.innerHTML = d.detections.map(obj => 
                `<span class="badge">${obj}</span>`
            ).join('');
        } else {
            detDiv.innerHTML = '<span style="color:#666;font-size:11px">No marine life detected</span>';
        }

        // Sensor Data
        document.getElementById('sensor-data').innerHTML = `
            <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px 8px;font-size:13px">
                <div><span style="color:#666">Distance</span><br><b>${d.distance}</b> m</div>
                <div><span style="color:#666">Water Temp</span><br><b>${d.waterTemp}</b> °C</div>
                <div><span style="color:#666">Surface Temp</span><br><b>${d.surfaceTemp}</b> °C</div>
                <div><span style="color:#666">Turbidity</span><br><b>${d.turbidity}</b> 
                    <span style="font-size:10px;padding:1px 6px;border-radius:3px;background:${d.quality==='Good'?'#00f5a022':'#ff9f1c22'}">
                        ${d.quality}
                    </span>
                </div>
            </div>
        `;

    } catch(e) {
        console.error("Data fetch error", e);
    }
}

setInterval(fetchData, 1200);
fetchData();
</script>
</body>
</html>
    """)

if __name__ == "__main__":
    print("🚀 MarineAI Dashboard Starting...")
    print("🌐 Open: http://localhost:5000")
    app.run(host='0.0.0.0', port=5000, debug=False)