/*
 * ESP32-C3 + BNO055 wireless acceleration sender for RT_SPARK.
 *
 * Wiring:
 *   BNO055 VIN -> ESP32-C3 5V
 *   BNO055 GND -> GND
 *   BNO055 SDA -> ESP32-C3 GPIO8
 *   BNO055 SCL -> ESP32-C3 GPIO9
 *
 * Runtime architecture:
 *   BNO055 -> ESP32-C3 -> Wi-Fi TCP -> bridge.js:8090 -> WebSocket -> cable-monitor.html
 *
 * First-time setup:
 *   If Wi-Fi/server settings are missing, ESP32 starts AP "ESP32_BNO055_SETUP".
 *   Open http://192.168.4.1/ and save:
 *     - Wi-Fi SSID / password
 *     - Bridge host: the PC LAN IP, e.g. 192.168.43.128
 *     - Bridge port: 8090
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <math.h>

static const int I2C_SDA_PIN = 8;
static const int I2C_SCL_PIN = 9;
static const uint32_t SERIAL_BAUD = 115200;
static const uint32_t SAMPLE_INTERVAL_MS = 20;   // 50 Hz

static const char *SETUP_AP_SSID = "ESP32_BNO055_SETUP";
static const char *SETUP_AP_PASS = "12345678";
static const uint16_t DEFAULT_BRIDGE_PORT = 8090;

static const uint8_t BNO_ADDR_A = 0x28;
static const uint8_t BNO_ADDR_B = 0x29;
static uint8_t bnoAddr = BNO_ADDR_A;

static const uint8_t REG_CHIP_ID = 0x00;
static const uint8_t REG_PAGE_ID = 0x07;
static const uint8_t REG_LINEAR_ACCEL_DATA_X_LSB = 0x28;
static const uint8_t REG_OPR_MODE = 0x3D;
static const uint8_t REG_PWR_MODE = 0x3E;
static const uint8_t REG_SYS_TRIGGER = 0x3F;
static const uint8_t REG_UNIT_SEL = 0x3B;

static const uint8_t MODE_CONFIG = 0x00;
static const uint8_t MODE_NDOF = 0x0C;

struct AppConfig {
  String ssid;
  String pass;
  String host;
  uint16_t port;
};

static Preferences prefs;
static WebServer setupServer(80);
static WiFiClient tcpClient;
static AppConfig cfg;

static bool bnoReady = false;
static bool configMode = false;
static uint32_t lastSampleMs = 0;
static uint32_t lastStatusMs = 0;
static uint32_t lastWifiTryMs = 0;
static uint32_t lastTcpTryMs = 0;
static uint32_t lastHelloMs = 0;

static String htmlEscape(const String &s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else out += c;
  }
  return out;
}

static void sendLine(const String &line) {
  Serial.println(line);
  if (tcpClient.connected()) {
    tcpClient.println(line);
  }
}

static bool bnoWrite8(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(bnoAddr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission(true) == 0;
}

static bool bnoRead(uint8_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(bnoAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;

  size_t got = Wire.requestFrom((int)bnoAddr, (int)len, (int)true);
  if (got != len) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)Wire.read();
  return true;
}

static bool bnoRead8(uint8_t reg, uint8_t *value) {
  return bnoRead(reg, value, 1);
}

static int16_t le16(const uint8_t *p) {
  return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static bool probeAddr(uint8_t addr) {
  bnoAddr = addr;
  uint8_t id = 0;
  if (!bnoRead8(REG_CHIP_ID, &id)) return false;
  return id == 0xA0;
}

static bool bnoInit() {
  delay(700);
  if (!probeAddr(BNO_ADDR_A) && !probeAddr(BNO_ADDR_B)) return false;

  bnoWrite8(REG_OPR_MODE, MODE_CONFIG);
  delay(30);
  bnoWrite8(REG_PAGE_ID, 0x00);
  bnoWrite8(REG_PWR_MODE, 0x00);
  delay(10);
  bnoWrite8(REG_SYS_TRIGGER, 0x00);
  delay(10);
  bnoWrite8(REG_UNIT_SEL, 0x00);
  delay(10);
  bnoWrite8(REG_OPR_MODE, MODE_NDOF);
  delay(80);

  uint8_t id = 0;
  return bnoRead8(REG_CHIP_ID, &id) && id == 0xA0;
}

static bool readLinearAccel(float *ax, float *ay, float *az) {
  uint8_t buf[6];
  if (!bnoRead(REG_LINEAR_ACCEL_DATA_X_LSB, buf, sizeof(buf))) return false;

  *ax = (float)le16(&buf[0]) * 0.001f * 9.80665f;
  *ay = (float)le16(&buf[2]) * 0.001f * 9.80665f;
  *az = (float)le16(&buf[4]) * 0.001f * 9.80665f;
  return isfinite(*ax) && isfinite(*ay) && isfinite(*az);
}

static void loadConfig() {
  prefs.begin("rt_spark", false);
  cfg.ssid = prefs.getString("ssid", "");
  cfg.pass = prefs.getString("pass", "");
  cfg.host = prefs.getString("host", "");
  cfg.port = prefs.getUShort("port", DEFAULT_BRIDGE_PORT);
}

static void saveConfig(const String &ssid, const String &pass, const String &host, uint16_t port) {
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.putString("host", host);
  prefs.putUShort("port", port);
  cfg.ssid = ssid;
  cfg.pass = pass;
  cfg.host = host;
  cfg.port = port;
}

static bool hasConfig() {
  return cfg.ssid.length() > 0 && cfg.host.length() > 0 && cfg.port > 0;
}

static void handleRoot() {
  String page = F(
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32 BNO055 Setup</title>"
    "<style>body{font-family:Arial,sans-serif;margin:24px;background:#0b1020;color:#e5e7eb}"
    "input{width:100%;box-sizing:border-box;padding:10px;margin:6px 0 14px;border-radius:8px;border:1px solid #374151;background:#111827;color:#fff}"
    "button{padding:12px 18px;border:0;border-radius:8px;background:#3b82f6;color:white;font-weight:700}"
    ".card{max-width:520px;margin:auto;background:#111827;padding:20px;border-radius:16px;border:1px solid #263244}"
    ".hint{color:#9ca3af;font-size:14px;line-height:1.5}</style></head><body><div class='card'>"
    "<h2>RT_SPARK ESP32/BNO055 配置</h2>"
    "<p class='hint'>保存后 ESP32 会重启，并通过 Wi-Fi 连接 bridge.js:8090，把加速度数据送到网页。</p>"
    "<form method='POST' action='/save'>"
    "Wi-Fi SSID<input name='ssid' value='");
  page += htmlEscape(cfg.ssid);
  page += F("'>Wi-Fi 密码<input name='pass' type='password' value='");
  page += htmlEscape(cfg.pass);
  page += F("'>电脑/桥接服务器 IP<input name='host' value='");
  page += htmlEscape(cfg.host);
  page += F("' placeholder='例如 192.168.43.128'>端口<input name='port' type='number' value='");
  page += String(cfg.port ? cfg.port : DEFAULT_BRIDGE_PORT);
  page += F("'><button type='submit'>保存并重启</button></form>"
            "<p class='hint'>当前 ESP32 AP: ESP32_BNO055_SETUP / 密码 12345678，配置页 http://192.168.4.1/</p>"
            "</div></body></html>");
  setupServer.send(200, "text/html; charset=utf-8", page);
}

static void handleSave() {
  String ssid = setupServer.arg("ssid");
  String pass = setupServer.arg("pass");
  String host = setupServer.arg("host");
  uint16_t port = (uint16_t)setupServer.arg("port").toInt();
  if (port == 0) port = DEFAULT_BRIDGE_PORT;

  saveConfig(ssid, pass, host, port);
  setupServer.send(200, "text/html; charset=utf-8",
                   "<meta charset='utf-8'><h3>已保存，ESP32 正在重启。</h3>");
  delay(500);
  ESP.restart();
}

static void startConfigPortal() {
  configMode = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(SETUP_AP_SSID, SETUP_AP_PASS);
  setupServer.on("/", HTTP_GET, handleRoot);
  setupServer.on("/save", HTTP_POST, handleSave);
  setupServer.begin();
  Serial.printf("{\"type\":\"setup_ap\",\"source\":\"esp32_bno055\",\"ssid\":\"%s\",\"ip\":\"192.168.4.1\",\"ts\":%lu}\n",
                SETUP_AP_SSID, (unsigned long)millis());
}

static void beginWifi() {
  if (!hasConfig()) {
    startConfigPortal();
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  lastWifiTryMs = millis();
  Serial.printf("{\"type\":\"wifi_connecting\",\"source\":\"esp32_bno055\",\"ssid\":\"%s\",\"host\":\"%s\",\"port\":%u,\"ts\":%lu}\n",
                cfg.ssid.c_str(), cfg.host.c_str(), cfg.port, (unsigned long)millis());
}

static void maintainWifi() {
  if (configMode) {
    setupServer.handleClient();
    return;
  }

  if (WiFi.status() == WL_CONNECTED) return;

  const uint32_t now = millis();
  if (now - lastWifiTryMs >= 15000) {
    lastWifiTryMs = now;
    WiFi.disconnect();
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
    Serial.printf("{\"type\":\"wifi_retry\",\"source\":\"esp32_bno055\",\"ssid\":\"%s\",\"ts\":%lu}\n",
                  cfg.ssid.c_str(), (unsigned long)now);
  }
}

static void sendHello() {
  String line = String("{\"type\":\"device_hello\",\"source\":\"esp32_bno055\",\"device\":\"ESP32_BNO055\",") +
                "\"ip\":\"" + WiFi.localIP().toString() + "\"," +
                "\"rssi\":" + String(WiFi.RSSI()) + "," +
                "\"bno_ok\":" + String(bnoReady ? "true" : "false") + "," +
                "\"ts\":" + String((unsigned long)millis()) + "}";
  sendLine(line);
}

static void maintainTcp() {
  if (configMode || WiFi.status() != WL_CONNECTED) return;
  if (tcpClient.connected()) {
    const uint32_t now = millis();
    if (now - lastHelloMs >= 5000) {
      lastHelloMs = now;
      sendHello();
    }
    return;
  }

  const uint32_t now = millis();
  if (now - lastTcpTryMs < 3000) return;
  lastTcpTryMs = now;

  tcpClient.stop();
  tcpClient.setNoDelay(true);
  if (tcpClient.connect(cfg.host.c_str(), cfg.port)) {
    Serial.printf("{\"type\":\"tcp_connected\",\"source\":\"esp32_bno055\",\"host\":\"%s\",\"port\":%u,\"ip\":\"%s\",\"ts\":%lu}\n",
                  cfg.host.c_str(), cfg.port, WiFi.localIP().toString().c_str(), (unsigned long)now);
    sendHello();
  } else {
    Serial.printf("{\"type\":\"tcp_connect_failed\",\"source\":\"esp32_bno055\",\"host\":\"%s\",\"port\":%u,\"ts\":%lu}\n",
                  cfg.host.c_str(), cfg.port, (unsigned long)now);
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(300);

  loadConfig();
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000);

  bnoReady = bnoInit();
  Serial.printf("{\"type\":\"boot\",\"source\":\"esp32_bno055\",\"ok\":%s,\"addr\":\"0x%02X\",\"ts\":%lu}\n",
                bnoReady ? "true" : "false", bnoAddr, (unsigned long)millis());

  beginWifi();
}

void loop() {
  const uint32_t now = millis();
  maintainWifi();
  maintainTcp();

  if (!bnoReady) {
    if (now - lastStatusMs >= 1000) {
      lastStatusMs = now;
      bnoReady = bnoInit();
      sendLine(String("{\"type\":\"sensor_status\",\"source\":\"esp32_bno055\",\"ok\":") +
               (bnoReady ? "true" : "false") +
               ",\"addr\":\"0x" + String(bnoAddr, HEX) + "\"" +
               ",\"ts\":" + String((unsigned long)now) + "}");
    }
    delay(10);
    return;
  }

  if (now - lastSampleMs < SAMPLE_INTERVAL_MS) {
    delay(1);
    return;
  }
  lastSampleMs = now;

  float ax = 0, ay = 0, az = 0;
  if (readLinearAccel(&ax, &ay, &az)) {
    String line = String("{\"type\":\"raw_accel\",\"source\":\"esp32_bno055\",\"ts\":") +
                  String((unsigned long)now) +
                  ",\"ax\":" + String(ax, 3) +
                  ",\"ay\":" + String(ay, 3) +
                  ",\"az\":" + String(az, 3) + "}";
    sendLine(line);
  } else {
    bnoReady = false;
    sendLine(String("{\"type\":\"sensor_error\",\"source\":\"esp32_bno055\",\"msg\":\"bno055_read_failed\",\"ts\":") +
             String((unsigned long)now) + "}");
  }
}
