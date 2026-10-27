#!/usr/bin/env python3
"""Generates common/config/layout_default.h (the layout used until a saved one exists) and adds the "boost" entry
to the CAN preset in common/config/can_map_default.h.

The layout is described here, in one place, with the palette and the helpers; the header is generated. Run it after
changing the design:   python tools/mk_default_layout.py

The limits of the firmware are checked here as well (14 live widgets and 2 dials on a screen, 16 signals).
"""
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAYOUT_H = os.path.join(ROOT, "common", "config", "layout_default.h")
CAN_H = os.path.join(ROOT, "common", "config", "can_map_default.h")

# ---- palette ---------------------------------------------------------------------------------------
BG = "#000000"
PANEL = "#15191e"       # the background of a tile
EDGE = "#2b323b"        # its border
TEXT = "#f4f6f8"        # the numbers
DIM = "#8b97a6"         # titles and units
RED = "#ff2a2a"
AMBER = "#ffb000"
# style B
B_PANEL = "#05080c"
B_EDGE = "#00cfe8"
B_TEXT = "#d8faff"
B_DIM = "#4fb3c4"
B_BEZEL = "#0a3a44"
B_TICK = "#aef3ff"
# the accent bar of a tile says what group the signal is from
ACC_NEUTRAL = "#d5dbe3"   # speed, rpm
ACC_TEMP = "#2f9bff"      # temperatures
ACC_PRESS = "#ff9f1a"     # pressures, boost
ACC_ELEC = "#3ddc84"      # battery, fuel level
ACC_MIX = "#b36bff"       # mixture


def zone(a, b, color, fill=True):
    """fill: a tile with "zone_bg" paints its panel with this zone; False - only the number takes the colour"""
    z = {"from": a, "to": b, "color": color}
    if not fill:
        z["fill"] = False
    return z


ATM = 101.325
# kPa signals with a "pressure" kind show in the units of the settings (units.h)
# Warnings (amber) colour the number, limits (red) fill the panel of a tile.
SIGNALS = [
    dict(name="rpm", title="RPM", unit="rpm", min=0, max=8000, decimals=0,
         zones=[zone(6000, 6500, AMBER, False), zone(6500, 8000, RED)]),
    dict(name="speed", title="SPEED", unit="km/h", min=0, max=240, decimals=0),
    dict(name="map", title="MAP", unit="kPa", pressure="abs", min=0, max=250, decimals=0),
    # the same MAP shown against the atmosphere; the range starts at -1 bar so that the dial labels are round numbers
    dict(name="boost", title="BOOST", unit="bar", pressure="boost", min=round(ATM - 100, 3), max=round(ATM + 150, 3),
         decimals=2, zones=[zone(round(ATM + 100, 3), round(ATM + 130, 3), AMBER, False), zone(round(ATM + 130, 3), round(ATM + 150, 3), RED)]),
    dict(name="coolant", title="COOLANT", unit="°C", min=0, max=140, decimals=0,
         zones=[zone(95, 102, AMBER, False), zone(102, 140, RED)]),
    dict(name="oil_press", title="OIL PRESS", unit="kPa", min=0, max=600, decimals=0,
         zones=[zone(0, 100, RED), zone(100, 180, AMBER, False)]),
    dict(name="oil_temp", title="OIL TEMP", unit="°C", min=0, max=160, decimals=0,
         zones=[zone(100, 110, AMBER, False), zone(110, 160, RED)]),
    dict(name="iat", title="INTAKE", unit="°C", min=-40, max=120, decimals=0,
         zones=[zone(45, 60, AMBER, False), zone(60, 120, RED)]),
    dict(name="fuel_press", title="FUEL PRESS", unit="kPa", min=0, max=600, decimals=0,
         zones=[zone(0, 200, RED), zone(200, 250, AMBER, False)]),
    dict(name="afr", title="AFR", unit="", min=10, max=20, decimals=1,
         zones=[zone(10, 11, AMBER, False), zone(15.5, 20, RED)]),
    dict(name="vbat", title="BATTERY", unit="V", min=8, max=16, decimals=1,
         zones=[zone(8, 11.5, RED), zone(11.5, 12.4, AMBER, False), zone(15, 16, RED)]),
    dict(name="fuel_level", title="FUEL", unit="%", min=0, max=100, decimals=0,
         zones=[zone(0, 10, RED), zone(10, 20, AMBER, False)]),
]

BAR_UNIT = dict(unit="bar", div=100, decimals=1)   # kPa pressures of the sensors in bar


# Style A (graphite, Orbitron): the panel is filled by a red zone, the amber one colours the number;
# the accent bar says what group the signal is from, on the outer edge of its column.
def tile_a(x, y, w, h, signal, accent, side, **extra):
    t = dict(type="tile", x=x, y=y, w=w, h=h, signal=signal, font="orbitron_36", color=TEXT, bg_color=PANEL,
             border_w=1, border_color=EDGE, radius=10, accent=side, accent_w=5, accent_color=accent,
             title_color=DIM, unit_color=DIM, zone_color=True, zone_bg=True)
    t.update(extra)
    return t


# Style B (neon HUD, Michroma and Jura): a cyan outline, the zone colours the number and the border.
def tile_b(x, y, signal, w=190, h=93, **extra):
    t = dict(type="tile", x=x, y=y, w=w, h=h, signal=signal, font="michroma_36", color=B_TEXT, bg_color=B_PANEL,
             border_w=1, border_color=B_EDGE, radius=4, accent="none", title_font="jura_24", unit_font="jura_24",
             title_color=B_DIM, unit_color=B_DIM, zone_color=True, zone_border=True)
    t.update(extra)
    return t


def dial_b(x, y, signal, **extra):
    d = dict(type="dial", x=x, y=y, w=380, signal=signal, bg_color="#03080c", bezel_color=B_BEZEL, tick_color=B_TICK,
             minor_color="#4a6f78", label_color=B_TICK, label_font="jura_24",
             needle_image="neon", needle_scale=256, show_hub=False, bezel_w=10, zone_w=14, needle_margin=60, center_value=True,
             cv_font="michroma_36", cv_color=B_TEXT, ct_font="jura_24", ct_color=B_DIM)
    d.update(extra)
    return d


def screens():
    s = []

    # 1. Main (style A): a tachometer with a picture needle in the middle, a shift light on top, five tiles on each side.
    # Left: what to watch while driving, right: the engine. 12 live widgets.
    left = [("speed", ACC_NEUTRAL, {}), ("coolant", ACC_TEMP, {}), ("oil_temp", ACC_TEMP, {}),
            ("oil_press", ACC_PRESS, BAR_UNIT), ("fuel_level", ACC_ELEC, {})]
    right = [("boost", ACC_PRESS, {}), ("afr", ACC_MIX, {}), ("iat", ACC_TEMP, {}),
             ("fuel_press", ACC_PRESS, BAR_UNIT), ("vbat", ACC_ELEC, {})]
    w = [dict(type="segments", x=0, y=4, w=800, h=22, signal="rpm", count=14, gap=4, start=3000, end=8000, zone_color=True),
         dict(type="dial", x=180, y=32, w=440, signal="rpm", label_div=1000, bg_color="#101214", bezel_color=EDGE,
              tick_color=TEXT, minor_color="#808a96", label_color=TEXT, ct_color=DIM, bg_image="bg_carbon.bin",
              needle_image="classic", needle_scale=290, show_hub=False, zone_w=20, center_text="RPM", ct_dy=60)]
    for i, (sig, acc, ex) in enumerate(left):
        w.append(tile_a(4, 34 + 88 * i, 168, 82, sig, acc, "left", **ex))
    for i, (sig, acc, ex) in enumerate(right):
        w.append(tile_a(628, 34 + 88 * i, 168, 82, sig, acc, "right", **ex))
    s.append(dict(bg_color=BG, widgets=w))

    # 2. Tuning (style B): boost and mixture gauges with picture needles on a plain dark disc (no background picture: it was 14 fps with two), four tiles.
    # The boost scale is -1.0 .. 1.5 bar from the bottom clockwise (the number in the free bottom right quarter); the
    # mixture scale 10 .. 20 is turned so that 15 stands straight up (the number at the bottom centre).
    w = [dial_b(10, 8, "boost", ticks=26, major_every=5, center_text="BOOST", cv_dx=70, cv_dy=75, ct_dx=70, ct_dy=118),
         dial_b(410, 8, "afr", rotation=135, ticks=21, major_every=2, center_text="AFR", cv_dy=95, ct_dy=138)]
    # the rpm tile shows hundreds: the last digits of a fast signal would only flicker
    for i, (sig, ex) in enumerate([("rpm", dict(round=100)), ("map", {}), ("fuel_press", BAR_UNIT), ("coolant", {})]):
        w.append(tile_b(10 + 197 * i, 396, sig, h=80, title_font="14", unit_font="14", **ex))   # small captions: the number is the point
    s.append(dict(bg_color=BG, widgets=w))

    # 3. Drive (style B): the rpm bar on top, the speed in big digits in the centre of the screen, five identical tiles
    # pressed to the bottom right corner: three in the right column, two in the bottom row. 7 live widgets.
    # The speed block (the digits 98 px, a gap, "km/h" 37 px) is centred on the screen, 800 x 480: x 400, y 240. The
    # digits are a value widget 400 px wide centred on x 400; "km/h" is a text and cannot be centred by itself, so its
    # x is set by hand from the width of the label (about 84 px in jura_36).
    gap = 14
    y0 = 240 - (98 + gap + 37) // 2
    w = [dict(type="bar", x=10, y=8, w=780, h=72, signal="rpm", color="#35d07f", bg_color="#0a1118", border_w=2,
              border_color=B_EDGE, radius=6, bg_opa=255, zone_color=True, mark_step=1000, mark_div=1000,
              mark_font="jura_24", mark_color=B_DIM, mark_gap=8),
         dict(type="value", x=200, y=y0, w=400, signal="speed", font="seg7_120", color=B_TEXT),
         dict(type="text", x=358, y=y0 + 98 + gap, text="km/h", font="jura_36", color=B_DIM)]
    for i, sig in enumerate(["coolant", "fuel_level", "vbat"]):
        w.append(tile_b(600, 173 + 103 * i, sig))
    for i, (sig, ex) in enumerate([("oil_press", BAR_UNIT), ("oil_temp", {})]):
        w.append(tile_b(200 + 200 * i, 379, sig, **ex))
    s.append(dict(bg_color=BG, widgets=w))
    return s


def check(doc):
    assert len(doc["signals"]) <= 16
    names = {g["name"] for g in doc["signals"]}
    for n, sc in enumerate(doc["screens"], 1):
        live = [x for x in sc["widgets"] if x["type"] not in ("text",)]
        dials = [x for x in sc["widgets"] if x["type"] == "dial"]
        assert len(live) <= 14, (n, len(live))
        assert len(dials) <= 2, n
        for x in sc["widgets"]:
            if x.get("signal"):
                assert x["signal"] in names, (n, x["signal"])
            h = x.get("w", 0) if x["type"] == "dial" else x.get("h", 0)
            if x["type"] != "text":
                assert x.get("x", 0) + x.get("w", 0) <= 800 and x.get("y", 0) + h <= 480, (n, x)
        print("screen %d: %d live widgets, %d dials" % (n, len(live), len(dials)))


def c_define(name, text, comment):
    raw = json.dumps(text, ensure_ascii=False, separators=(",", ":"))
    chunks = [raw[i:i + 100] for i in range(0, len(raw), 100)]
    esc = [c.replace("\\", "\\\\").replace('"', '\\"') for c in chunks]
    return comment + "#define %s \\\n" % name + " \\\n".join('    "%s"' % c for c in esc) + "\n"


def main():
    doc = {"version": 1, "signals": SIGNALS, "screens": screens()}
    check(doc)
    comment = """#pragma once

// The layout used until a saved one exists (and after "reset to built-in"). Generated by tools/mk_default_layout.py:
// change the design there, not here.
//   1. Main   (style A) a tachometer with a picture needle and a shift light, five tiles on each side
//   2. Tuning (style B) two analog gauges (boost, mixture) with neon needles and four tiles
//   3. Drive  (style B) the rpm bar, the speed in big digits, five identical tiles in the bottom right corner
// Colour zones are examples: set the limits of the engine on the "Signals" tab of the editor. A red zone fills the
// panel of a tile, an amber one ("fill": false) colours the number.
// Pressures: MAP and boost follow the pressure units of the settings; the sensors are shown in bar (value / 100).
"""
    text = c_define("LND_LAYOUT_DEFAULT_JSON", doc, comment)
    assert len(json.dumps(doc, ensure_ascii=False, separators=(",", ":"))) < 16384
    open(LAYOUT_H, "w", encoding="utf-8", newline="\n").write(text)
    print("layout_default.h: %d bytes of JSON" % len(json.dumps(doc, ensure_ascii=False, separators=(",", ":"))))

    # CAN preset: boost is the MAP frame field again (the same signal under another name)
    s = open(CAN_H, encoding="utf-8").read()
    head, body = s.split("#define LND_CAN_MAP_DEFAULT_JSON", 1)
    lits = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
    can = json.loads("".join(lits).replace('\\"', '"'))
    ents = can["entries"]
    if not any(e["signal"] == "boost" for e in ents):
        m = next(e for e in ents if e["signal"] == "map")
        ents.insert(ents.index(m) + 1, {**m, "signal": "boost"})
    assert len(json.dumps(can, separators=(",", ":"))) < 4096
    head = head.replace("Pressures are kPa", "\"boost\" is the field of \"map\" again (the display subtracts the atmosphere).\n// Pressures are kPa") \
        if "\"boost\"" not in head else head
    open(CAN_H, "w", encoding="utf-8", newline="\n").write(
        head + c_define("LND_CAN_MAP_DEFAULT_JSON", can, "").replace("", "", 0))
    print("can_map_default.h: %d entries" % len(ents))


if __name__ == "__main__":
    main()
