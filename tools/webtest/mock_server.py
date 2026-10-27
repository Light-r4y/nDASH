#!/usr/bin/env python3
"""
Standalone mock server for trying the nDash web interface in an ordinary browser on a
computer - without the firmware, an ESP32 or a CAN bus.

It serves the static files straight from ../../www (the same set that goes into the www
partition image) and replaces the /api/* endpoints with plausible data kept in memory
for as long as the process runs. The built-in layout and CAN table are taken from the
same headers that are compiled into the firmware (layout_default.h, can_map_default.h),
the /ota page from tasks/webcfg/ota_page.h, and the validation of layout, CAN table and
settings follows the C code, so what the mock refuses the board refuses too.

Run:
    python tools/webtest/mock_server.py [port] [--password PASSWORD]   (default port 8088)

--password turns the settings password on, as if it were set on the board, so the
password prompt can be tried.

/ota: the image upload is only simulated - the body is read slowly and the reply is
"success" (a write error with ?fail=1, a dropped connection with ?hang=1, a reply that
never comes with ?stall=1, the same image with ?same=1). Nothing is written.
"""
import base64
import http.server
import json
import math
import os
import random
import re
import struct
import sys
import threading
import time
import urllib.parse

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
WWW_DIR = os.path.join(ROOT, "www")
BENCH_HOLD_MS = 6000     # LND_BENCH_HOLD_MS

# The log of the microSD card (tasks/logger): a card that appears after "rescan", a recording
# that grows with the time, files that can be downloaded and deleted.
logs = {"card": False, "rec": None, "cfg": {"mode": "manual", "signal": "rpm", "op": ">", "value": 500.0, "stop_delay": 10},
        "files": [{"name": "log_0001.mlg", "size": 183425, "time": 1790000000}, {"name": "log_0002.mlg", "size": 20481, "time": 1790090000}],
        "next": 3}
LOG_NAME_RE = re.compile(r"^log_\d{4}\.mlg$")


def log_payload():
    rec = logs["rec"]
    secs = int(time.time() - rec["t0"]) if rec else 0
    return {"state": "recording" if rec else ("idle" if logs["card"] else "no_card"), "error": "",
            "file": rec["name"] if rec else "", "records": secs * 20, "bytes": 22 + secs * 20 * 45, "seconds": secs,
            "total_kb": 30535680 if logs["card"] else 0, "free_kb": 29000000 if logs["card"] else 0, **logs["cfg"]}
BENCH_RUNNING_RPM = 100  # LND_BENCH_RUNNING_RPM
BENCH_CMDS_MAX = 48
BENCH_CMDS_JSON_MAX = 8192


def bench_preset():
    """The same table as preset() in common/config/bench_cmds.c."""
    def c(name, label, group, sub, idx, dlc, stopped=True):
        return {"name": name, "label": label, "group": group, "id": 0x77000C, "ext": True, "sub": sub, "idx": idx,
                "dlc": dlc, "stopped_only": stopped}
    out = [c("ign%d" % i, "Coil %d" % i, "Ignition", 0x12, i, 8) for i in range(1, 9)]
    out += [c("inj%d" % i, "Injector %d" % i, "Injectors", 0x13, i, 8) for i in range(1, 9)]
    for name, label, idx in (("fuel_pump", "Fuel pump", 1), ("fan1", "Fan 1", 4), ("fan2", "Fan 2", 5),
                             ("main_relay", "Main relay", 0), ("ac_relay", "A/C relay", 6),
                             ("mil", "Check engine light", 7), ("starter", "Starter relay", 2),
                             ("idle_valve", "Idle valve", 8), ("boost_valve", "Boost valve", 43)):
        out.append(c(name, label, "Outputs", 0x16, idx, 6))
    out += [c("start_stop", "Start / stop engine", "Engine", 0x14, 9, 6, False),
            c("stop_engine", "Stop engine", "Engine", 0x24, 0, 6, False),
            c("cancel", "Abort test", "Engine", 0x16, 15, 6, False)]
    return out


def bench_cmds_valid(text):
    """lnd_bench_cmds_apply_json: the cleaned list, or None when any entry is wrong."""
    if len(text) == 0 or len(text) > BENCH_CMDS_JSON_MAX:
        return None
    try:
        data = json.loads(text)
    except ValueError:
        return None
    lst = data.get("cmds") if isinstance(data, dict) else None
    if not isinstance(lst, list) or len(lst) > BENCH_CMDS_MAX:
        return None
    out, names = [], set()
    for e in lst:
        if not isinstance(e, dict):
            return None
        name = e.get("name", "")
        ext = e.get("ext", True)
        cid = e.get("id", 0x77000C)
        if isinstance(cid, str):
            try:
                cid = int(cid, 0)
            except ValueError:
                return None
        dlc = e.get("dlc", 6)
        if (not re.fullmatch(r"[A-Za-z0-9_]{1,15}", str(name)) or name in names or not isinstance(cid, int)
                or cid < 0 or cid > (0x1FFFFFFF if ext else 0x7FF) or dlc not in (6, 8)
                or any(ch in str(e.get(k, "")) for k in ("label", "group") for ch in '"\\')):
            return None
        names.add(name)
        out.append({"name": name, "label": (str(e.get("label", "")) or name)[:23], "group": str(e.get("group", ""))[:15],
                    "id": cid, "ext": bool(ext), "sub": max(0, min(65535, int(e.get("sub", 0)))),
                    "idx": max(0, min(65535, int(e.get("idx", 0)))), "dlc": dlc,
                    "stopped_only": bool(e.get("stopped_only", True))})
    return out
bench = {"demo": False, "held": {}, "cmds": None}   # held: name -> (value, until); cmds None - the preset


def bench_signals():
    text = state["layout"] or c_string_define(LAYOUT_DEFAULT_H, "LND_LAYOUT_DEFAULT_JSON")
    try:
        return json.loads(text).get("signals") or []
    except ValueError:
        return []


def bench_payload():
    now = time.time()
    rows = []
    for i, sg in enumerate(bench_signals()):
        name, lo, hi = sg["name"], sg.get("min", 0), sg.get("max", 100)
        held = bench["held"].get(name)
        if held and held[1] < now:
            bench["held"].pop(name, None)
            held = None
        if held:
            value = held[0]
        else:
            period = 5.0 + (i % 4)
            x = ((now + i * 0.8) % period) / period
            tri = x * 2 if x < 0.5 else (1 - x) * 2
            value = lo + (hi - lo) * (tri if bench["demo"] else 0.5 + 0.1 * math.sin(now + i))
        rows.append({"name": name, "min": lo, "max": hi, "held": held is not None, "age": 0 if held else int(now * 1000) % 90,
                     "value": None if (not held and i == len(bench_signals()) - 1 and not bench["demo"]) else round(value, 2)})
    return {"demo": bench["demo"], "hold_ms": BENCH_HOLD_MS, "signals": rows}


MEDIA_DIR = os.path.join(ROOT, "media")
MEDIA_MAX_FILE = 1000000   # LND_MEDIA_MAX_FILE
MEDIA_TOTAL = 0x8E0000     # the size of the media partition
MEDIA_NAME_RE = re.compile(r"^(?!\.)[A-Za-z0-9._-]{1,31}$")
media = {}                 # file name -> bytes; starts with what is flashed from media/
VERSION_FILE = os.path.join(ROOT, "version.txt")
OTA_PAGE_H = os.path.join(ROOT, "tasks", "webcfg", "ota_page.h")
LAYOUT_DEFAULT_H = os.path.join(ROOT, "common", "config", "layout_default.h")
CAN_MAP_DEFAULT_H = os.path.join(ROOT, "common", "config", "can_map_default.h")

LAYOUT_JSON_MAX = 16384    # LND_LAYOUT_JSON_MAX
CAN_MAP_JSON_MAX = 4096    # LND_CAN_MAP_JSON_MAX
CAN_MAP_MAX = 24           # LND_CAN_MAP_MAX
MAX_SCREENS = 8            # LND_UI_MAX_SCREENS
BRIGHTNESS_MIN = 5         # LND_BRIGHTNESS_MIN_PCT
PASSWORD_MIN, PASSWORD_MAX = 4, 64           # LND_AUTH_PASSWORD_*
WIFI_PASS_MIN, WIFI_PASS_MAX = 8, 64         # LND_WIFI_AP_PASSWORD_*
WIFI_SSID_MAX = 32
WIFI_PASS_DEFAULT = "ndash1234"
START_TIME = time.time()

try:
    with open(VERSION_FILE, encoding="utf-8") as f:
        FW_VERSION = f.read().strip()
except OSError:
    FW_VERSION = "0.0.0-mock"


def c_string_define(path, name):
    """The value of `#define name "..." "..."`: C string literals glued together."""
    with open(path, encoding="utf-8") as f:
        text = f.read()
    body = text[text.index(name):]
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
    return "".join(parts).replace('\\"', '"').replace("\\\\", "\\")


lock = threading.RLock()

state = {
    "auth_password": "",
    "www_update": None,         # result of the last www update, as /api/status gives it on the board
    "build_id_override": None,  # the "new" /.build_id after a simulated update
    "layout": None,             # saved layout (str); None - the built-in one
    "can_map": None,            # saved CAN table (str); None - the rusEFI preset
    "can": {"bitrate": 500000, "mode": "normal", "heartbeat": True, "demo": False},
    "brightness": 80,
    "baro": "kpa",
    "boost": "bar",
    "button_pin": 0,
    "wifi": {"ssid": "", "password": WIFI_PASS_DEFAULT},
    "sniff_t": 0.0,             # when the web last polled the sniffer
}


# ---------------------------------------------------------------------------
# Validation as in the firmware
# ---------------------------------------------------------------------------
def layout_is_valid(text):
    """lnd_layout_apply_json: the JSON parses and there is at least one screen."""
    if len(text) == 0 or len(text) > LAYOUT_JSON_MAX:
        return False
    try:
        data = json.loads(text)
    except ValueError:
        return False
    screens = data.get("screens") if isinstance(data, dict) else None
    return isinstance(screens, list) and any(isinstance(s, dict) for s in screens[:MAX_SCREENS])


MAX_LIVE = 14    # LND_LAYOUT_MAX_LIVE
MAX_DIALS = 2    # LND_LAYOUT_MAX_DIALS
MAX_WIDGETS = 24


def _font_px(name, dflt):
    m = re.search(r"(\d+)$", name or "")
    return int(m.group(1)) if m else dflt


def layout_problem(text):
    """lnd_layout_check: the text of the refusal, or None."""
    data = json.loads(text)
    for sc in [s for s in data["screens"] if isinstance(s, dict)][:MAX_SCREENS]:
        widgets = [w for w in (sc.get("widgets") or []) if isinstance(w, dict)][:MAX_WIDGETS]
        live = dials = 0
        for i, w in enumerate(widgets):
            live += w.get("type") != "text"
            dials += w.get("type") == "dial"
            if live > MAX_LIVE or dials > MAX_DIALS:
                return "Too many widgets: up to %d live ones and %d dials per screen" % (MAX_LIVE, MAX_DIALS)
        for w in widgets:
            if w.get("type") not in ("value", "text"):
                continue
            px = _font_px(w.get("font"), 48 if w["type"] == "value" else 20)
            width = w.get("w", 150) if w["type"] == "value" else len(w.get("text", "")) * px * 0.6
            height = px * 1.25
            x, y = w.get("x", 0), w.get("y", 0)
            for d in widgets:
                if d.get("type") != "dial":
                    continue
                r = d.get("w", 460) / 2.0
                cx, cy = d.get("x", 0) + r, d.get("y", 0) + r
                nx, ny = min(max(cx, x), x + width), min(max(cy, y), y + height)
                if (nx - cx) ** 2 + (ny - cy) ** 2 < r * r:
                    return "A number or text cannot lie over a dial"
    return None


def _next_bit(pos, motorola):
    if not motorola:
        return pos + 1
    return pos + 15 if pos % 8 == 0 else pos - 1


def _field_fits(start, length, motorola):
    pos = start
    for _ in range(length):
        if pos < 0 or pos > 63:
            return False
        pos = _next_bit(pos, motorola)
    return True


def can_map_is_valid(text):
    """lnd_can_map_apply_json: any wrong entry rejects the whole table."""
    if len(text) == 0 or len(text) > CAN_MAP_JSON_MAX:
        return False
    try:
        data = json.loads(text)
    except ValueError:
        return False
    entries = data.get("entries") if isinstance(data, dict) else None
    if not isinstance(entries, list) or len(entries) > CAN_MAP_MAX:
        return False
    seen = set()
    for e in entries:
        try:
            sig = e["signal"]
            if not isinstance(sig, str) or not sig or sig in seen:
                return False
            seen.add(sig)
            eid = e["id"]
            eid = int(eid, 0) if isinstance(eid, str) else int(eid)
            ext = bool(e.get("ext", False))
            if eid < 0 or eid > (0x1FFFFFFF if ext else 0x7FF):
                return False
            start, length = int(e.get("start", 0)), int(e.get("len", 8))
            order = e.get("order", "intel")
            if order not in ("intel", "motorola") or not (0 <= start <= 63 and 1 <= length <= 32):
                return False
            if not _field_fits(start, length, order == "motorola"):
                return False
        except (KeyError, ValueError, TypeError):
            return False
    return True


# ---------------------------------------------------------------------------
# A fake CAN bus for the sniffer and the state
# ---------------------------------------------------------------------------
def _fake_bus():
    def rpm(t):
        return struct.pack("<H", int(3000 + 2500 * math.sin(t / 2.0))) + bytes([0, 0, 0, 0]) + bytes([int(80 + 60 * math.sin(t / 5.0)), 0])

    def f203(t):
        return struct.pack("<H", int(2800 + 600 * math.sin(t / 3.0))) + bytes([int(85 + 20 * math.sin(t / 7.0)) + 40, 60, 0, 0, 0, 100])

    def f204(t):
        return bytes([0, 0]) + struct.pack("<H", int(8000 + 2000 * math.sin(t / 4.0))) + bytes([int(95 + 10 * math.sin(t / 9.0)) + 40, 0]) + struct.pack("<H", 13800)

    def f207(t):
        return struct.pack("<H", int(6800 + 400 * math.sin(t))) + bytes([0, 0]) + struct.pack("<H", 9000) + bytes([0, 0])

    def foreign(t):
        return bytes([int(t * 10) & 0xFF, 0xAA, 0, 0, 0, 0, 0, int(t) & 0xFF])

    # id, ext, period (s), data function; the first four are in the built-in table
    return [
        (0x201, False, 0.01, rpm), (0x203, False, 0.02, f203), (0x204, False, 0.05, f204),
        (0x207, False, 0.05, f207), (0x7E8, False, 1.0, lambda t: bytes([3, 0x41, 0x0D, 0x40, 0, 0, 0, 0])),
        (0x18FEF100, True, 0.1, foreign),
    ]


def fake_frames_payload():
    """Like GET /api/can/frames: IDs outside the table come through only while the web is
    polling (3 s after the last request), so the first reply shows the table's IDs only."""
    now = time.time()
    warm = (now - state["sniff_t"]) < 3.0
    state["sniff_t"] = now
    t = now - START_TIME
    out = []
    for i, (fid, ext, period, fn) in enumerate(_fake_bus()):
        if i >= 4 and not warm:
            continue
        data = fn(t)
        out.append({"id": fid, "ext": ext, "dlc": len(data), "data": data.hex(),
                    "count": int(t / period), "age": int((t % period) * 1000)})
    return out


def can_status_payload():
    t = time.time() - START_TIME
    rx = int(sum(t / p for _, _, p, _ in _fake_bus()[:4]))
    return {**state["can"], "node_up": True, "state": 0, "rx": rx, "dropped": 0,
            "tx_err": 0, "rx_err": 0, "bus_err": 0, "busoff": 0}


def status_payload():
    payload = {
        "fw_version": FW_VERSION,
        "board": "MOCK",
        "uptime_s": int(time.time() - START_TIME),
        "reset_reason": 1,  # esp_reset_reason(): 1 - power-on
        "ap_clients": 1,
        "heap": {"int_free": 118000, "int_min": 92000, "int_block": 61000, "psram_free": 2400000, "psram_min": 2300000},
    }
    if state["www_update"]:
        payload["www_update"] = state["www_update"]
    return payload


def default_ap_ssid():
    return "nDash-MOCK"


def check_auth(handler):
    """As in the firmware: reading is open; modifying requests need the settings
    password (Basic Auth, the login is ignored) if one is set."""
    if not state["auth_password"]:
        return True
    header = handler.headers.get("Authorization", "")
    if header.startswith("Basic "):
        try:
            _, _, password = base64.b64decode(header[6:]).decode("utf-8").partition(":")
            if password == state["auth_password"]:
                return True
        except Exception:
            pass
    handler.send_response(401)
    handler.send_header("WWW-Authenticate", 'Basic realm="nDash"')
    handler.send_header("Content-Length", "0")
    handler.end_headers()
    return False


# ---------------------------------------------------------------------------
# HTTP
# ---------------------------------------------------------------------------
class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=WWW_DIR, **kwargs)

    def log_message(self, fmt, *args):
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def end_headers(self):
        # No cache: after editing JS/CSS the browser must not keep the old version.
        self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
        super().end_headers()

    def send_json(self, obj, status=200):
        body = (obj if isinstance(obj, str) else json.dumps(obj)).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_text(self, status, text):
        body = text.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def read_body(self, limit):
        length = int(self.headers.get("Content-Length", 0))
        if length <= 0 or length > limit:
            self.send_text(400, "Bad body size")
            return None
        return self.rfile.read(length)

    def read_json(self, limit):
        raw = self.read_body(limit)
        if raw is None:
            return None
        try:
            return json.loads(raw)
        except ValueError:
            self.send_text(400, "Invalid JSON")
            return None

    def log_post(self, path):
        if path == "/api/log/time":
            data = self.read_json(64)
            if data is None:
                return
            ok = isinstance(data.get("unix"), (int, float)) and 1700000000 < data["unix"] < 4100000000
            return self.send_json({"status": "success"}) if ok else self.send_text(400, "Bad time")
        if path == "/api/log/rescan":
            logs["card"] = True
            return self.send_json({"status": "success"})
        if not check_auth(self):
            return
        if path == "/api/log/start":
            logs["card"] = True
            if not logs["rec"]:
                name = "log_%04d.mlg" % logs["next"]
                logs["next"] += 1
                logs["rec"] = {"name": name, "t0": time.time()}
                logs["files"].append({"name": name, "size": 22, "time": int(time.time())})
            return self.send_json({"status": "success"})
        if path == "/api/log/stop":
            rec, logs["rec"] = logs["rec"], None
            if rec:
                f = next(x for x in logs["files"] if x["name"] == rec["name"])
                f["size"] = 22 + int(time.time() - rec["t0"]) * 900
            return self.send_json({"status": "success"})
        if path == "/api/log/delete":
            q = urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
            name = (q.get("name") or [""])[0]
            if not LOG_NAME_RE.match(name):
                return self.send_text(400, "Bad file name")
            if logs["rec"] and logs["rec"]["name"] == name:
                return self.send_text(409, "File is being recorded")
            before = len(logs["files"])
            logs["files"] = [f for f in logs["files"] if f["name"] != name]
            return self.send_json({"status": "success"}) if len(logs["files"]) < before else self.send_text(404, "File not found")
        if path == "/api/log":
            data = self.read_json(256)
            if data is None:
                return
            cfg = dict(logs["cfg"])
            if data.get("mode") in ("manual", "boot", "condition"):
                cfg["mode"] = data["mode"]
            elif "mode" in data:
                return self.send_text(400, "Mode must be manual, boot or condition")
            if isinstance(data.get("signal"), str):
                if len(data["signal"]) > 15:
                    return self.send_text(400, "Signal name too long")
                cfg["signal"] = data["signal"]
            if data.get("op") in ("<", ">"):
                cfg["op"] = data["op"]
            if isinstance(data.get("value"), (int, float)):
                cfg["value"] = float(data["value"])
            if isinstance(data.get("stop_delay"), (int, float)):
                if not 0 <= data["stop_delay"] <= 3600:
                    return self.send_text(400, "stop_delay must be 0..3600")
                cfg["stop_delay"] = int(data["stop_delay"])
            if cfg["mode"] == "condition" and cfg["signal"] not in [x["name"] for x in bench_signals()]:
                return self.send_text(400, "Condition signal is not in the layout")
            logs["cfg"] = cfg
            return self.send_json({"status": "success"})
        self.send_error(404)

    def bench_post(self, path):
        if not check_auth(self):
            return
        if path == "/api/bench/cmds/reset":
            bench["cmds"] = None
            return self.send_json({"status": "success"})
        if path == "/api/bench/cmds":
            raw = self.read_body(BENCH_CMDS_JSON_MAX)
            if raw is None:
                return
            cleaned = bench_cmds_valid(raw.decode("utf-8", "replace"))
            if cleaned is None:
                return self.send_text(400, "Invalid command table")
            bench["cmds"] = cleaned
            return self.send_json({"status": "success"})
        if path == "/api/bench/send":
            data = self.read_json(96)
            if data is None:
                return
            cmds = bench["cmds"] if bench["cmds"] is not None else bench_preset()
            cmd = next((x for x in cmds if x["name"] == data.get("name")), None)
            if cmd is None:
                return self.send_text(404, "Unknown command")
            rpm = next((x["value"] for x in bench_payload()["signals"] if x["name"] == "rpm"), None)
            if cmd["stopped_only"] and rpm is not None and rpm > BENCH_RUNNING_RPM:
                return self.send_text(409, "The engine is running")
            return self.send_json({"sent": state["can"]["mode"] == "normal"})
        data = self.read_json(256)
        if data is None:
            return
        if path == "/api/bench/demo":
            if not isinstance(data.get("on"), bool):
                return self.send_text(400, '"on" must be true or false')
            bench["demo"] = data["on"]
            return self.send_json({"status": "success"})
        if path == "/api/bench/hold":
            names = [x["name"] for x in bench_signals()]
            name = data.get("name")
            if name not in names or not (isinstance(data.get("value"), (int, float)) or data.get("release") is True):
                return self.send_text(400, "Unknown signal or no value")
            if data.get("release") is True:
                bench["held"].pop(name, None)
            else:
                bench["held"][name] = (float(data["value"]), time.time() + BENCH_HOLD_MS / 1000.0)
            return self.send_json({"status": "success"})
        ext = data.get("ext") is True
        hexs = data.get("data")
        cid = data.get("id")
        if (not isinstance(cid, int) or cid < 0 or cid > (0x1FFFFFFF if ext else 0x7FF) or not isinstance(hexs, str)
                or len(hexs) % 2 or len(hexs) > 16 or not re.fullmatch(r"[0-9a-fA-F]*", hexs)):
            return self.send_text(400, "id (11 or 29 bit) and data (up to 16 hex digits) needed")
        return self.send_json({"sent": state["can"]["mode"] == "normal"} if "can" in state else {"sent": True})

    # ---- media ------------------------------------------------------------
    def media_name(self):
        q = urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
        name = (q.get("name") or [""])[0]
        if not MEDIA_NAME_RE.match(name):
            self.send_text(400, "Bad file name")
            return None
        return name

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/api/bench":
            return self.send_json(bench_payload())
        if path == "/api/log":
            return self.send_json(log_payload())
        if path == "/api/log/files":
            if not logs["card"]:
                return self.send_text(409, "No SD card")
            return self.send_json({"files": sorted(logs["files"], key=lambda f: f["name"], reverse=True)})
        if path.startswith("/log/"):
            name = urllib.parse.unquote(path[len("/log/"):])
            f = next((x for x in logs["files"] if x["name"] == name), None) if LOG_NAME_RE.match(name) else None
            if not logs["card"] or f is None:
                return self.send_error(404)
            body = b"MLVLG\x00" + bytes(f["size"] % 4096)
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Disposition", 'attachment; filename="%s"' % name)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if path == "/api/bench/cmds":
            return self.send_json({"version": 1, "cmds": bench["cmds"] if bench["cmds"] is not None else bench_preset()})
        if path == "/api/media":
            with lock:
                used = sum(len(v) for v in media.values())
                files = [{"name": k, "size": len(v)} for k, v in sorted(media.items())]
            return self.send_json({"total": MEDIA_TOTAL, "used": used, "max_file": MEDIA_MAX_FILE, "files": files})
        if path.startswith("/media/"):
            name = urllib.parse.unquote(path[len("/media/"):])
            data = media.get(name) if MEDIA_NAME_RE.match(name) else None
            if data is None:
                return self.send_error(404)
            q = urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
            scale = int((q.get("s") or ["1"])[0]) if (q.get("s") or ["1"])[0].isdigit() else 1
            head_n = int((q.get("n") or ["0"])[0]) if (q.get("n") or ["0"])[0].isdigit() else 0
            if 0 < head_n <= 256:
                data = data[:head_n]
            if scale in (2, 4, 8) and len(data) > 12 and data[0] == 0x19 and data[1] in (0x10, 0x11, 0x12):
                bpp = 2 if data[1] == 0x12 else 4
                w, h, stride = struct.unpack("<HHH", data[4:10])
                ow, oh = w // scale, h // scale
                rows = [b"".join(data[12 + y * scale * stride + x * scale * bpp:12 + y * scale * stride + x * scale * bpp + bpp]
                                 for x in range(ow)) for y in range(oh)]
                data = data[:4] + struct.pack("<HHH", ow, oh, ow * bpp) + data[10:12] + b"".join(rows)
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if path == "/ota":
            return self.serve_ota_page()
        if path == "/.build_id" and state["build_id_override"]:
            body = state["build_id_override"].encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if path.startswith("/api/"):
            return self.api_get(path)
        return super().do_GET()

    def do_POST(self):
        path = self.path.split("?")[0]
        if path in ("/api/www/update", "/api/ota/update"):
            return self.api_fake_update()
        if path in ("/api/bench/hold", "/api/bench/demo", "/api/bench/can", "/api/bench/cmds", "/api/bench/cmds/reset",
                    "/api/bench/send"):
            return self.bench_post(path)
        if path.startswith("/api/log"):
            return self.log_post(path)
        if path == "/api/media":
            if not check_auth(self):
                return
            name = self.media_name()
            if name is None:
                return
            body = self.read_body(MEDIA_MAX_FILE)
            if body is None:
                return
            if len(body) < 12:
                return self.send_text(400, "Bad file size")
            with lock:
                old = len(media.get(name, b""))
                if sum(len(v) for v in media.values()) - old + len(body) > MEDIA_TOTAL * 7 // 8:
                    return self.send_text(507, "Not enough space")
                media[name] = body
            return self.send_json({"status": "success"})
        if path == "/api/media/delete":
            if not check_auth(self):
                return
            name = self.media_name()
            if name is None:
                return
            with lock:
                if media.pop(name, None) is None:
                    return self.send_error(404)
            return self.send_json({"status": "success"})
        if path.startswith("/api/"):
            return self.api_post(path)
        self.send_error(404)

    def serve_ota_page(self):
        """The /ota page is not in www on the board but compiled in from ota_page.h -
        taken from the same place so the real code is what gets tested."""
        text = open(OTA_PAGE_H, encoding="utf-8").read()
        start = text.index('R"rawliteral(') + len('R"rawliteral(')
        body = text[start:text.index(')rawliteral"')].encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def api_fake_update(self):
        """The body is read in chunks with a delay so the page shows progress, as with the
        board's slow Wi-Fi. Nothing is written."""
        if not check_auth(self):
            return
        length = int(self.headers.get("Content-Length", 0))
        left = length
        while left > 0:
            chunk = self.rfile.read(min(left, 64 * 1024))
            if not chunk:
                break
            left -= len(chunk)
            time.sleep(0.03)
        m = re.search(r"[?&]id=([^&]+)", self.path)
        upd_id = m.group(1) if m else ""
        failed = left > 0 or "fail=1" in self.path
        if not failed and "same=1" not in self.path:
            state["build_id_override"] = "%064x" % random.getrandbits(256)
        blocks = (length + 65535) // 65536
        written = 0 if "same=1" in self.path else blocks // 3
        if upd_id:
            state["www_update"] = {
                "id": upd_id, "done": True, "ok": not failed,
                **({"error": "Write failed"} if failed else {}),
                "blocks_written": written, "blocks_skipped": blocks - written, "total_ms": 4200,
            }
        if "stall=1" in self.path:
            time.sleep(60)
            self.close_connection = True
            return
        if "hang=1" in self.path:
            self.close_connection = True
            return
        if failed:
            return self.send_text(500, "Write failed")
        return self.send_json({"status": "success", "bytes": length, "blocks_written": written,
                               "blocks_skipped": blocks - written, "flash_ms": 1800, "total_ms": 4200})

    # ---- GET /api/* -------------------------------------------------------
    def api_get(self, path):
        with lock:
            if path == "/api/status":
                return self.send_json(status_payload())
            if path == "/api/auth/status":
                return self.send_json({"enabled": bool(state["auth_password"])})
            if path == "/api/layout":
                return self.send_json(state["layout"] or c_string_define(LAYOUT_DEFAULT_H, "LND_LAYOUT_DEFAULT_JSON"))
            if path == "/api/display":
                return self.send_json({"brightness": state["brightness"], "has_brightness": True, "baro": state["baro"], "boost": state["boost"], "button_pin": state["button_pin"]})
            if path == "/api/wifi":
                w = state["wifi"]
                return self.send_json({"ssid": w["ssid"] or default_ap_ssid(), "default_ssid": default_ap_ssid(),
                                       "custom_ssid": bool(w["ssid"]), "password_default": w["password"] == WIFI_PASS_DEFAULT})
            if path == "/api/can":
                return self.send_json(can_status_payload())
            if path == "/api/can/map":
                return self.send_json(state["can_map"] or c_string_define(CAN_MAP_DEFAULT_H, "LND_CAN_MAP_DEFAULT_JSON"))
            if path == "/api/can/frames":
                return self.send_json(fake_frames_payload())
        self.send_error(404)

    # ---- POST /api/* ------------------------------------------------------
    def api_post(self, path):
        with lock:
            if path == "/api/auth/verify":
                if not check_auth(self):
                    return
                return self.send_json({"status": "ok"})
            if path == "/api/auth/password":
                if not check_auth(self):
                    return
                data = self.read_json(256)
                if data is None:
                    return
                pw = data.get("password") if isinstance(data, dict) else None
                if not isinstance(pw, str):
                    return self.send_text(400, "Invalid body")
                if pw and not (PASSWORD_MIN <= len(pw) <= PASSWORD_MAX):
                    return self.send_text(400, "Password must be %d..%d characters" % (PASSWORD_MIN, PASSWORD_MAX))
                state["auth_password"] = pw
                return self.send_json({"status": "success", "enabled": bool(pw)})
            if path == "/api/layout":
                if not check_auth(self):
                    return
                raw = self.read_body(LAYOUT_JSON_MAX)
                if raw is None:
                    return
                text = raw.decode("utf-8", "replace")
                if not layout_is_valid(text):
                    return self.send_text(400, "Invalid layout")
                problem = layout_problem(text)
                if problem:
                    return self.send_text(400, problem)
                state["layout"] = text
                return self.send_json({"status": "success"})
            if path == "/api/layout/reset":
                if not check_auth(self):
                    return
                state["layout"] = None
                return self.send_json({"status": "success"})
            if path == "/api/display":
                if not check_auth(self):
                    return
                data = self.read_json(128)
                if data is None:
                    return
                if not isinstance(data, dict) or not ({"brightness", "baro", "boost", "button_pin"} & set(data)):
                    return self.send_text(400, "Nothing to set")
                if "brightness" in data:
                    v = data["brightness"]
                    if not isinstance(v, (int, float)) or v < BRIGHTNESS_MIN or v > 100:
                        return self.send_text(400, "Brightness must be %d..100" % BRIGHTNESS_MIN)
                if "baro" in data and data["baro"] not in ("kpa", "inhg"):
                    return self.send_text(400, "baro must be kpa or inhg")
                if "boost" in data and data["boost"] not in ("bar", "psi"):
                    return self.send_text(400, "boost must be bar or psi")
                if "button_pin" in data:
                    p = data["button_pin"]
                    if (not isinstance(p, int) or isinstance(p, bool) or p < 0 or p > 48
                            or (p != 0 and (22 <= p <= 37 or p in (19, 20, 4, 6)))):
                        return self.send_text(400, "This GPIO is used by the board or the chip (panel, touch, CAN, SD, flash, USB)")
                    state["button_pin"] = p
                for k in ("baro", "boost"):
                    if k in data:
                        state[k] = data[k]
                if "brightness" in data:
                    state["brightness"] = int(data["brightness"])
                return self.send_json({"status": "success"})
            if path == "/api/wifi":
                if not state["auth_password"]:
                    return self.send_text(403, "Set a settings password first")
                if not check_auth(self):
                    return
                data = self.read_json(512)
                if data is None:
                    return
                ssid, pw = data.get("ssid", ""), data.get("password")
                if not isinstance(pw, str) or not isinstance(ssid, str):
                    return self.send_text(400, "Invalid body")
                if len(ssid.encode()) > WIFI_SSID_MAX or not (WIFI_PASS_MIN <= len(pw) <= WIFI_PASS_MAX):
                    return self.send_text(400, "SSID up to %d bytes, password %d..%d characters" % (WIFI_SSID_MAX, WIFI_PASS_MIN, WIFI_PASS_MAX))
                state["wifi"] = {"ssid": ssid, "password": pw}
                return self.send_json({"status": "success", "restart": True})
            if path == "/api/can":
                if not check_auth(self):
                    return
                data = self.read_json(256)
                if data is None:
                    return
                c = dict(state["can"])
                if "bitrate" in data:
                    if not isinstance(data["bitrate"], (int, float)) or not (10000 <= data["bitrate"] <= 1000000):
                        return self.send_text(400, "Bitrate must be 10000..1000000")
                    c["bitrate"] = int(data["bitrate"])
                if "mode" in data:
                    if data["mode"] not in ("normal", "listen_only"):
                        return self.send_text(400, "Unknown mode")
                    c["mode"] = data["mode"]
                for key in ("heartbeat", "demo"):
                    if isinstance(data.get(key), bool):
                        c[key] = data[key]
                state["can"] = c
                return self.send_json({"status": "success"})
            if path == "/api/can/map":
                if not check_auth(self):
                    return
                raw = self.read_body(CAN_MAP_JSON_MAX)
                if raw is None:
                    return
                text = raw.decode("utf-8", "replace")
                if not can_map_is_valid(text):
                    return self.send_text(400, "Invalid CAN table")
                state["can_map"] = text
                return self.send_json({"status": "success"})
            if path == "/api/can/map/reset":
                if not check_auth(self):
                    return
                state["can_map"] = None
                return self.send_json({"status": "success"})
        self.send_error(404)


Handler.extensions_map.update({
    ".woff2": "font/woff2",
    ".js": "text/javascript",
})


def load_media():
    if os.path.isdir(MEDIA_DIR):
        for n in sorted(os.listdir(MEDIA_DIR)):
            if MEDIA_NAME_RE.match(n):
                media[n] = open(os.path.join(MEDIA_DIR, n), "rb").read()


def main():
    load_media()
    args = sys.argv[1:]
    if "--password" in args:
        i = args.index("--password")
        if i + 1 >= len(args):
            sys.exit("--password needs a value")
        state["auth_password"] = args[i + 1]
        del args[i:i + 2]
    port = int(args[0]) if args else 8088

    if not os.path.isdir(WWW_DIR):
        sys.exit("www not found at: %s" % WWW_DIR)
    with http.server.ThreadingHTTPServer(("0.0.0.0", port), Handler) as httpd:
        print("nDash mock server: http://localhost:%d/" % port)
        print("                   http://localhost:%d/ota" % port)
        print("Serving files from: %s" % WWW_DIR)
        if state["auth_password"]:
            print("Password protection is on")
        print("Ctrl+C to stop")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
