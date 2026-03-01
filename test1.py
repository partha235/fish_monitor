import cv2
import numpy as np
import requests
import time

# ────────────────────────────────────────────────
# Config
# ────────────────────────────────────────────────
STREAM_URL = "http://192.168.1.4:81/stream"   # ← double-check this is really an MJPEG stream
SAVE_FILENAME = "insect_{}.jpg"               # timestamp will be added
RECONNECT_DELAY = 3                           # seconds to wait before retry after error

# ────────────────────────────────────────────────
def main():
    while True:
        stream = None
        try:
            # Use stream=True → keeps connection open (important for MJPEG)
            stream = requests.get(STREAM_URL, stream=True, timeout=10)
            
            if stream.status_code != 200:
                print(f"Server returned {stream.status_code} — retrying in {RECONNECT_DELAY}s...")
                time.sleep(RECONNECT_DELAY)
                continue

            bytes_data = bytes()
            print("Stream connected — press 'q' to quit, 's' to save current frame")

            for chunk in stream.iter_content(chunk_size=1024):
                if chunk:
                    bytes_data += chunk
                    # MJPEG boundary: look for start & end of JPEG frame
                    a = bytes_data.find(b'\xff\xd8')         # JPEG SOI
                    b = bytes_data.find(b'\xff\xd9')         # JPEG EOI

                    if a != -1 and b != -1 and a < b:
                        jpg = bytes_data[a:b+2]
                        bytes_data = bytes_data[b+2:]       # remove processed frame

                        img_array = np.frombuffer(jpg, dtype=np.uint8)
                        frame = cv2.imdecode(img_array, cv2.IMREAD_COLOR)

                        if frame is None:
                            continue

                        # Show the frame
                        cv2.imshow('ESP32-CAM Stream', frame)

                        key = cv2.waitKey(1) & 0xFF

                        if key == ord('q'):
                            print("Quit requested.")
                            return

                        elif key == ord('s'):
                            timestamp = time.strftime("%Y%m%d_%H%M%S")
                            filename = SAVE_FILENAME.format(timestamp)
                            cv2.imwrite(filename, frame)
                            print(f"Saved: {filename}")

        except requests.exceptions.RequestException as e:
            print(f"Connection error: {e}")
        except Exception as e:
            print(f"Unexpected error: {e}")
        finally:
            if stream:
                try:
                    stream.close()
                except:
                    pass

        print(f"Reconnecting in {RECONNECT_DELAY} seconds...")
        time.sleep(RECONNECT_DELAY)

    # Cleanup (only reached if we break the outer loop)
    cv2.destroyAllWindows()


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print("\nInterrupted by user.")
    finally:
        cv2.destroyAllWindows()