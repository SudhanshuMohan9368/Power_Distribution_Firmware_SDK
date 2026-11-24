#!/usr/bin/env python3
"""
Power Distribution Board - Test GUI  (USB serial + optional CAN)
================================================================
Matches the CURRENT firmware protocol in PROTOCOL.md:
  * Serial : USB CDC, 115200, ASCII commands (M1/M0, UV1, F1, FAN n, L1,
             H1, NEO ..., STATUS ...) -> board answers with a JSON line.
  * CAN    : 500 kbps, cmd ID 0x6A5, reply ID 0x6B5 (optional, python-can).

Install:   pip install pyserial            (required)
           pip install python-can          (only if you want the CAN tab)
Run:       python power_distribution_gui.py

Every button sends to whichever links are connected (serial and/or CAN).
"""

import json
import queue
import re
import sys
import threading
import time
import tkinter as tk
from datetime import datetime
from tkinter import messagebox, ttk
from tkinter.scrolledtext import ScrolledText

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("pyserial missing -> run:  pip install pyserial")
    sys.exit(1)

try:
    import can  # type: ignore
except ImportError:
    can = None

# ---- protocol constants (see PROTOCOL.md) ---------------------------------
CAN_ID_CMD, CAN_ID_RESP = 0x6A5, 0x6B5

FN_SMPS, FN_POLL, FN_HEATER, FN_LIGHT, FN_UV, FN_FAN = 1, 2, 3, 4, 5, 6
FN_NEO_STATE, FN_NEO_COLOR, FN_STATUS, FN_NEO_FILL, FN_FAN_PWM = 7, 8, 9, 0x0A, 0x0B

NEO_SUBCODE = {"INIT": 0x50, "PROGRESS": 0x51, "ERROR": 0x52, "COMPLETE": 0x53,
               "IDLE": 0x54, "OFF": 0x55, "RUNNING": 0x56}
NEO_ENUM = {0: "IDLE", 1: "INIT", 2: "PROGRESS", 3: "RUNNING", 4: "COMPLETE",
            5: "ERROR", 6: "MANUAL", 7: "RAINBOW", 8: "OFF", 9: "PIXEL_FILL"}

BIT_SMPS, BIT_UV, BIT_FAN, BIT_LIGHT, BIT_HEATER = 0x01, 0x02, 0x04, 0x08, 0x10
SNS_LIMIT, SNS_HALL, SNS_EMG = 0x01, 0x02, 0x04

# ---- colours ---------------------------------------------------------------
BG, PANEL, FG, DIM = "#1e2227", "#272c33", "#e6e9ee", "#8b95a3"
ACCENT, GOOD, WARN, BAD, BLUE = "#3d8bfd", "#2ecc71", "#f5a623", "#ff5c5c", "#4da3ff"
LED_OFF = "#3a4048"


class LED(tk.Canvas):
    def __init__(self, parent, size=16):
        super().__init__(parent, width=size, height=size, bg=PANEL,
                         highlightthickness=0)
        self._o = self.create_oval(2, 2, size - 2, size - 2, fill=LED_OFF,
                                   outline="#555c66")

    def set(self, on, color=GOOD):
        self.itemconfig(self._o, fill=color if on else LED_OFF)


class App:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("Power Distribution Board - Test GUI")
        self.root.configure(bg=BG)
        self.root.minsize(1000, 700)
        self._style()

        self.ser = None
        self.ser_run = False
        self.bus = None
        self.can_run = False
        self.q = queue.Queue()
        self.last_rx = 0.0

        self._build()
        self._pump()
        self._autopoll()
        self._refresh_ports()
        self.root.protocol("WM_DELETE_WINDOW", self._close)

    # ------------------------------------------------------------- styling
    def _style(self):
        s = ttk.Style()
        s.theme_use("clam")
        s.configure(".", background=BG, foreground=FG, fieldbackground="#323842")
        s.configure("TFrame", background=BG)
        s.configure("TLabel", background=BG, foreground=FG)
        s.configure("Panel.TLabel", background=PANEL, foreground=FG)
        s.configure("Dim.TLabel", background=PANEL, foreground=DIM)
        s.configure("TLabelframe", background=PANEL, foreground=FG, borderwidth=1)
        s.configure("TLabelframe.Label", background=PANEL, foreground=ACCENT,
                    font=("Segoe UI", 9, "bold"))
        s.configure("TButton", background="#3a414b", foreground=FG, padding=5,
                    borderwidth=0)
        s.map("TButton", background=[("active", "#4a525e")])
        s.configure("On.TButton", background="#1f7a4a")
        s.map("On.TButton", background=[("active", "#27935a")])
        s.configure("Off.TButton", background="#8a3030")
        s.map("Off.TButton", background=[("active", "#a53a3a")])
        s.configure("Accent.TButton", background=ACCENT)
        s.map("Accent.TButton", background=[("active", "#5a9dff")])
        s.configure("TCheckbutton", background=BG, foreground=FG)
        s.configure("TCombobox", arrowcolor=FG)
        s.configure("Horizontal.TScale", background=PANEL)

    # ----------------------------------------------------------------- UI
    def _panel(self, parent, title):
        f = ttk.LabelFrame(parent, text=f" {title} ", padding=8)
        f.pack(fill="x", pady=4)
        return f

    def _btn(self, parent, text, cmd, style="TButton", w=9):
        b = ttk.Button(parent, text=text, command=cmd, style=style, width=w)
        b.pack(side="left", padx=2)
        return b

    def _build(self):
        # ---- connection bar
        top = ttk.Frame(self.root)
        top.pack(fill="x", padx=10, pady=(10, 4))

        ttk.Label(top, text="USB Serial:").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_cb = ttk.Combobox(top, textvariable=self.port_var, width=34)
        self.port_cb.pack(side="left", padx=4)
        ttk.Button(top, text="Refresh", width=8,
                   command=self._refresh_ports).pack(side="left")
        self.ser_btn = ttk.Button(top, text="Connect", width=11,
                                  style="Accent.TButton", command=self._toggle_ser)
        self.ser_btn.pack(side="left", padx=6)
        self.ser_lbl = ttk.Label(top, text="Disconnected", foreground=DIM)
        self.ser_lbl.pack(side="left", padx=4)

        # CAN (optional) on the right
        self.can_btn = ttk.Button(top, text="CAN Connect", width=12,
                                  command=self._toggle_can)
        self.can_btn.pack(side="right")
        self.can_ch = tk.StringVar(value="COM3")
        ttk.Entry(top, textvariable=self.can_ch, width=10).pack(side="right", padx=4)
        self.can_if = tk.StringVar(value="slcan")
        ttk.Combobox(top, textvariable=self.can_if, width=9, state="readonly",
                     values=["slcan", "pcan", "socketcan", "kvaser", "virtual"]
                     ).pack(side="right")
        ttk.Label(top, text="CAN:").pack(side="right", padx=(10, 4))
        if can is None:
            self.can_btn.state(["disabled"])

        opts = ttk.Frame(self.root)
        opts.pack(fill="x", padx=10)
        self.poll_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(opts, text="Auto-refresh status (1 s)",
                        variable=self.poll_var).pack(side="left")
        self.hide_dbg = tk.BooleanVar(value=True)
        ttk.Checkbutton(opts, text="Hide CANDBG lines in log",
                        variable=self.hide_dbg).pack(side="left", padx=12)
        self.banner = tk.Label(opts, text="", bg=BG, fg=BAD,
                               font=("Segoe UI", 11, "bold"))
        self.banner.pack(side="right")

        # ---- body
        body = ttk.Frame(self.root)
        body.pack(fill="both", expand=True, padx=10, pady=4)
        left = ttk.Frame(body)
        left.pack(side="left", fill="y", padx=(0, 8))
        right = ttk.Frame(body)
        right.pack(side="left", fill="both", expand=True)

        self._build_outputs(left)
        self._build_fan(left)
        self._build_neo(left)
        self._build_status(right)
        self._build_console(right)

        # ---- log
        lf = ttk.LabelFrame(self.root, text=" Log ", padding=4)
        lf.pack(fill="both", expand=True, padx=10, pady=(0, 10))
        bar = ttk.Frame(lf)
        bar.pack(fill="x")
        ttk.Button(bar, text="Clear", width=7,
                   command=lambda: self.log.delete("1.0", "end")).pack(side="right")
        self.log = ScrolledText(lf, height=9, font=("Consolas", 9), bg="#101418",
                                fg="#cfd6df", insertbackground="white",
                                wrap="none", borderwidth=0)
        self.log.pack(fill="both", expand=True)
        for tag, col in [("tx", "#5ec8f5"), ("rx", "#a6e3a1"), ("can_tx", "#7aa2ff"),
                         ("can_rx", "#7fe0a0"), ("err", BAD), ("info", DIM)]:
            self.log.tag_config(tag, foreground=col)

    def _build_outputs(self, parent):
        f = self._panel(parent, "Outputs")
        rows = [
            ("SMPS (machine power)", "M1", "M0", FN_SMPS),
            ("Heater relay (GP16)", "H1", "H0", FN_HEATER),
            ("Work light (GP17)", "L1", "L0", FN_LIGHT),
            ("UV lamp (GP19)", "UV1", "UV0", FN_UV),
        ]
        for label, on, off, fn in rows:
            r = ttk.Frame(f, style="TFrame")
            r.pack(fill="x", pady=2)
            r.configure(style="TFrame")
            ttk.Label(r, text=label, width=22, style="Panel.TLabel").pack(side="left")
            self._btn(r, "ON", lambda o=on, n=fn: self.send(o, n, 0, 0, 0, 0, 1),
                      "On.TButton", 6)
            self._btn(r, "OFF", lambda o=off, n=fn: self.send(o, n, 0, 0, 0, 0, 0),
                      "Off.TButton", 6)
        # SMPS extra: soft off over CAN has no serial equivalent
        r = ttk.Frame(f)
        r.pack(fill="x", pady=2)
        ttk.Label(r, text="SMPS soft-off (CAN only)", width=22,
                  style="Panel.TLabel").pack(side="left")
        self._btn(r, "Soft OFF", lambda: self.send(None, FN_SMPS, 0, 0, 0, 0, 0), w=9)
        ttk.Label(r, text="restarts after 10 s", style="Dim.TLabel").pack(side="left", padx=6)

        r = ttk.Frame(f)
        r.pack(fill="x", pady=(8, 0))
        self._btn(r, "ALL ON", self._all_on, "On.TButton", 10)
        self._btn(r, "ALL OFF", self._all_off, "Off.TButton", 10)
        self._btn(r, "STATUS", lambda: self.send("STATUS", FN_STATUS), "Accent.TButton", 10)

    def _build_fan(self, parent):
        f = self._panel(parent, "HEPA Fan (GP18, PWM 25 kHz)")
        r = ttk.Frame(f)
        r.pack(fill="x")
        self._btn(r, "Fan ON", lambda: self.send("F1", FN_FAN, 0, 0, 0, 0, 1),
                  "On.TButton", 8)
        self._btn(r, "Fan OFF", lambda: self.send("F0", FN_FAN, 0, 0, 0, 0, 0),
                  "Off.TButton", 8)
        self.fan_lbl = ttk.Label(r, text="Speed: 100 %", width=12, style="Panel.TLabel")
        self.fan_lbl.pack(side="left", padx=8)
        r2 = ttk.Frame(f)
        r2.pack(fill="x", pady=(6, 0))
        self.fan_var = tk.IntVar(value=100)
        ttk.Scale(r2, from_=0, to=100, variable=self.fan_var, orient="horizontal",
                  length=210, command=self._fan_moved).pack(side="left")
        self._btn(r2, "Set speed", self._set_fan, "Accent.TButton", 10)
        ttk.Label(f, text="Duty is floored at 25 % by firmware while fan is ON.",
                  style="Dim.TLabel").pack(anchor="w", pady=(4, 0))

    def _build_neo(self, parent):
        f = self._panel(parent, "NeoPixel (25 x WS2812B)")
        r = ttk.Frame(f)
        r.pack(fill="x", pady=2)
        for name in ["IDLE", "INIT", "RUNNING", "COMPLETE", "ERROR", "OFF"]:
            self._btn(r, name, lambda n=name: self._neo_state(n), w=8)
        r = ttk.Frame(f)
        r.pack(fill="x", pady=2)
        self._btn(r, "RAINBOW", lambda: self.send("NEO RAINBOW", None), w=9)
        ttk.Label(r, text="(serial only)", style="Dim.TLabel").pack(side="left", padx=4)

        r = ttk.Frame(f)
        r.pack(fill="x", pady=(6, 2))
        ttk.Label(r, text="Progress 0-25:", style="Panel.TLabel").pack(side="left")
        self.prog = tk.IntVar(value=10)
        ttk.Spinbox(r, from_=0, to=25, textvariable=self.prog, width=4).pack(side="left", padx=4)
        self._btn(r, "Set", self._neo_progress, w=6)

        r = ttk.Frame(f)
        r.pack(fill="x", pady=2)
        self.rgb = [tk.IntVar(value=v) for v in (255, 255, 255)]
        for lab, var in zip("RGB", self.rgb):
            ttk.Label(r, text=lab + ":", style="Panel.TLabel").pack(side="left")
            ttk.Spinbox(r, from_=0, to=255, textvariable=var, width=4).pack(side="left", padx=(2, 6))
        self._btn(r, "Set colour", self._neo_color, "Accent.TButton", 10)

        r = ttk.Frame(f)
        r.pack(fill="x", pady=2)
        for name, rgb in [("White", (255, 255, 255)), ("Red", (255, 0, 0)),
                          ("Green", (0, 255, 0)), ("Blue", (0, 0, 255)),
                          ("Warm", (255, 160, 32))]:
            self._btn(r, name, lambda c=rgb: self._neo_preset(c), w=6)

        r = ttk.Frame(f)
        r.pack(fill="x", pady=(6, 0))
        ttk.Label(r, text="Fill first N px (CAN only):", style="Panel.TLabel").pack(side="left")
        self.fill_n = tk.IntVar(value=12)
        ttk.Spinbox(r, from_=0, to=25, textvariable=self.fill_n, width=4).pack(side="left", padx=4)
        self._btn(r, "Fill", self._neo_fill, w=6)

    def _build_status(self, parent):
        f = self._panel(parent, "Outputs (live)")
        g = ttk.Frame(f)
        g.pack(fill="x")
        self.leds = {}
        spec = [("smps", "SMPS", GOOD), ("heater", "Heater", WARN),
                ("light", "Work light", WARN), ("uv", "UV lamp", BLUE),
                ("fan", "Fan", GOOD)]
        for i, (k, label, col) in enumerate(spec):
            c = ttk.Frame(g)
            c.grid(row=i // 3, column=i % 3, sticky="w", padx=10, pady=3)
            led = LED(c)
            led.pack(side="left", padx=(0, 6))
            ttk.Label(c, text=label, width=11, style="Panel.TLabel").pack(side="left")
            self.leds[k] = (led, col)

        f = self._panel(parent, "Sensors (live)")
        g = ttk.Frame(f)
        g.pack(fill="x")
        spec = [("limit", "Limit switch", WARN), ("hall", "Door closed (hall)", GOOD),
                ("emergency", "EMERGENCY STOP", BAD), ("powerbtn", "Power button", BLUE)]
        for i, (k, label, col) in enumerate(spec):
            c = ttk.Frame(g)
            c.grid(row=i // 2, column=i % 2, sticky="w", padx=10, pady=3)
            led = LED(c)
            led.pack(side="left", padx=(0, 6))
            ttk.Label(c, text=label, width=18, style="Panel.TLabel").pack(side="left")
            self.leds[k] = (led, col)

        f = self._panel(parent, "System")
        self.vars = {k: tk.StringVar(value="--") for k in
                     ("v24", "neo", "fanspd", "restart", "diag", "age")}
        for label, key in [("24 V rail", "v24"), ("NeoPixel state", "neo"),
                           ("Fan duty", "fanspd"), ("SMPS restart", "restart"),
                           ("CAN diag", "diag"), ("Last update", "age")]:
            r = ttk.Frame(f)
            r.pack(fill="x", pady=1)
            ttk.Label(r, text=label, width=15, style="Dim.TLabel").pack(side="left")
            ttk.Label(r, textvariable=self.vars[key], style="Panel.TLabel",
                      font=("Consolas", 10, "bold")).pack(side="left")

    def _build_console(self, parent):
        f = self._panel(parent, "Serial console")
        r = ttk.Frame(f)
        r.pack(fill="x")
        self.cmd_var = tk.StringVar()
        e = ttk.Entry(r, textvariable=self.cmd_var, font=("Consolas", 10))
        e.pack(side="left", fill="x", expand=True, padx=(0, 6))
        e.bind("<Return>", lambda _e: self._send_console())
        ttk.Button(r, text="Send", width=7, command=self._send_console).pack(side="left")
        ttk.Label(f, text="HELP  STATUS  M1  FAN 60  ADC  LIMIT  HALL  SET 0x03 1  NEO 255 0 0",
                  style="Dim.TLabel").pack(anchor="w", pady=(3, 0))

        f = self._panel(parent, "CAN raw frame (to 0x6A5)")
        r = ttk.Frame(f)
        r.pack(fill="x")
        self.raw_var = tk.StringVar(value="09 00 00 00 00 00 00 00")
        ttk.Entry(r, textvariable=self.raw_var, font=("Consolas", 10), width=28
                  ).pack(side="left", padx=(0, 6))
        ttk.Button(r, text="Send", width=7, command=self._send_raw).pack(side="left")

    # --------------------------------------------------------- port / links
    def _refresh_ports(self):
        ports = list(serial.tools.list_ports.comports())
        items = [f"{p.device} - {p.description}" for p in ports]
        self.port_cb["values"] = items
        if items and not self.port_var.get():
            pick = next((i for i in items if "USB Serial" in i or "Pico" in i), items[0])
            self.port_var.set(pick)

    def _toggle_ser(self):
        if self.ser:
            self._ser_close()
            return
        port = self.port_var.get().split(" - ")[0].strip()
        if not port:
            messagebox.showwarning("Serial", "Pick a COM port first.")
            return
        try:
            self.ser = serial.Serial(port, 115200, timeout=0.1, write_timeout=2)
            self.ser.dtr = True
        except Exception as e:
            self.ser = None
            messagebox.showerror("Serial connect failed", str(e))
            return
        self.ser_run = True
        threading.Thread(target=self._ser_reader, daemon=True).start()
        self.ser_btn.config(text="Disconnect")
        self.ser_lbl.config(text=f"{port} @ 115200", foreground=GOOD)
        self._log(f"Serial connected: {port}", "info")
        self.root.after(300, lambda: self.send("STATUS", FN_STATUS))

    def _ser_close(self):
        self.ser_run = False
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None
        self.ser_btn.config(text="Connect")
        self.ser_lbl.config(text="Disconnected", foreground=DIM)
        self._log("Serial disconnected", "info")

    def _ser_reader(self):
        buf = b""
        while self.ser_run and self.ser:
            try:
                data = self.ser.read(256)
            except Exception as e:
                self.q.put(("ser_err", str(e)))
                return
            if not data:
                continue
            buf += data
            while b"\n" in buf:
                line, _, buf = buf.partition(b"\n")
                txt = line.decode("utf-8", "replace").strip()
                if txt:
                    self.q.put(("ser", txt))

    def _toggle_can(self):
        if self.bus:
            self.can_run = False
            try:
                self.bus.shutdown()
            except Exception:
                pass
            self.bus = None
            self.can_btn.config(text="CAN Connect")
            self._log("CAN disconnected", "info")
            return
        try:
            kw = dict(interface=self.can_if.get(), channel=self.can_ch.get().strip(),
                      bitrate=500000)
            if kw["interface"] == "virtual":
                kw.pop("bitrate")
            self.bus = can.Bus(**kw)
        except Exception as e:
            self.bus = None
            messagebox.showerror("CAN connect failed", str(e))
            return
        self.can_run = True
        threading.Thread(target=self._can_reader, daemon=True).start()
        self.can_btn.config(text="CAN Disconnect")
        self._log(f"CAN connected: {self.can_if.get()} / {self.can_ch.get()}", "info")

    def _can_reader(self):
        while self.can_run and self.bus:
            try:
                m = self.bus.recv(timeout=0.1)
            except Exception as e:
                self.q.put(("can_err", str(e)))
                return
            if m is not None and m.arbitration_id == CAN_ID_RESP and len(m.data) >= 8:
                self.q.put(("can", bytes(m.data)))

    # ----------------------------------------------------------------- send
    def send(self, ser_cmd, fn=None, sub=0, d2=0, d3=0, d4=0, onoff=0):
        """ser_cmd: ASCII string (or None = no serial equivalent).
           fn:      CAN function code (or None = no CAN equivalent)."""
        sent = False
        if self.ser and ser_cmd:
            try:
                self.ser.write((ser_cmd + "\n").encode())
                self._log(f"SER TX  {ser_cmd}", "tx")
                sent = True
            except Exception as e:
                self._log(f"Serial write failed: {e}", "err")
        if self.bus and fn is not None:
            data = [fn, sub & 0xFF, d2 & 0xFF, d3 & 0xFF, d4 & 0xFF, 0, 0, onoff]
            try:
                self.bus.send(can.Message(arbitration_id=CAN_ID_CMD, data=data,
                                          is_extended_id=False))
                self._log("CAN TX  %03X  %s" % (CAN_ID_CMD,
                          " ".join(f"{b:02X}" for b in data)), "can_tx")
                sent = True
            except Exception as e:
                self._log(f"CAN send failed: {e}", "err")
        if not sent:
            self._log("Nothing sent - connect serial (or CAN), or this command "
                      "is not available on the connected link.", "err")

    def _send_console(self):
        cmd = self.cmd_var.get().strip()
        if not cmd:
            return
        if not self.ser:
            self._log("Serial not connected.", "err")
            return
        self.send(cmd, None)
        self.cmd_var.set("")

    def _send_raw(self):
        try:
            data = [int(x, 16) for x in re.split(r"[\s,]+", self.raw_var.get().strip())]
        except ValueError:
            messagebox.showerror("Raw CAN", "Use hex bytes, e.g. 09 00 00 00 00 00 00 00")
            return
        if len(data) != 8 or any(not 0 <= b <= 255 for b in data):
            messagebox.showerror("Raw CAN", "Need exactly 8 hex bytes.")
            return
        if not self.bus:
            self._log("CAN not connected.", "err")
            return
        self.bus.send(can.Message(arbitration_id=CAN_ID_CMD, data=data, is_extended_id=False))
        self._log("CAN TX  6A5  " + " ".join(f"{b:02X}" for b in data), "can_tx")

    # -------------------------------------------------------- command helpers
    def _all_on(self):
        self.send("ALL1", None)
        for fn in (FN_SMPS, FN_HEATER, FN_LIGHT, FN_UV, FN_FAN):
            if self.bus:
                self.send(None, fn, onoff=1)

    def _all_off(self):
        self.send("ALL0", None)
        for fn in (FN_SMPS, FN_HEATER, FN_LIGHT, FN_UV, FN_FAN):
            if self.bus:
                self.send(None, fn, d2=1 if fn == FN_SMPS else 0, onoff=0)

    def _fan_moved(self, _v):
        self.fan_lbl.config(text=f"Speed: {self.fan_var.get()} %")

    def _set_fan(self):
        n = max(0, min(100, int(self.fan_var.get())))
        self.send(f"FAN {n}", FN_FAN_PWM, sub=n)

    def _neo_state(self, name):
        self.send(f"NEO {name}", FN_NEO_STATE, sub=NEO_SUBCODE[name])

    def _neo_progress(self):
        n = max(0, min(25, int(self.prog.get())))
        self.send(f"NEO PROGRESS {n}", FN_NEO_STATE, sub=NEO_SUBCODE["PROGRESS"], d2=n)

    def _neo_color(self):
        r, g, b = [max(0, min(255, int(v.get()))) for v in self.rgb]
        self.send(f"NEO {r} {g} {b}", FN_NEO_COLOR, sub=r, d2=g, d3=b)

    def _neo_preset(self, c):
        for v, x in zip(self.rgb, c):
            v.set(x)
        self._neo_color()

    def _neo_fill(self):
        r, g, b = [max(0, min(255, int(v.get()))) for v in self.rgb]
        n = max(0, min(25, int(self.fill_n.get())))
        self.send(None, FN_NEO_FILL, sub=n, d2=r, d3=g, d4=b)

    # --------------------------------------------------------------- receive
    def _pump(self):
        try:
            while True:
                kind, val = self.q.get_nowait()
                if kind == "ser":
                    self._on_serial_line(val)
                elif kind == "can":
                    self._on_can(val)
                elif kind == "ser_err":
                    self._log(f"Serial error: {val}", "err")
                    self._ser_close()
                elif kind == "can_err":
                    self._log(f"CAN error: {val}", "err")
        except queue.Empty:
            pass
        if self.last_rx:
            self.vars["age"].set(f"{time.time() - self.last_rx:.1f} s ago")
        self.root.after(40, self._pump)

    def _on_serial_line(self, line):
        if line.startswith("CANDBG"):
            m = re.findall(r"(\w+)=(\d+)", line)
            self.vars["diag"].set(" ".join(f"{k}={v}" for k, v in m))
            if not self.hide_dbg.get():
                self._log(f"SER RX  {line}", "rx")
            return
        self._log(f"SER RX  {line}", "rx")
        if line.startswith("{"):
            try:
                self._apply(json.loads(line))
            except json.JSONDecodeError:
                pass

    def _on_can(self, d):
        self._log("CAN RX  6B5  " + " ".join(f"{b:02X}" for b in d), "can_rx")
        if d[0] == 0xFF and d[7] == 0x01:  # STATUS frame
            bits, sns, neo = d[1], d[2], d[3]
            adc = (d[4] << 8) | d[5]
            self._apply({
                "smps": bool(bits & BIT_SMPS), "uv": bool(bits & BIT_UV),
                "fan": bool(bits & BIT_FAN), "light": bool(bits & BIT_LIGHT),
                "heater": bool(bits & BIT_HEATER),
                "limit": bool(sns & SNS_LIMIT), "hall": bool(sns & SNS_HALL),
                "emergency": bool(sns & SNS_EMG),
                "v24": adc / 4095.0 * 3.3 * 11.0, "neo": neo})
        elif d[7] == 0x02:  # ACK: only sensors in d[6]
            sns = d[6]
            self._apply({"limit": bool(sns & SNS_LIMIT), "hall": bool(sns & SNS_HALL),
                         "emergency": bool(sns & SNS_EMG)})

    def _apply(self, d):
        self.last_rx = time.time()
        for k, (led, col) in self.leds.items():
            if k in d:
                led.set(bool(d[k]), col)
        if "v24" in d:
            self.vars["v24"].set(f"{float(d['v24']):.2f} V")
        if "neo" in d:
            self.vars["neo"].set(NEO_ENUM.get(int(d["neo"]), str(d["neo"])))
        if "fanspd" in d:
            self.vars["fanspd"].set(f"{d['fanspd']} %")
        if "smps" in d:
            self.vars["restart"].set(f"in {d['restart_in']} s" if "restart_in" in d else "-")
        if "drops" in d:
            self.vars["diag"].set("rx_drop=%s bus_err=%s tx_fail=%s tx_try=%s tx_ok=%s" % (
                d.get("drops"), d.get("buserr"), d.get("txfail"), d.get("txtry"), d.get("txok")))
        if "emergency" in d:
            self.banner.config(text="EMERGENCY STOP ACTIVE" if d["emergency"] else "")

    def _autopoll(self):
        if self.poll_var.get() and (self.ser or self.bus):
            if self.ser:
                try:
                    self.ser.write(b"STATUS\n")
                except Exception:
                    pass
            elif self.bus:
                try:
                    self.bus.send(can.Message(arbitration_id=CAN_ID_CMD,
                                              data=[FN_STATUS, 0, 0, 0, 0, 0, 0, 0],
                                              is_extended_id=False))
                except Exception:
                    pass
        self.root.after(1000, self._autopoll)

    # ------------------------------------------------------------------ misc
    def _log(self, text, tag="info"):
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self.log.insert("end", f"{ts}  {text}\n", tag)
        if int(self.log.index("end-1c").split(".")[0]) > 3000:
            self.log.delete("1.0", "500.0")
        self.log.see("end")

    def _close(self):
        self.ser_run = self.can_run = False
        try:
            if self.ser:
                self.ser.close()
            if self.bus:
                self.bus.shutdown()
        except Exception:
            pass
        self.root.destroy()

    def run(self):
        self.root.mainloop()


if __name__ == "__main__":
    App().run()
