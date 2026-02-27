import socket
import threading
import cv2
import json
import time
import random

# Get local IP address
def get_local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

PORT = 8080
LOCAL_IP = get_local_ip()

# Start socket server
server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server_socket.bind((LOCAL_IP, PORT))
server_socket.listen(5)

print(f"\n🔥 Server running at: http://{LOCAL_IP}:{PORT}\n")

# Initialize camera
camera = cv2.VideoCapture(0)

def get_sensor_data():
    return random.randint(400, 1000)

def handle_robot_command(client_socket, command):
    print(f"Robot Command Received: {command}")
    client_socket.sendall(
        b"HTTP/1.1 200 OK\r\n"
        b"Content-Type: application/json\r\n"
        b"Access-Control-Allow-Origin: *\r\n\r\n"
        b'{"status": "command received"}'
    )
    client_socket.close()

def stream_video(client_socket):
    try:
        client_socket.sendall(
            b"HTTP/1.1 200 OK\r\n"
            b"Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n"
        )
        while camera.isOpened():
            ret, frame = camera.read()
            if not ret:
                break
            _, buffer = cv2.imencode('.jpg', frame)
            frame_bytes = buffer.tobytes()
            client_socket.sendall(
                b"--frame\r\n"
                b"Content-Type: image/jpeg\r\n\r\n" + frame_bytes + b"\r\n"
            )
            time.sleep(0.05)
    except:
        print("Video stream closed.")
    finally:
        client_socket.close()

def handle_sensor_data(client_socket):
    value = get_sensor_data()
    response = json.dumps({"value": value})
    client_socket.sendall(
        b"HTTP/1.1 200 OK\r\n"
        b"Content-Type: application/json\r\n"
        b"Access-Control-Allow-Origin: *\r\n\r\n" +
        response.encode()
    )
    client_socket.close()

def handle_client(client_socket, addr):
    print(f"Connected by {addr}")
    try:
        request = client_socket.recv(1024).decode()
        if "GET /video" in request:
            stream_video(client_socket)
        elif "GET /data" in request:
            handle_sensor_data(client_socket)
        elif "POST /command" in request:
            command = request.split('\r\n')[-1]
            handle_robot_command(client_socket, command)
        else:
            html = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Grain Quality Robot - Webcam & Control</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body {{ font-family: Arial; text-align: center; background: #f4f4f4; margin: 0; padding: 0; }}
        header {{ background: #4CAF50; color: white; padding: 20px; }}
        img {{ width: 80%; max-width: 800px; margin: 20px; border: 5px solid #4CAF50; box-shadow: 0 0 10px #888; }}
        canvas {{ width: 80%; max-width: 800px; height: 400px; margin: 20px auto; display: block; }}
        button {{ font-size: 24px; padding: 15px 30px; margin: 10px; }}
        .controls {{ display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; width: 300px; margin: 20px auto; }}
    </style>
</head>
<body>
    <header>
        <h1>Grain Quality Robot - Webcam & Control</h1>
    </header>
    <h2>Access from another device</h2>
    <a href="http://{LOCAL_IP}:{PORT}" target="_blank">📱 Open on iPhone: http://{LOCAL_IP}:{PORT}</a>
    <h2>Live Webcam Stream</h2>
    <img src="/video" alt="Webcam Stream">
    <h2>Robot Controls</h2>
    <div class="controls">
        <button onclick="sendCommand('UP')">⬆️ Forward</button>
        <button onclick="sendCommand('LEFT')">⬅️ Left</button>
        <button onclick="sendCommand('RIGHT')">➡️ Right</button>
        <button onclick="sendCommand('DOWN')">⬇️ Backward</button>
    </div>
    <h2>Real-time CO₂ Monitoring</h2>
    <canvas id="chart"></canvas>

    <script>
        const ctx = document.getElementById('chart').getContext('2d');
        const chart = new Chart(ctx, {{
            type: 'line',
            data: {{
                labels: [],
                datasets: [{{
                    label: 'CO₂ (ppm)',
                    borderColor: 'rgb(75, 192, 192)',
                    backgroundColor: 'rgba(75, 192, 192, 0.2)',
                    data: [],
                    fill: true
                }}]
            }},
            options: {{
                responsive: true,
                scales: {{
                    x: {{ display: true, title: {{ display: true, text: 'Time' }} }},
                    y: {{ display: true, title: {{ display: true, text: 'CO₂ (ppm)' }} }}
                }}
            }}
        }});

        function updateChart(data) {{
            if (chart.data.labels.length > 20) {{
                chart.data.labels.shift();
                chart.data.datasets[0].data.shift();
            }}
            chart.data.labels.push(new Date().toLocaleTimeString());
            chart.data.datasets[0].data.push(data);
            chart.update();
        }}

        function sendCommand(command) {{
            fetch('/command', {{
                method: 'POST',
                body: command
            }}).then(res => res.json())
              .then(d => console.log(d))
              .catch(err => console.error(err));
        }}

        setInterval(() => {{
            fetch('/data')
                .then(res => res.json())
                .then(data => updateChart(data.value))
                .catch(err => console.error(err));
        }}, 1000);
    </script>
</body>
</html>"""
            client_socket.sendall(
                b"HTTP/1.1 200 OK\r\n"
                b"Content-Type: text/html\r\n"
                b"Access-Control-Allow-Origin: *\r\n\r\n" +
                html.encode()
            )
    except Exception as e:
        print("Client error:", e)
    finally:
        client_socket.close()

# Main loop
try:
    while True:
        client_sock, addr = server_socket.accept()
        threading.Thread(target=handle_client, args=(client_sock, addr)).start()
except KeyboardInterrupt:
    print("Server shutting down...")
    camera.release()
    server_socket.close()