/*
 * bridge.js - 纯无线桥接服务（零 COM 口，TCP ↔ WebSocket 双向桥接）
 *
 * 架构：
 *   星火一号 → UART2 → Air778E → 4G → frp → TCP → bridge.js → WebSocket → 网页
 *   网页 → WebSocket → bridge.js → TCP → frp → 4G → Air778E → UART2 → 星火一号
 *
 * 说明：
 *   本服务完全不依赖任何 COM 口/串口。它只负责：
 *   1. 维护一个本地 TCP Server（接收 Sakura Frp 转发来的 Air778E 连接）
 *   2. 维护一个 WebSocket 服务器（供本地网页连接）
 *   3. 从 TCP 收到的数据（来自 STM32）广播给所有 WebSocket 客户端
 *   4. 从 WebSocket 收到的命令通过 TCP 发送给 STM32
 *
 * 数据流：
 *   上行：STM32 JSON → Air778E → Sakura Frp → bridge.js:8090 → WebSocket → 网页
 *   下行：网页 → WebSocket → bridge.js:8090 → Sakura Frp → Air778E → STM32
 *
 * 用法：
 *   node bridge.js
 *   node bridge.js --tcp-host=0.0.0.0 --tcp-port=8090 --ws-port=8080
 *
 * 零依赖：不使用 npm 包，WebSocket 用 Node.js 内置 http + crypto 手写实现
 */

const fs = require("fs");
const net = require("net");
const http = require("http");
const crypto = require("crypto");
const path = require("path");

// ---- 参数解析 ----
const args = process.argv.slice(2);
function getArg(name, defaultVal) {
  const flag = `--${name}=`;
  const found = args.find((a) => a.startsWith(flag));
  return found ? found.slice(flag.length) : defaultVal;
}

const TCP_HOST = getArg("tcp-host", "0.0.0.0");
const TCP_PORT = parseInt(getArg("tcp-port", "8090"), 10);
const WS_PORT  = parseInt(getArg("ws-port", "8080"), 10);
const DASHBOARD_FILE = path.join(__dirname, "cable-monitor.html");

// ---- WS 客户端连接池 ----
const wsClients = new Set();

// ---- 活跃 TCP socket（来自 Air778E / Sakura Frp）----
const tcpClients = new Set();

// ---- 统计信息 ----
const stats = {
  tcpConnected: 0,
  tcpConnections: 0,
  wsConnections: 0,
  totalMessages: 0,
  bytesReceived: 0,
  bytesSent: 0,
  commandsSent: 0,
  startTime: Date.now(),
};

// ---- 日志带时间戳 ----
function log(tag, msg) {
  const ts = new Date().toLocaleTimeString("zh-CN", { hour12: false });
  console.log(`[${ts}] [${tag}] ${msg}`);
}

// ---- 向所有 WS 客户端广播文本 ----
function broadcastText(text) {
  for (const ws of wsClients) {
    ws.sendText(text);
  }
}

// ---- 向所有 WS 客户端广播 JSON 对象 ----
function broadcastJSON(obj) {
  broadcastText(JSON.stringify(obj));
}

// ================================================================
// WebSocket 实现（基于 RFC 6455，使用 Node.js 原生 http + crypto）
// ================================================================

function acceptWebSocketKey(key) {
  const magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  return crypto.createHash("sha1").update(key + magic).digest("base64");
}

function createWSConnection(socket) {
  const ws = {
    socket: socket,
    alive: true,
    sendText: function(text) {
      if (!this.alive) return;
      const data = Buffer.from(text, "utf8");
      const mask = data.length <= 125 ? 2 : (data.length <= 65535 ? 4 : 10);
      const frame = Buffer.allocUnsafe(mask + data.length);
      frame[0] = 0x81;
      if (data.length <= 125) {
        frame[1] = data.length;
      } else if (data.length <= 65535) {
        frame[1] = 126;
        frame.writeUInt16BE(data.length, 2);
      } else {
        frame[1] = 127;
        frame.writeUInt32BE(0, 2);
        frame.writeUInt32BE(data.length, 6);
      }
      data.copy(frame, mask);
      try { socket.write(frame); } catch (e) { this.alive = false; }
    },
    close: function() {
      this.alive = false;
      try { socket.end(); } catch (e) {}
    }
  };
  return ws;
}

function parseWSFrame(buffer) {
  if (buffer.length < 2) return { consumed: 0, payload: null, opcode: null };

  const opcode = buffer[0] & 0x0f;
  const masked = (buffer[1] & 0x80) !== 0;
  let payloadLen = buffer[1] & 0x7f;
  let offset = 2;

  if (payloadLen === 126) {
    if (buffer.length < 4) return { consumed: 0 };
    payloadLen = buffer.readUInt16BE(2);
    offset = 4;
  } else if (payloadLen === 127) {
    if (buffer.length < 10) return { consumed: 0 };
    payloadLen = Number(buffer.readBigUInt64BE(2));
    offset = 10;
  }

  if (masked) {
    if (buffer.length < offset + 4 + payloadLen) return { consumed: 0 };
    const mask = buffer.slice(offset, offset + 4);
    offset += 4;
    const payload = Buffer.allocUnsafe(payloadLen);
    for (let i = 0; i < payloadLen; i++) {
      payload[i] = buffer[offset + i] ^ mask[i % 4];
    }
    return { consumed: offset + payloadLen, payload: payload.toString("utf8"), opcode };
  } else {
    if (buffer.length < offset + payloadLen) return { consumed: 0 };
    const payload = buffer.slice(offset, offset + payloadLen).toString("utf8");
    return { consumed: offset + payloadLen, payload, opcode };
  }
}

// HTTP + WebSocket 服务器
const httpServer = http.createServer((req, res) => {
  const pathname = new URL(req.url, "http://localhost").pathname;

  if (pathname === "/health") {
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify({
      status: "ok",
      mode: "wireless-only",
      tcpConnected: tcpClients.size > 0,
      tcpConnections: tcpClients.size,
      tcpHost: TCP_HOST,
      tcpPort: TCP_PORT,
      wsConnections: stats.wsConnections,
      totalMessages: stats.totalMessages,
      bytesReceived: stats.bytesReceived,
      bytesSent: stats.bytesSent,
      commandsSent: stats.commandsSent,
      uptime: Math.floor((Date.now() - stats.startTime) / 1000)
    }));
    return;
  }

  if (pathname === "/" || pathname === "/cable-monitor.html") {
    fs.readFile(DASHBOARD_FILE, "utf8", (err, html) => {
      if (err) {
        res.writeHead(500, { "Content-Type": "text/plain; charset=utf-8" });
        res.end("Failed to load cable-monitor.html: " + err.message);
        return;
      }
      res.writeHead(200, {
        "Content-Type": "text/html; charset=utf-8",
        "Cache-Control": "no-store",
      });
      res.end(html);
    });
    return;
  }

  res.writeHead(200, { "Content-Type": "text/plain; charset=utf-8" });
  res.end("RT_SPARK Wireless Bridge. Open http://localhost:" + WS_PORT + "/");
});

httpServer.on("upgrade", (req, socket, head) => {
  const key = req.headers["sec-websocket-key"];
  if (!key) {
    socket.destroy();
    return;
  }

  const acceptKey = acceptWebSocketKey(key);
  const responseHeaders = [
    "HTTP/1.1 101 Switching Protocols",
    "Upgrade: websocket",
    "Connection: Upgrade",
    "Sec-WebSocket-Accept: " + acceptKey,
    "Sec-WebSocket-Version: 13",
    "",
    ""
  ].join("\r\n");

  socket.write(responseHeaders);

  const ws = createWSConnection(socket);
  wsClients.add(ws);
  stats.wsConnections++;

  const clientIP = req.socket.remoteAddress;
  log("WS", `网页客户端连接 (${clientIP})  当前: ${stats.wsConnections}`);

  ws.sendText(JSON.stringify({
    type: "system",
    event: "ws_connected",
    message: "RT_SPARK 无线桥接已连接",
    mode: "wireless-only",
    stats: {
      uptime: Math.floor((Date.now() - stats.startTime) / 1000),
      totalMessages: stats.totalMessages,
      bytesReceived: stats.bytesReceived,
      tcpConnected: tcpClients.size > 0,
    },
    ts: Date.now(),
  }));

  // 如果无线设备已连接，按设备类型通知新客户端恢复在线状态
  if (countClientsByType("air778e") > 0) {
    ws.sendText(JSON.stringify({
      type: "system",
      event: "device_connected",
      remote: "Air778E",
      ts: Date.now(),
    }));
  }
  if (countClientsByType("esp32_bno055") > 0) {
    ws.sendText(JSON.stringify({
      type: "system",
      event: "esp32_connected",
      remote: "ESP32-BNO055",
      ts: Date.now(),
    }));
  }

  let wsBuffer = Buffer.alloc(0);

  socket.on("data", (chunk) => {
    wsBuffer = Buffer.concat([wsBuffer, chunk]);

    while (wsBuffer.length > 0) {
      const result = parseWSFrame(wsBuffer);
      if (result.consumed === 0) break;

      wsBuffer = wsBuffer.slice(result.consumed);

      if (result.opcode === 8) {
        ws.alive = false;
        return;
      }

      if (result.opcode === 9) {
        const pong = Buffer.allocUnsafe(2);
        pong[0] = 0x8a;
        pong[1] = 0;
        try { socket.write(pong); } catch(e) {}
        continue;
      }

      if (result.opcode === 1 && result.payload) {
        const cmd = result.payload.trim();
        if (cmd.length === 0) continue;
        log("CMD", `网页->STM32: ${cmd}`);

        // 纯无线模式：命令只能通过 TCP 发送
        if (tcpClients.size > 0) {
          try {
            for (const client of tcpClients) {
              if (client.writable) {
                client.write(cmd + "\n");
              }
            }
            stats.bytesSent += cmd.length + 1;
            stats.commandsSent++;
            log("TCP", `命令已发送: ${cmd}`);
            ws.sendText(JSON.stringify({
              type: "system",
              event: "command_sent",
              command: cmd,
              via: "wireless",
              ts: Date.now(),
            }));
          } catch (e) {
            log("TCP", `发送失败: ${e.message}`);
            ws.sendText(JSON.stringify({
              type: "system",
              event: "command_failed",
              command: cmd,
              reason: "TCP 发送失败",
              ts: Date.now(),
            }));
          }
        } else {
          log("CMD", `无法转发: TCP 未连接 (目标 ${TCP_HOST}:${TCP_PORT})`);
          ws.sendText(JSON.stringify({
            type: "system",
            event: "command_failed",
            command: cmd,
            reason: "TCP 未连接，无线通道不可用",
            ts: Date.now(),
          }));
        }
      }
    }
  });

  socket.on("close", () => {
    ws.alive = false;
    wsClients.delete(ws);
    stats.wsConnections--;
    log("WS", `网页客户端断开  剩余: ${stats.wsConnections}`);
  });

  socket.on("error", (err) => {
    log("WS", `错误: ${err.message}`);
  });
});

httpServer.on("error", (err) => {
  if (err.code === "EADDRINUSE") {
    log("WS", `端口 ${WS_PORT} 被占用！请用 --ws-port=xxxx 指定其他端口`);
  } else {
    log("WS", `服务器错误: ${err.message}`);
  }
  process.exit(1);
});

httpServer.listen(WS_PORT, "0.0.0.0", () => {
  log("WS", `WebSocket Server 监听 ws://localhost:${WS_PORT}  (等待网页连接)`);
});

// ================================================================
// TCP 服务端 — 等待 Sakura Frp 将 Air778E 连接转发进来
// ================================================================
function countClientsByType(type) {
  let count = 0;
  for (const client of tcpClients) {
    if (client._deviceType === type) count++;
  }
  return count;
}

function handleTcpLine(line, socket) {
  if (line.length === 0) return;

  stats.totalMessages++;

  let parsed = null;
  try {
    parsed = JSON.parse(line);
  } catch (e) {
    log("RAW", line.slice(0, 80));
  }

  if (parsed) {
    const msgType = parsed.type || "unknown";
    if (parsed.source === "esp32_bno055" || parsed.source === "esp32_bno055_raw_estimate") {
      if (socket && socket._deviceType !== "esp32_bno055") {
        socket._deviceType = "esp32_bno055";
        broadcastJSON({
          type: "system",
          event: "esp32_connected",
          remote: socket.remoteAddress,
          rssi: parsed.rssi,
          ip: parsed.ip,
          ts: Date.now(),
        });
      }
    } else if (socket && !socket._deviceType && msgType !== "system") {
      socket._deviceType = "air778e";
      broadcastJSON({
        type: "system",
        event: "device_connected",
        remote: socket.remoteAddress || "wireless",
        ts: Date.now(),
      });
    }

    switch (msgType) {
      case "galloping":
        log("DATA", `舞动: state=${parsed.state} amp=${parsed.amp_dominant} freq=${parsed.dominant_freq}Hz`);
        break;
      case "env":
        log("DATA", `温湿度: ${parsed.temperature}C  ${parsed.humidity}%RH`);
        break;
      case "motor":
        log("DATA", `电机: ${parsed.motor_state}  pos=${parsed.position}%`);
        break;
      case "raw_accel":
        log("DATA", `加速度: ax=${parsed.ax} ay=${parsed.ay} az=${parsed.az}`);
        break;
      case "heartbeat":
        log("DATA", `心跳 #${stats.totalMessages}`);
        break;
      case "boot":
        log("DATA", `系统启动: ${parsed.msg}`);
        break;
      default:
        log("DATA", `${msgType}: ${line.slice(0, 60)}`);
    }
  }

  broadcastText(line);
}

const tcpServer = net.createServer((socket) => {
  socket.setNoDelay(true);
  socket._rtSparkBuffer = "";
  tcpClients.add(socket);
  stats.tcpConnected = 1;
  stats.tcpConnections = tcpClients.size;

  log("TCP", `Air778E/frp 已接入: ${socket.remoteAddress}:${socket.remotePort}  当前:${tcpClients.size}`);
  broadcastJSON({
    type: "system",
    event: "tcp_connected",
    host: socket.remoteAddress,
    port: socket.remotePort,
    mode: "wireless",
    ts: Date.now(),
  });
  broadcastJSON({
    type: "system",
    event: "device_connected",
    remote: "wireless",
    ts: Date.now(),
  });

  socket.on("data", (data) => {
    stats.bytesReceived += data.length;
    socket._rtSparkBuffer += data.toString("utf8");

    let idx;
    while ((idx = socket._rtSparkBuffer.indexOf("\n")) !== -1) {
      const line = socket._rtSparkBuffer.slice(0, idx).trim();
      socket._rtSparkBuffer = socket._rtSparkBuffer.slice(idx + 1);
      handleTcpLine(line, socket);
    }
  });

  socket.on("error", (err) => {
    log("TCP", `连接错误: ${err.message}`);
  });

  socket.on("close", () => {
    tcpClients.delete(socket);
    stats.tcpConnected = tcpClients.size > 0 ? 1 : 0;
    stats.tcpConnections = tcpClients.size;
    log("TCP", `Air778E/frp 连接断开  剩余:${tcpClients.size}`);

    if (socket._deviceType === "esp32_bno055" && countClientsByType("esp32_bno055") === 0) {
      broadcastJSON({
        type: "system",
        event: "esp32_disconnected",
        ts: Date.now(),
      });
    }

    if (tcpClients.size === 0 || (socket._deviceType === "air778e" && countClientsByType("air778e") === 0)) {
      broadcastJSON({
        type: "system",
        event: "tcp_disconnected",
        reason: "无线链路断开",
        ts: Date.now(),
      });
    }
  });
});

tcpServer.on("error", (err) => {
  if (err.code === "EADDRINUSE") {
    log("TCP", `端口 ${TCP_PORT} 被占用！请释放本地 TCP 端口或使用 --tcp-port=xxxx`);
  } else {
    log("TCP", `服务端错误: ${err.message}`);
  }
  process.exit(1);
});

tcpServer.listen(TCP_PORT, TCP_HOST, () => {
  log("TCP", `TCP Server 监听 ${TCP_HOST}:${TCP_PORT}  (等待 Air778E / Sakura Frp 连接)`);
});

// ---- 定时打印统计 ----
setInterval(() => {
  const uptime = Math.floor((Date.now() - stats.startTime) / 1000);
  const min = Math.floor(uptime / 60);
  const sec = uptime % 60;
  log("STAT", `运行 ${min}m${sec}s | TCP客户端:${tcpClients.size} | WS:${stats.wsConnections} | 消息:${stats.totalMessages} | 发送:${stats.bytesSent}B 接收:${stats.bytesReceived}B`);
}, 30000);

// ---- 优雅退出 ----
process.on("SIGINT", () => {
  log("SYS", "正在关闭...");
  for (const ws of wsClients) ws.close();
  for (const client of tcpClients) client.destroy();
  tcpServer.close();
  httpServer.close();
  setTimeout(() => process.exit(0), 500);
});

log("SYS", "==========================================");
log("SYS", "  RT_SPARK 纯无线桥接 已启动");
log("SYS", "  模式: 无线透传（无 COM 口）");
log("SYS", `  TCP:  本地监听        ${TCP_HOST}:${TCP_PORT}`);
log("SYS", `  WS:   本地网页        ws://localhost:${WS_PORT}`);
log("SYS", `  HTTP: 健康检查        http://localhost:${WS_PORT}/health`);
log("SYS", "  按 Ctrl+C 退出");
log("SYS", "==========================================");
