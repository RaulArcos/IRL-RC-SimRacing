import cv2
import time

URL = "udp://@0.0.0.0:5600?fifo_size=500000&overrun_nonfatal=1"

cap = cv2.VideoCapture(URL, cv2.CAP_FFMPEG)
if not cap.isOpened():
    raise SystemExit("Could not open UDP video stream. Check firewall, port, sender IP.")

cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

last = time.time()
frames = 0

while True:
    ok, frame = cap.read()
    if not ok:
        time.sleep(0.01)
        continue

    frames += 1
    now = time.time()
    if now - last >= 1.0:
        print(f"FPS: {frames}")
        frames = 0
        last = now

    cv2.imshow("RC Cam", frame)
    if cv2.waitKey(1) & 0xFF == 27:  # ESC
        break

cap.release()
cv2.destroyAllWindows()
