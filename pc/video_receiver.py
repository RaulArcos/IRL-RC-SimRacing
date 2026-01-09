import av
import cv2

url = "udp://0.0.0.0:5600?listen=1&localaddr=0.0.0.0&overrun_nonfatal=1&fifo_size=50000"

opts = {
    "fflags": "nobuffer",
    "flags": "low_delay",
    "flush_packets": "1",
    "probesize": "32",
    "analyzeduration": "0",
}

print("Opening:", url)
container = av.open(url, format="mpegts", options=opts)

for frame in container.decode(video=0):
    img = frame.to_ndarray(format="bgr24")
    cv2.imshow("RC Cam", img)
    if cv2.waitKey(1) == 27:
        break
