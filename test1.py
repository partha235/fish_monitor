import cv2
import numpy as np
import serial
import time

# -------- SERIAL SETUP --------
ser = serial.Serial('/dev/ttyUSB0', 9600)
time.sleep(2)

# -------- CAMERA --------
cap = cv2.VideoCapture(1)

while True:
    ret, frame = cap.read()
    if not ret:
        break

    frame = cv2.resize(frame, (640, 480))

    # Convert to HSV
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)

    # 🔥 Fire color range
    lower1 = np.array([0, 150, 150])
    upper1 = np.array([15, 255, 255])

    lower2 = np.array([18, 150, 150])
    upper2 = np.array([35, 255, 255])

    mask1 = cv2.inRange(hsv, lower1, upper1)
    mask2 = cv2.inRange(hsv, lower2, upper2)

    fire_mask = mask1 + mask2
    fire_pixels = cv2.countNonZero(fire_mask)
    print("value = ",fire_pixels)

    # -------- FIRE LOGIC --------
    if fire_pixels > 1500:
        # 🔥 FIRE irukkura ella frame-layum
        print("🔥 FIRE DETECTED")
        ser.write(b'FIRE\n')

        cv2.putText(frame, "🔥 FIRE DETECTED", (20, 40),
                    cv2.FONT_HERSHEY_SIMPLEX, 1,
                    (0, 0, 255), 2)
        # time.sleep(0.5)
    else:
        print("SAFE")
        ser.write(b'SAFE\n')

        cv2.putText(frame, "SAFE", (20, 40),
                    cv2.FONT_HERSHEY_SIMPLEX, 1,
                    (0, 255, 0), 2)
        # time.sleep(0.5)

    cv2.imshow("Fire Detection", frame)

    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

cap.release()
cv2.destroyAllWindows()
ser.close() 