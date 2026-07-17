/*
 * bridge.js - RNDIS 双串口架构桥接服务（零依赖，纯 Node.js 原生实现）
 *
 * 修改说明：
 *   原方案等待 4G 模块通过 frp 转发连接本地 TCP 端口。
 *   新方案：直接读取 STM32 的 ST-Link VCP（COM9），解析 JSON 数据，
 *   通过 TCP 客户端连接到 frp 服务器，同时保持 WebSocket 服务器供本地调试。
 *
 * 数据流：
 *   STM32 (BNO055/SHT3X) → 控制台 UART1 → ST-Link VCP (COM9) →
 *   bridge.js (COM9 读取) → TCP 客户端 → frp-oil.com:32762 → 网页
 *
 * 用法：
 *   node bridge.js
 *   node bridge.js --com-port=\\.\COM9 --tcp-host=frp-oil.com --tcp-port=32762 --ws-port=8080
 *
 * 零依赖：不使用 npm 包，WebSocket 用 Node.js 内置 http + crypto 手写实现
 *         COM 口使用 serialport 库（npm install serialport）
 */

const fs = require("fs");
const net = require("net");
const http = require("http");
const crypto = require("crypto");
const { SerialPort } = require("serialport");

// ---- 参数解析 ----
const args = process.argv.slice(2);
function getArg(name, defaultVal) {
  const flag = `--${name}=`;
  const found = args.find((a) => a.startsWith(flag));
  return found ? found.slice(flag.length) : defaultVal;
}

const COM_PORT   = getArg("com-port", "COM9");
const TCP_HOST   = getArg("tcp-host", "frp-oil.com");
const TCP_PORT   = parseInt(getArg("tcp-port", "32762"), 10);
const WS_PORT    = parseInt(getArg("ws-port", "8080"), 10);

// ---- WS 客户端连接池 ----
const wsClients = new Set();

// ---- 活跃 TCP socket 集合 ----
const activeSockets = new Set();

// ---- TCP 客户端（连接到 frp 服务器）----
let tcpClient = null;
let tcpReconnectTimer = null;

// ---- 统计信息 ----
const stats = {
  tcpConnections: 0,
  wsConnections: 0,
  totalMessages: 0,
  bytesReceived: 0,
  comLines: 0,
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
  if (req.url === "/health") {
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify({
      status: "ok",
      tcpConnected: tcpClient && tcpClient.writable,
      comPort: COM_PORT,
      tcpConnections: stats.tcpConnections,
      wsConnections: stats.wsConnections,
      totalMessages: stats.totalMessages,
      comLines: stats.comLines,
      uptime: Math.floor((Date.now() - stats.startTime) / 1000)
    }));
    return;
  }
  res.writeHead(200, { "Content-Type": "text/plain; charset=utf-8" });
  res.end("RT_SPARK WebSocket Bridge. Connect via ws://localhost:" + WS_PORT);
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
    message: "RT_SPARK WebSocket Bridge 已连接",
    stats: {
      uptime: Math.floor((Date.now() - stats.startTime) / 1000),
      totalMessages: stats.totalMessages,
      bytesReceived: stats.bytesReceived,
      tcpConnected: tcpClient && tcpClient.writable,
    },
    ts: Date.now(),
  }));

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

        // 通过 COM 口转发给 STM32
        let sent = writeComData(cmd + "\n");

        if (sent) {
          ws.sendText(JSON.stringify({
            type: "system",
            event: "command_sent",
            command: cmd,
            ts: Date.now(),
          }));
        } else {
          log("CMD", `无法转发: COM 口未就绪`);
          ws.sendText(JSON.stringify({
            type: "system",
            event: "command_failed",
            reason: "COM 口未就绪",
            command: cmd,
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
// TCP 客户端 — 连接到 frp 服务器
// ================================================================
function connectTCP() {
  if (tcpClient) {
    tcpClient.destroy();
    tcpClient = null;
  }

  log("TCP", `正在连接 ${TCP_HOST}:${TCP_PORT}...`);

  tcpClient = net.createConnection({ host: TCP_HOST, port: TCP_PORT }, () => {
    log("TCP", `已连接到 ${TCP_HOST}:${TCP_PORT}`);
    stats.tcpConnections = 1;

    broadcastText(JSON.stringify({
      type: "system",
      event: "tcp_connected",
      host: TCP_HOST,
      port: TCP_PORT,
      ts: Date.now(),
    }));
  });

  tcpClient.on("data", (data) => {
    const text = data.toString("utf8");
    stats.bytesReceived += data.length;

    // frp 服务器返回的数据，如果包含命令，转发给 STM32
    let lines = text.split("\n");
    for (let line of lines) {
      line = line.trim();
      if (line.length === 0) continue;

        // 尝试解析为 JSON
        try {
          const parsed = JSON.parse(line);
          if (parsed.command) {
            log("TCP", `收到命令: ${parsed.command}`);
            // 转发给 STM32
            writeComData(parsed.command + "\n");
          }
        } catch (e) {
          // 非 JSON，忽略
        }
    }
  });

  tcpClient.on("error", (err) => {
    log("TCP", `连接错误: ${err.message}`);
  });

  tcpClient.on("close", () => {
    log("TCP", "连接断开，5秒后重连...");
    stats.tcpConnections = 0;
    tcpClient = null;

    broadcastText(JSON.stringify({
      type: "system",
      event: "tcp_disconnected",
      ts: Date.now(),
    }));

    if (!tcpReconnectTimer) {
      tcpReconnectTimer = setTimeout(() => {
        tcpReconnectTimer = null;
        connectTCP();
      }, 5000);
    }
  });
}

connectTCP();

// ================================================================
// COM 口读写 — 读取 STM32 通过 ST-Link VCP 输出的 JSON 数据
// ================================================================
let comPort = null;
let comBuffer = "";

function openComPort() {
  if (comPort) {
    try { comPort.close(); } catch (e) {}
    comPort = null;
  }

  log("COM", `正在打开 ${COM_PORT}...`);

  comPort = new SerialPort({
    path: COM_PORT,
    baudRate: 115200,
    autoOpen: false,
  });

  comPort.on('open', () => {
    log("COM", `${COM_PORT} 已打开 (baud=115200)`);
    // 通知所有 WebSocket 客户端：设备已接入（新架构：COM9 直连）
    const deviceMsg = JSON.stringify({
      type: "system",
      event: "device_connected",
      remote: "COM9-direct",
      ts: Date.now(),
    });
    broadcastText(deviceMsg);
  });

  comPort.on('data', (chunk) => {
    comBuffer += chunk.toString("utf8");
    stats.bytesReceived += chunk.length;

    let idx;
    while ((idx = comBuffer.indexOf("\n")) !== -1) {
      let line = comBuffer.slice(0, idx).trim();
      comBuffer = comBuffer.slice(idx + 1);
      if (line.length === 0) continue;
      stats.comLines++;

      let parsed = null;
      try {
        parsed = JSON.parse(line);
      } catch (e) {
        continue;
      }

      const msgType = parsed.type || "unknown";
      stats.totalMessages++;

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
        case "heartbeat":
          log("DATA", `心跳 #${stats.totalMessages}`);
          break;
        case "boot":
          log("DATA", `系统启动: ${parsed.msg}`);
          break;
        default:
          log("DATA", `${msgType}: ${line.slice(0, 60)}`);
      }

      if (tcpClient && tcpClient.writable) {
        try {
          tcpClient.write(line + "\n");
        } catch (e) {
          log("TCP", `发送失败: ${e.message}`);
        }
      }

      broadcastText(line);
    }
  });

  comPort.on('error', (err) => {
    log("COM", `错误: ${err.message}`);
    comPort = null;
    setTimeout(openComPort, 3000);
  });

  comPort.on('close', () => {
    log("COM", "串口关闭，3秒后重试...");
    comPort = null;
    setTimeout(openComPort, 3000);
  });

  comPort.open((err) => {
    if (err) {
      log("COM", `打开失败: ${err.message}`);
      comPort = null;
      setTimeout(openComPort, 3000);
    }
  });
}

function writeComData(data) {
  if (!comPort || !comPort.writable) return false;
  comPort.write(data, (err) => {
    if (err) log("COM", `写入错误: ${err.message}`);
  });
  return true;
}

openComPort();

// ---- 定时打印统计 ----
setInterval(() => {
  const uptime = Math.floor((Date.now() - stats.startTime) / 1000);
  const min = Math.floor(uptime / 60);
  const sec = uptime % 60;
  log("STAT", `运行 ${min}m${sec}s | TCP:${stats.tcpConnections ? 'OK' : 'DIS'} | WS:${stats.wsConnections} | 消息:${stats.totalMessages} | COM行:${stats.comLines}`);
}, 30000);

// ---- 优雅退出 ----
process.on("SIGINT", () => {
  log("SYS", "正在关闭...");
  for (const ws of wsClients) ws.close();
  if (tcpClient) tcpClient.destroy();
  if (comPort) { try { comPort.close(); } catch (e) {} comPort = null; }
  httpServer.close();
  setTimeout(() => process.exit(0), 500);
});

log("SYS", "==========================================");
log("SYS", "  RT_SPARK RNDIS Bridge 已启动");
log("SYS", `  COM:  STM32 数据输入  ${COM_PORT}`);
log("SYS", `  TCP:  frp 服务器      ${TCP_HOST}:${TCP_PORT}`);
log("SYS", `  WS:   本地调试        ws://localhost:${WS_PORT}`);
log("SYS", `  HTTP: 健康检查        http://localhost:${WS_PORT}/health`);
log("SYS", "  按 Ctrl+C 退出");
log("SYS", "==========================================");
