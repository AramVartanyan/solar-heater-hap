#!/usr/bin/env python3
"""Stand-in for the firmware, to try the web page on a computer (no ESP32 needed):

    python3 project/solar-heater-hap/test/web_stub.py [port]      # default 8080
    open http://localhost:8080/

Serves main/web/index.html and the requests of main/sh_web_data.h with sample values in the same
JSON shape as sh_web_state_json(): settings and installer data with the ranges and checks of the
firmware, the computed limits (same formulas as sh_installer.c), the pump test, the access code
810841 (network "SOLAR-810841"). The temperatures drift a little on every request. Query
parameters of the page URL choose a scenario: ?alarms=E1,Hot,Fro  ?fault=col  ?fault=tank.
"""

import json
import math
import random
import sys
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

PAGE = Path(__file__).resolve().parent.parent / "main" / "web" / "index.html"
CODE = "810841"
FEATURES = ["alarms", "temps", "pump", "test", "settings", "frost", "installer", "calc", "network",
            "firmware", "reset_wifi", "reset_settings", "reset_factory"]
SET_RANGES = {"tbo": (30, 85, 1), "c_lo": (10, 80, 1), "d_on": (3, 20, 1), "d_of": (1, 15, 1),
              "t_st": (60, 900, 30)}
INST_RANGES = {"coolant": (0, 1, 1), "glycol": (0, 60, 1), "p_min": (5, 60, 1),
               "col_max": (100, 250, 1)}
SET_DEFAULTS = {"tbo": 70, "c_lo": 25, "d_on": 6, "d_of": 4, "t_st": 300, "frost": 1}
INST_DEFAULTS = {"coolant": 0, "glycol": 0, "p_min": 15, "col_max": 120}
COL_PROTECT = 1100

state = {
    "set": dict(SET_DEFAULTS), "inst": dict(INST_DEFAULTS), "tc": 523, "tt": 412,
    "test_end": None, "scenario": {},
}


def interp(points, pct):
    for (a, x), (b, y) in zip(points, points[1:]):
        if pct <= b:
            return x + (y - x) * (pct - a) / (b - a)
    return points[-1][1]


def limits(inst):
    p_abs = inst["p_min"] / 10 + 1.013
    water = 1810.94 / (8.14019 - math.log10(p_abs * 750.062)) - 244.485
    glycol = inst["coolant"] == 1
    add = interp([(0, 0), (20, 1), (30, 2), (40, 3), (50, 4), (60, 6)], inst["glycol"]) if glycol else 0
    boil = math.floor((water + add) * 10)
    cap = min(inst["col_max"] * 10 - 50, boil - 100)
    if glycol:
        fp = interp([(0, 0), (10, -30), (20, -80), (30, -140), (40, -220), (50, -330), (60, -480)],
                    inst["glycol"])
        frost = max(-400, min(100, math.ceil(fp) + 40))
    else:
        frost = 40
    return {"boil": boil, "cap": cap, "prot": COL_PROTECT, "used": min(COL_PROTECT, cap),
            "ban": boil, "frost": frost}


def ranges():
    s = state["set"]
    r = {k: list(v) for k, v in SET_RANGES.items()}
    r["d_on"][0] = max(r["d_on"][0], s["d_of"] + 1)
    r["d_of"][1] = min(r["d_of"][1], s["d_on"] - 1)
    r.update({k: list(v) for k, v in INST_RANGES.items()})
    return r


def snapshot():
    sc = state["scenario"]
    state["tc"] += random.randint(-3, 3)
    state["tt"] += random.randint(-1, 1)
    test_left = None
    if state["test_end"] is not None:
        test_left = max(0, int(state["test_end"] - time.time()))
        if test_left == 0:
            state["test_end"] = None
            test_left = None
    cf = sc.get("fault") == "col"
    tf = sc.get("fault") == "tank"
    alarms = [a for a in sc.get("alarms", "").split(",") if a]
    if cf:
        alarms.insert(0, "E1")
    if tf:
        alarms.insert(0, "E2")
    why = "test" if test_left is not None else ("frost" if "Fro" in alarms else
                                                 "col_protect" if "Hot" in alarms else "diff")
    return {
        "f": FEATURES, "ver": "0.1.0", "host": "solar-heater", "al": alarms,
        "tc": None if cf else state["tc"], "tt": None if tf else state["tt"],
        "cf": int(cf), "tf": int(tf), "pump": 1, "why": why, "test": test_left,
        "set": state["set"], "rng": ranges(), "inst": state["inst"], "calc": limits(state["inst"]),
        "net": {"ssid": "A16blue", "ip": "192.168.1.20", "rssi": -41},
    }


class Handler(BaseHTTPRequestHandler):
    def reply(self, status, body, ctype="application/json"):
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def code_ok(self):
        cookies = dict(c.strip().split("=", 1) for c in self.headers.get("Cookie", "").split(";") if "=" in c)
        return cookies.get("code", "").upper() == CODE

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        if url.path == "/":
            q = dict(urllib.parse.parse_qsl(url.query))
            if q:
                state["scenario"] = q
            self.reply(200, PAGE.read_bytes(), "text/html; charset=utf-8")
        elif url.path == "/api/state":
            self.reply(200, snapshot())
        else:
            self.reply(404, {"ok": False})

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        form = dict(urllib.parse.parse_qsl(self.rfile.read(n).decode()))
        path = urllib.parse.urlparse(self.path).path
        if path == "/api/code":
            ok = form.get("code", "").upper() == CODE
            return self.reply(200 if ok else 403, {"ok": ok} if ok else {"ok": False, "err": "code"})
        if path == "/api/set":
            k, v = form.get("k"), form.get("v", "")
            try:
                v = int(v)
            except ValueError:
                return self.reply(400, {"ok": False, "err": "request"})
            if k in SET_RANGES or k == "frost":
                lo, hi, st = ranges()[k] if k != "frost" else (0, 1, 1)
                if not (lo <= v <= hi and (v - SET_RANGES.get(k, (0,))[0]) % st == 0):
                    return self.reply(400, {"ok": False, "err": "value"})
                state["set"][k] = v
            elif k in INST_RANGES:
                if not self.code_ok():
                    return self.reply(403, {"ok": False, "err": "code"})
                lo, hi, _ = INST_RANGES[k]
                if not lo <= v <= hi or (k == "glycol" and v and state["inst"]["coolant"] == 0):
                    return self.reply(400, {"ok": False, "err": "value"})
                state["inst"][k] = v
                if k == "coolant" and v == 0:
                    state["inst"]["glycol"] = 0
            else:
                return self.reply(400, {"ok": False, "err": "request"})
            return self.reply(200, {"ok": True})
        if path == "/api/cmd":
            c = form.get("c")
            now = time.time()
            if c == "test_plus":
                if state["test_end"] is None:
                    state["test_end"] = now + state["set"]["t_st"]
                else:
                    state["test_end"] = now + min(900, state["test_end"] - now + 60)
            elif c == "test_minus":
                if state["test_end"] is not None and state["test_end"] - now > 60:
                    state["test_end"] -= 60
            elif c == "test_stop":
                state["test_end"] = None
            elif c in ("reset_wifi", "reset_settings", "reset_factory"):
                if not self.code_ok():
                    return self.reply(403, {"ok": False, "err": "code"})
                if c == "reset_settings":
                    state["set"], state["inst"] = dict(SET_DEFAULTS), dict(INST_DEFAULTS)
            else:
                return self.reply(400, {"ok": False, "err": "request"})
            return self.reply(200, {"ok": True})
        self.reply(404, {"ok": False})

    def log_message(self, fmt, *args):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    print(f"http://localhost:{port}/  (code {CODE})")
    ThreadingHTTPServer(("", port), Handler).serve_forever()
