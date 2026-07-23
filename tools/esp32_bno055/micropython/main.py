from machine import Pin, I2C, reset
import network
import socket
import time
import json
import os
import math

I2C_SDA_PIN = 8
I2C_SCL_PIN = 9
SAMPLE_INTERVAL_MS = 20

CONFIG_FILE = "rt_spark_config.json"
SETUP_AP_SSID = "ESP32_BNO055_SETUP"
SETUP_AP_PASS = "12345678"
DEFAULT_BRIDGE_PORT = 8090

BNO_ADDRS = (0x28, 0x29)
REG_CHIP_ID = 0x00
REG_PAGE_ID = 0x07
REG_LINEAR_ACCEL_DATA_X_LSB = 0x28
REG_OPR_MODE = 0x3D
REG_PWR_MODE = 0x3E
REG_SYS_TRIGGER = 0x3F
REG_UNIT_SEL = 0x3B
MODE_CONFIG = 0x00
MODE_NDOF = 0x0C


def now_ms():
    return time.ticks_ms()


def ticks_diff(a, b):
    return time.ticks_diff(a, b)


def line(obj):
    s = json.dumps(obj)
    print(s)
    return s + "\n"


def load_config():
    try:
        with open(CONFIG_FILE, "r") as f:
            cfg = json.loads(f.read())
        cfg["port"] = int(cfg.get("port", DEFAULT_BRIDGE_PORT))
        return cfg
    except Exception:
        return {"ssid": "", "pass": "", "host": "", "port": DEFAULT_BRIDGE_PORT}


def save_config(cfg):
    with open(CONFIG_FILE, "w") as f:
        f.write(json.dumps(cfg))


def url_decode(s):
    s = s.replace("+", " ")
    out = ""
    i = 0
    while i < len(s):
        if s[i] == "%" and i + 2 < len(s):
            try:
                out += chr(int(s[i + 1:i + 3], 16))
                i += 3
                continue
            except Exception:
                pass
        out += s[i]
        i += 1
    return out


def parse_form(body):
    data = {}
    for pair in body.split("&"):
        if "=" in pair:
            k, v = pair.split("=", 1)
            data[url_decode(k)] = url_decode(v)
    return data


def html_escape(s):
    return str(s).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def start_config_portal(cfg):
    wlan = network.WLAN(network.STA_IF)
    wlan.active(False)
    ap = network.WLAN(network.AP_IF)
    ap.active(True)
    ap.config(essid=SETUP_AP_SSID, password=SETUP_AP_PASS, authmode=network.AUTH_WPA_WPA2_PSK)

    addr = socket.getaddrinfo("0.0.0.0", 80)[0][-1]
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(addr)
    srv.listen(1)

    print(line({
        "type": "setup_ap",
        "source": "esp32_bno055",
        "ssid": SETUP_AP_SSID,
        "password": SETUP_AP_PASS,
        "ip": "192.168.4.1",
        "ts": now_ms()
    }).strip())

    while True:
        client, remote = srv.accept()
        try:
            req = client.recv(2048).decode("utf-8", "ignore")
            first = req.split("\r\n", 1)[0]
            if first.startswith("POST /save"):
                body = req.split("\r\n\r\n", 1)[1] if "\r\n\r\n" in req else ""
                form = parse_form(body)
                new_cfg = {
                    "ssid": form.get("ssid", "").strip(),
                    "pass": form.get("pass", ""),
                    "host": form.get("host", "").strip(),
                    "port": int(form.get("port", DEFAULT_BRIDGE_PORT) or DEFAULT_BRIDGE_PORT)
                }
                save_config(new_cfg)
                resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
                resp += "<meta charset='utf-8'><h3>已保存，ESP32 正在重启。</h3>"
                client.send(resp)
                time.sleep(1)
                reset()
            else:
                page = """<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 BNO055 Setup</title>
<style>
body{font-family:Arial,sans-serif;margin:24px;background:#0b1020;color:#e5e7eb}
input{width:100%;box-sizing:border-box;padding:10px;margin:6px 0 14px;border-radius:8px;border:1px solid #374151;background:#111827;color:#fff}
button{padding:12px 18px;border:0;border-radius:8px;background:#3b82f6;color:white;font-weight:700}
.card{max-width:520px;margin:auto;background:#111827;padding:20px;border-radius:16px;border:1px solid #263244}
.hint{color:#9ca3af;font-size:14px;line-height:1.5}
</style></head><body><div class="card">
<h2>RT_SPARK ESP32/BNO055 配置</h2>
<p class="hint">保存后 ESP32 会重启，并通过 Wi-Fi 连接 bridge.js:8090，把加速度数据送到网页。</p>
<form method="POST" action="/save">
Wi-Fi SSID<input name="ssid" value="{ssid}">
Wi-Fi 密码<input name="pass" type="password" value="{password}">
电脑/桥接服务器 IP<input name="host" value="{host}" placeholder="例如 192.168.43.128">
端口<input name="port" type="number" value="{port}">
<button type="submit">保存并重启</button></form>
<p class="hint">当前 ESP32 热点：ESP32_BNO055_SETUP，密码：12345678，配置页：http://192.168.4.1/</p>
</div></body></html>""".format(
                    ssid=html_escape(cfg.get("ssid", "")),
                    password=html_escape(cfg.get("pass", "")),
                    host=html_escape(cfg.get("host", "")),
                    port=html_escape(cfg.get("port", DEFAULT_BRIDGE_PORT))
                )
                resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n" + page
                client.send(resp)
        except Exception as e:
            try:
                client.send("HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\n\r\n" + repr(e))
            except Exception:
                pass
        finally:
            client.close()


class BNO055:
    def __init__(self):
        self.i2c = I2C(0, scl=Pin(I2C_SCL_PIN), sda=Pin(I2C_SDA_PIN), freq=400000)
        self.addr = None

    def read8(self, reg):
        return self.i2c.readfrom_mem(self.addr, reg, 1)[0]

    def write8(self, reg, val):
        self.i2c.writeto_mem(self.addr, reg, bytes([val & 0xFF]))

    def init(self):
        time.sleep_ms(700)
        for addr in BNO_ADDRS:
            self.addr = addr
            try:
                if self.read8(REG_CHIP_ID) == 0xA0:
                    break
            except Exception:
                self.addr = None
        if self.addr is None:
            return False

        self.write8(REG_OPR_MODE, MODE_CONFIG)
        time.sleep_ms(30)
        self.write8(REG_PAGE_ID, 0)
        self.write8(REG_PWR_MODE, 0)
        time.sleep_ms(10)
        self.write8(REG_SYS_TRIGGER, 0)
        time.sleep_ms(10)
        self.write8(REG_UNIT_SEL, 0)
        time.sleep_ms(10)
        self.write8(REG_OPR_MODE, MODE_NDOF)
        time.sleep_ms(80)
        return self.read8(REG_CHIP_ID) == 0xA0

    def linear_accel(self):
        data = self.i2c.readfrom_mem(self.addr, REG_LINEAR_ACCEL_DATA_X_LSB, 6)
        vals = []
        for i in range(0, 6, 2):
            v = data[i] | (data[i + 1] << 8)
            if v & 0x8000:
                v -= 0x10000
            vals.append(v * 0.001 * 9.80665)
        return vals[0], vals[1], vals[2]


def connect_wifi(cfg):
    sta = network.WLAN(network.STA_IF)
    sta.active(True)
    try:
        sta.config(pm=0xa11140)  # disable powersave on common ESP32 MicroPython builds
    except Exception:
        pass
    if not sta.isconnected():
        sta.connect(cfg["ssid"], cfg.get("pass", ""))
        start = now_ms()
        while not sta.isconnected() and ticks_diff(now_ms(), start) < 20000:
            time.sleep_ms(250)
    return sta if sta.isconnected() else None


def connect_tcp(cfg):
    s = socket.socket()
    s.settimeout(4)
    s.connect((cfg["host"], int(cfg.get("port", DEFAULT_BRIDGE_PORT))))
    s.settimeout(None)
    return s


def main():
    cfg = load_config()
    if not cfg.get("ssid") or not cfg.get("host"):
        start_config_portal(cfg)

    bno = BNO055()
    bno_ok = bno.init()
    print(line({
        "type": "boot",
        "source": "esp32_bno055",
        "ok": bno_ok,
        "addr": "0x%02X" % (bno.addr or 0),
        "ts": now_ms()
    }).strip())

    sta = None
    tcp = None
    last_sample = now_ms()
    last_hello = 0
    last_reconnect = 0

    while True:
        if sta is None or not sta.isconnected():
            sta = connect_wifi(cfg)
            tcp = None
            if sta:
                print(line({
                    "type": "wifi_connected",
                    "source": "esp32_bno055",
                    "ip": sta.ifconfig()[0],
                    "rssi": sta.status("rssi"),
                    "ts": now_ms()
                }).strip())
            else:
                print(line({
                    "type": "wifi_failed",
                    "source": "esp32_bno055",
                    "ssid": cfg.get("ssid", ""),
                    "ts": now_ms()
                }).strip())
                time.sleep(5)
                continue

        if tcp is None:
            try:
                tcp = connect_tcp(cfg)
                hello = line({
                    "type": "device_hello",
                    "source": "esp32_bno055",
                    "device": "ESP32_BNO055",
                    "ip": sta.ifconfig()[0],
                    "rssi": sta.status("rssi"),
                    "bno_ok": bno_ok,
                    "ts": now_ms()
                })
                tcp.send(hello.encode())
            except Exception as e:
                tcp = None
                if ticks_diff(now_ms(), last_reconnect) > 3000:
                    last_reconnect = now_ms()
                    print(line({
                        "type": "tcp_connect_failed",
                        "source": "esp32_bno055",
                        "host": cfg.get("host", ""),
                        "port": int(cfg.get("port", DEFAULT_BRIDGE_PORT)),
                        "err": repr(e),
                        "ts": now_ms()
                    }).strip())
                time.sleep_ms(500)
                continue

        t = now_ms()
        if ticks_diff(t, last_hello) > 5000:
            last_hello = t
            try:
                tcp.send(line({
                    "type": "device_hello",
                    "source": "esp32_bno055",
                    "device": "ESP32_BNO055",
                    "ip": sta.ifconfig()[0],
                    "rssi": sta.status("rssi"),
                    "bno_ok": bno_ok,
                    "ts": t
                }).encode())
            except Exception:
                try:
                    tcp.close()
                except Exception:
                    pass
                tcp = None
                continue

        if ticks_diff(t, last_sample) >= SAMPLE_INTERVAL_MS:
            last_sample = t
            try:
                if not bno_ok:
                    bno_ok = bno.init()
                    continue
                ax, ay, az = bno.linear_accel()
                if not (math.isfinite(ax) and math.isfinite(ay) and math.isfinite(az)):
                    raise ValueError("non-finite acceleration")
                tcp.send(line({
                    "type": "raw_accel",
                    "source": "esp32_bno055",
                    "ts": t,
                    "ax": round(ax, 3),
                    "ay": round(ay, 3),
                    "az": round(az, 3)
                }).encode())
            except Exception as e:
                bno_ok = False
                try:
                    tcp.send(line({
                        "type": "sensor_error",
                        "source": "esp32_bno055",
                        "msg": repr(e),
                        "ts": now_ms()
                    }).encode())
                except Exception:
                    try:
                        tcp.close()
                    except Exception:
                        pass
                    tcp = None
        time.sleep_ms(2)


main()
