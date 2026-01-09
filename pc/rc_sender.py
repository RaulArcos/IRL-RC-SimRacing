import os
os.environ["SDL_JOYSTICK_RAWINPUT"] = "1"
os.environ["SDL_JOYSTICK_HIDAPI"] = "1"  # if needed, try "0"

import socket
import struct
import time
import sys
import configparser

import pygame
import pywinusb.hid as hid

CONFIG_PATH = "config.ini"

MAGIC = b"IRL1"
FLAG_ENABLE = 0x0001
PKT_FMT = "!4sIhhHH"

def clamp(x, lo, hi):
    return lo if x < lo else hi if x > hi else x

# -------------------- HID helpers --------------------
def axis16_le(report, idx):
    if report is None or idx + 1 >= len(report):
        return 0
    return report[idx] | (report[idx + 1] << 8)

def normalize_u16_to_01(v, vmin, vmax):
    if vmax <= vmin:
        return 0.0
    return clamp((v - vmin) / (vmax - vmin), 0.0, 1.0)

def find_moving_bytes(baseline, current, threshold=2):
    if not baseline or not current or len(baseline) != len(current):
        return []
    return [i for i, (a, b) in enumerate(zip(baseline, current)) if abs(b - a) >= threshold]

class HidPedals:
    def __init__(self, device_path: str):
        self.device = None
        self.last_report = None

        devs = hid.HidDeviceFilter().get_devices()
        for d in devs:
            if d.device_path == device_path:
                self.device = d
                break

        if not self.device:
            raise SystemExit("Pedals HID device not found. Unplug/replug or re-run configuration.")

        self.device.open()
        self.device.set_raw_data_handler(self._on_data)

        print(f"[HID] Using pedals: {self.device.product_name}")

    def _on_data(self, data):
        self.last_report = data

    def get_report(self):
        return self.last_report

    def close(self):
        try:
            self.device.close()
        except:
            pass

# -------------------- pygame helpers --------------------
def list_pygame_devices():
    pygame.joystick.init()
    count = pygame.joystick.get_count()
    devs = []
    print(f"\nDetected {count} pygame device(s):")
    for i in range(count):
        js = pygame.joystick.Joystick(i)
        js.init()
        devs.append(js)
        print(f"  [{i}] {js.get_name()} axes={js.get_numaxes()} buttons={js.get_numbuttons()}")
    return devs

def pick_int(prompt, lo, hi):
    while True:
        try:
            v = int(input(prompt).strip())
            if lo <= v <= hi:
                return v
            print(f"Enter a number between {lo} and {hi}.")
        except ValueError:
            print("Enter a number.")
        except KeyboardInterrupt:
            sys.exit(0)

# -------------------- config.ini --------------------
def load_config():
    cfg = configparser.ConfigParser()
    if not os.path.exists(CONFIG_PATH):
        return None
    cfg.read(CONFIG_PATH, encoding="utf-8")

    # Basic validation
    if "network" not in cfg or "wheel" not in cfg or "pedals" not in cfg:
        return None

    return cfg

def save_config(cfg: configparser.ConfigParser):
    with open(CONFIG_PATH, "w", encoding="utf-8") as f:
        cfg.write(f)
    print(f"\nSaved configuration to {CONFIG_PATH}")

# -------------------- guided setup --------------------
def guided_setup(pygame_devs):
    cfg = configparser.ConfigParser()

    print("\n=== Network ===")
    ip = input("Raspberry Pi IP (e.g. 192.168.1.50): ").strip()
    port = input("UDP port [6001]: ").strip()
    port = int(port) if port else 6001

    cfg["network"] = {"ip": ip, "port": str(port), "send_hz": "20"}

    print("\n=== Steering (pygame) ===")
    steer_dev = pick_int(f"Select steering device [0-{len(pygame_devs)-1}]: ", 0, len(pygame_devs)-1)
    steer_axis = pick_int(f"Select steering axis [0-{pygame_devs[steer_dev].get_numaxes()-1}]: ", 0, pygame_devs[steer_dev].get_numaxes()-1)

    cfg["wheel"] = {
        "device_index": str(steer_dev),
        "axis_index": str(steer_axis),
        "enable_always_on": "true",
        "enable_button": "0",
    }

    print("\n=== Pedals (HID) ===")
    print("Available HID devices containing 'Pedals' / 'Sim' / 'T-LCM':")
    devs = hid.HidDeviceFilter().get_devices()
    candidates = []
    for d in devs:
        name = d.product_name or ""
        if any(k in name.lower() for k in ["pedal", "sim", "tlcm", "t-lcm", "thrustmaster"]):
            candidates.append(d)

    if not candidates:
        # Fallback: show all
        candidates = [d for d in devs if d.product_name]

    for i, d in enumerate(candidates):
        print(f"  [{i}] {d.product_name}  VID={hex(d.vendor_id)} PID={hex(d.product_id)}")

    idx = pick_int(f"Select pedals HID device [0-{len(candidates)-1}]: ", 0, len(candidates)-1)
    pedals_dev = candidates[idx]
    print(f"Selected pedals: {pedals_dev.product_name}")

    # open pedals
    pedals_dev.open()
    last_report = {"data": None}
    def on_data(data):
        last_report["data"] = data
    pedals_dev.set_raw_data_handler(on_data)

    # Wait for first report
    print("Waiting for pedal HID reports...")
    t0 = time.time()
    while last_report["data"] is None and time.time() - t0 < 3.0:
        time.sleep(0.01)
    if last_report["data"] is None:
        pedals_dev.close()
        raise SystemExit("No HID reports received from pedals. Try unplug/replug and rerun.")

    baseline = last_report["data"]

    print("\nMapping bytes that move (helps you pick correct indices).")
    input("Press Enter, then press/release THROTTLE for ~3 seconds...")
    thr_moves = set()
    t0 = time.time()
    while time.time() - t0 < 3.0:
        cur = last_report["data"]
        if cur:
            for j in find_moving_bytes(baseline, cur, threshold=2):
                thr_moves.add(j)
        time.sleep(0.01)
    print("Throttle moving indices:", sorted(thr_moves))

    input("\nPress Enter, then press/release BRAKE for ~3 seconds...")
    baseline2 = last_report["data"] or baseline
    brk_moves = set()
    t0 = time.time()
    while time.time() - t0 < 3.0:
        cur = last_report["data"]
        if cur:
            for j in find_moving_bytes(baseline2, cur, threshold=2):
                brk_moves.add(j)
        time.sleep(0.01)
    print("Brake moving indices:", sorted(brk_moves))

    print("\nPick LOW-byte indices for each 16-bit axis (little-endian).")
    thr_lo = int(input("Throttle LOW byte index: ").strip())
    brk_lo = int(input("Brake LOW byte index: ").strip())

    input("\nRelease BOTH pedals fully, then press Enter...")
    time.sleep(0.3)
    rep = last_report["data"]
    thr_min = axis16_le(rep, thr_lo)
    brk_min = axis16_le(rep, brk_lo)
    print(f"Rest: throttle={thr_min} brake={brk_min}")

    input("Press THROTTLE fully and hold, then press Enter...")
    time.sleep(0.2)
    rep = last_report["data"]
    thr_max = axis16_le(rep, thr_lo)
    print(f"Throttle max={thr_max}")

    input("Release throttle. Press BRAKE fully and hold, then press Enter...")
    time.sleep(0.2)
    rep = last_report["data"]
    brk_max = axis16_le(rep, brk_lo)
    print(f"Brake max={brk_max}")

    pedals_dev.close()

    cfg["pedals"] = {
        "device_path": pedals_dev.device_path,
        "thr_lo": str(thr_lo),
        "brk_lo": str(brk_lo),
        "thr_min": str(thr_min),
        "thr_max": str(thr_max),
        "brk_min": str(brk_min),
        "brk_max": str(brk_max),
    }

    save_config(cfg)
    return cfg

# -------------------- main loop --------------------
def main():
    pygame.init()
    pygame.joystick.init()

    cfg = load_config()

    devs = list_pygame_devices()
    if not devs:
        raise SystemExit("No pygame devices detected (wheel not visible).")

    if cfg is None:
        print("\nNo valid config.ini found. Running setup once...")
        cfg = guided_setup(devs)

    ip = cfg["network"]["ip"]
    port = int(cfg["network"].get("port", "6001"))
    send_hz = float(cfg["network"].get("send_hz", "20"))

    wheel_dev = int(cfg["wheel"]["device_index"])
    wheel_axis = int(cfg["wheel"]["axis_index"])
    enable_always_on = cfg["wheel"].get("enable_always_on", "true").lower() == "true"
    enable_button = int(cfg["wheel"].get("enable_button", "0"))

    pedals_path = cfg["pedals"]["device_path"]
    thr_lo = int(cfg["pedals"]["thr_lo"])
    brk_lo = int(cfg["pedals"]["brk_lo"])
    thr_min = int(cfg["pedals"]["thr_min"])
    thr_max = int(cfg["pedals"]["thr_max"])
    brk_min = int(cfg["pedals"]["brk_min"])
    brk_max = int(cfg["pedals"]["brk_max"])

    if wheel_dev < 0 or wheel_dev >= len(devs):
        raise SystemExit("Wheel device index no longer valid. Delete config.ini and re-run setup.")

    wheel = devs[wheel_dev]

    pedals = HidPedals(pedals_path)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    seq = 0

    dt = 1.0 / send_hz
    next_t = time.perf_counter()
    last_print = 0.0

    print(f"\nSending to {ip}:{port} at {send_hz:.0f}Hz. Ctrl+C to stop.")
    while True:
        pygame.event.pump()

        steer_raw = wheel.get_axis(wheel_axis)
        steer = clamp(-steer_raw * 5.0, -1.0, 1.0)
        steer_pm = clamp(int(steer * 1000), -1000, 1000)

        rep = pedals.get_report()
        if rep is None:
            throttle01 = 0.0
            brake01 = 0.0
        else:
            thr = axis16_le(rep, thr_lo)
            brk = axis16_le(rep, brk_lo)
            throttle01 = normalize_u16_to_01(thr, thr_min, thr_max)
            brake01    = normalize_u16_to_01(brk, brk_min, brk_max)

        power = throttle01 - brake01
        power_pm = clamp(int(power * 1000), -1000, 1000)

        flags = FLAG_ENABLE if enable_always_on else 0
        if not enable_always_on:
            if wheel.get_button(enable_button):
                flags |= FLAG_ENABLE

        pkt = struct.pack(PKT_FMT, MAGIC, seq, steer_pm, power_pm, flags, 0)
        sock.sendto(pkt, (ip, port))
        seq = (seq + 1) & 0xFFFFFFFF

        now = time.perf_counter()
        if now - last_print > 0.1:
            print(
                f"steer={steer:+.3f}({steer_pm:+5d}) "
                f"th={throttle01:.3f} br={brake01:.3f} "
                f"power={power:+.3f}({power_pm:+5d})",
                end="\r",
            )
            last_print = now

        next_t += dt
        sleep = next_t - time.perf_counter()
        if sleep > 0:
            time.sleep(sleep)
        else:
            next_t = time.perf_counter()

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped.")
