import requests
import numpy as np
import cv2

url = "http://192.168.1.5/stream"

stream = requests.get(url, stream=True)

bytes_buffer = b''

for chunk in stream.iter_content(chunk_size=4096):
    bytes_buffer += chunk

    a = bytes_buffer.find(b'\xff\xd8')
    b = bytes_buffer.find(b'\xff\xd9')

    if a != -1 and b != -1:
        jpg = bytes_buffer[a:b+2]
        bytes_buffer = bytes_buffer[b+2:]

        frame = cv2.imdecode(
            np.frombuffer(jpg, dtype=np.uint8),
            cv2.IMREAD_COLOR
        )

        if frame is not None:
            cv2.imshow("ESP32 Stream", frame)

            if cv2.waitKey(1) & 0xFF == 27:
                break

cv2.destroyAllWindows()