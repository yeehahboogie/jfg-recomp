"""Virtual gamepad (uinput) for driving an emulator from a timed script, without touching keyboard focus.
Usage: vpad.py guid            -> print SDL3 GUID / ares binding strings
       vpad.py run SCRIPT      -> create the pad, wait for SIGUSR1-free start, replay SCRIPT ('T+D BUTTONS [X Y]; ...')"""
import sys, time
NAME, BUS, VENDOR, PRODUCT, VERSION = "JFG Test Pad", 0x03, 0x1209, 0x0001, 0x0001

def crc16(data: bytes, crc: int = 0) -> int:  # SDL_crc16: CRC-16/ARC
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc

def guid() -> str:
    c = crc16(NAME.encode())
    words = [BUS, c, VENDOR, 0, PRODUCT, 0, VERSION]
    raw = b"".join(w.to_bytes(2, "little") for w in words) + bytes([0, 0])
    return raw.hex()

# button order as SDL enumerates evdev keys (ascending codes from BTN_JOYSTICK)
from evdev import UInput, AbsInfo, ecodes as e
BUTTONS = [e.BTN_SOUTH, e.BTN_EAST, e.BTN_TL, e.BTN_TR, e.BTN_TL2, e.BTN_START]
NAMES = {"A": e.BTN_SOUTH, "B": e.BTN_EAST, "L": e.BTN_TL, "R": e.BTN_TR, "Z": e.BTN_TL2, "START": e.BTN_START}

def bindings() -> dict:
    g = guid() + "/0"
    return {"Start": f"{g}/3/5", "A..South": f"{g}/3/0", "B..East": f"{g}/3/1", "L-Bumper": f"{g}/3/2",
            "R-Bumper": f"{g}/3/3", "L-Trigger": f"{g}/3/4", "L-Up": f"{g}/0/1/Lo", "L-Down": f"{g}/0/1/Hi",
            "L-Left": f"{g}/0/0/Lo", "L-Right": f"{g}/0/0/Hi"}

if sys.argv[1] == "guid":
    print(guid())
    for k, v in bindings().items(): print(f"{k}: {v}")
    sys.exit(0)

events = []
for entry in sys.argv[2].split(";"):
    f = entry.split()
    if not f: continue
    t, d = map(float, f[0].split("+"))
    btns = [] if f[1] == "-" else [NAMES[n] for n in f[1].split("+")]
    x, y = (float(f[2]), float(f[3])) if len(f) > 3 else (0.0, 0.0)
    events.append((t, d, btns, x, y))
cap = {e.EV_KEY: BUTTONS, e.EV_ABS: [(e.ABS_X, AbsInfo(0, -32768, 32767, 16, 128, 0)), (e.ABS_Y, AbsInfo(0, -32768, 32767, 16, 128, 0))]}
ui = UInput(cap, name=NAME, bustype=BUS, vendor=VENDOR, product=PRODUCT, version=VERSION)
print("pad created; script starts now", flush=True)
start = time.time()
end = max(t + d for t, d, *_ in events) + 1
state = None
while time.time() - start < end:
    now = time.time() - start
    held, x, y = set(), 0.0, 0.0
    for t, d, btns, ex, ey in events:
        if t <= now < t + d:
            held |= set(btns); x += ex; y += ey
    new = (frozenset(held), x, y)
    if new != state:
        for b in BUTTONS: ui.write(e.EV_KEY, b, 1 if b in held else 0)
        ui.write(e.EV_ABS, e.ABS_X, int(max(-1, min(1, x)) * 32767))
        ui.write(e.EV_ABS, e.ABS_Y, int(-max(-1, min(1, y)) * 32767))  # evdev Y grows downwards
        ui.syn(); state = new
    time.sleep(0.01)
ui.close()
