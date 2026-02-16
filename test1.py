import cv2
import numpy as np
import requests
import time

url = "http://192.168.1.7/capture"

while True:
    try:
        response = requests.get(url, timeout=5)

        if response.status_code != 200:
            print("Failed to get image")
            continue

        img_array = np.frombuffer(response.content, dtype=np.uint8)
        img = cv2.imdecode(img_array, cv2.IMREAD_COLOR)

        if img is None:
            print("Image decode failed")
            continue

        cv2.imshow("ESP32-CAM", img)

        cv2.imwrite("insect.jpg", img)

        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

        time.sleep(20)  # match your ESP refresh (20 sec)

    except Exception as e:
        print("Error:", e)
        break

cv2.destroyAllWindows()
