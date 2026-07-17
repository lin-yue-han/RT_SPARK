/*
 * bridge.js - TCP -> WebSocket 桥接服务（零依赖，纯 Node.js 原生实现）
 *
 * 功能：
 *   1. 启动 TCP Server (端口 8090)，接收 Air778E 4G 模块经 frp 转发来的 JSON 行数据
 *   2. 启动 WebSocket Server (端口 8080)，供网页连接
 *   3. TCP 收到的数据实时推送到所有 WS 客户端
 *   4. WS 客户端可发送命令，桥接脚本通过 TCP 转发给 STM32 (双向通信)
 *
 * 数据流：
 *   STM32 --UART2--> Air778E --4G--> frp公网节点 --转发--> 本机TCP:8090 --bridge--> WS:8080 --> 网页
 *
 * 用法：
 *   node bridge.js
 *   node bridge.js --tcp-port=9090 --ws-port=8080
 *
 * 零依赖：不使用 npm 包，WebSocket 用 Node.js 内置 http + crypto 手写实现
 */

const net = require("net");
const http = require("http");
const crypto = require("crypto");

// ---- 参数解析 ----
const args = process.argv.slice(2);
function getArg(name, defaultVal) {
  const flag = `--${name}=`;
  const found = args.find((a) => a.startsWith(flag));
  return found ? found.slice(flag.length) : defaultVal;
}

const TCP_PORT = parseInt(getArg("tcp-port", "8090"), 10);
const WS_PORT  = parseInt(getArg("ws-port", "8080"), 10);

// ---- WS 客户端连接池 ----
const wsClients = new Set();

// ---- 活跃 TCP socket 集合 ----
const activeSockets = new Set();

// ---- 统计信息 ----
const stats = {
  tcpConnections: 0,
  wsConnections: 0,
  totalMessages: 0,
  bytesReceived: 0,
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

// 对单个连接封装帧收发
function createWSConnection(socket) {
  const ws = {
    socket: socket,
    alive: true,
    sendText: function(text) {
      if (!this.alive) return;
      const data = Buffer.from(text, "utf8");
      const mask = data.length <= 125 ? 2 : (data.length <= 65535 ? 4 : 10);
      const frame = Buffer.allocUnsafe(mask + data.length);
      frame[0] = 0x81; // FIN + opcode 1 (text)
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

// 解析 WebSocket 帧（处理分片和掩码）
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
  // 健康检查端点
  if (req.url === "/health") {
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify({
      status: "ok",
      tcpConnections: stats.tcpConnections,
      wsConnections: stats.wsConnections,
      totalMessages: stats.totalMessages,
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

  // 发送欢迎信息
  ws.sendText(JSON.stringify({
    type: "system",
    event: "ws_connected",
    message: "RT_SPARK WebSocket Bridge 已连接",
    stats: {
      uptime: Math.floor((Date.now() - stats.startTime) / 1000),
      totalMessages: stats.totalMessages,
      bytesReceived: stats.bytesReceived,
      tcpConnected: stats.tcpConnections > 0,
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

      // opcode 8 = close
      if (result.opcode === 8) {
        ws.alive = false;
        return;
      }

      // opcode 9 = ping, reply pong
      if (result.opcode === 9) {
        const pong = Buffer.allocUnsafe(2);
        pong[0] = 0x8a; // FIN + pong
        pong[1] = 0;
        try { socket.write(pong); } catch(e) {}
        continue;
      }

      // opcode 1 = text message
      if (result.opcode === 1 && result.payload) {
        const cmd = result.payload.trim();
        if (cmd.length === 0) continue;
        log("CMD", `网页->STM32: ${cmd}`);

        let sent = false;
        for (const sock of activeSockets) {
          if (sock.writable) {
            sock.write(cmd + "\n");
            sent = true;
            break;
          }
        }

        if (sent) {
          ws.sendText(JSON.stringify({
            type: "system",
            event: "command_sent",
            command: cmd,
            ts: Date.now(),
          }));
        } else {
          log("CMD", `无法转发: 没有活跃的 Air778E 连接`);
          ws.sendText(JSON.stringify({
            type: "system",
            event: "command_failed",
            reason: "Air778E 未连接",
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
// TCP Server — 接收 Air778E / frp 转发的数据
// ================================================================
const tcpServer = net.createServer((socket) => {
  stats.tcpConnections++;
  activeSockets.add(socket);
  const remoteAddr = `${socket.remoteAddress}:${socket.remotePort}`;
  log("TCP", `Air778E 已连接 (${remoteAddr})  当前连接数: ${stats.tcpConnections}`);

  broadcastText(JSON.stringify({
    type: "system",
    event: "device_connected",
    remote: remoteAddr,
    ts: Date.now(),
  }));

  let tcpBuffer = "";

  socket.on("data", (chunk) => {
    const text = chunk.toString("utf8");
    stats.bytesReceived += chunk.length;
    tcpBuffer += text;

    let idx;
    while ((idx = tcpBuffer.indexOf("\n")) !== -1) {
      let line = tcpBuffer.slice(0, idx).trim();
      tcpBuffer = tcpBuffer.slice(idx + 1);
      if (line.length === 0) continue;
      stats.totalMessages++;

      let parsed = null;
      try {
        parsed = JSON.parse(line);
      } catch (e) {
        log("TCP", `非JSON: ${line.slice(0, 80)}`);
        broadcastText(JSON.stringify({ type: "raw", data: line, ts: Date.now() }));
        continue;
      }

      const msgType = parsed.type || "unknown";
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
        default:
          log("DATA", `${msgType}: ${line.slice(0, 60)}`);
      }

      broadcastText(line); // 原始 JSON 行转发，网页端解析
    }
  });

  socket.on("error", (err) => {
    log("TCP", `连接错误: ${err.message}`);
  });

  socket.on("close", () => {
    stats.tcpConnections--;
    activeSockets.delete(socket);
    log("TCP", `Air778E 断开  剩余连接: ${stats.tcpConnections}`);
    broadcastText(JSON.stringify({
      type: "system",
      event: "device_disconnected",
      ts: Date.now(),
    }));
  });
});

tcpServer.on("error", (err) => {
  if (err.code === "EADDRINUSE") {
    log("TCP", `端口 ${TCP_PORT} 被占用！请用 --tcp-port=xxxx 指定其他端口`);
  } else {
    log("TCP", `服务器错误: ${err.message}`);
  }
  process.exit(1);
});

tcpServer.listen(TCP_PORT, "0.0.0.0", () => {
  log("TCP", `TCP Server 监听 0.0.0.0:${TCP_PORT}  (等待 Air778E / frp 连接)`);
});

// ---- 定时打印统计 ----
setInterval(() => {
  const uptime = Math.floor((Date.now() - stats.startTime) / 1000);
  const min = Math.floor(uptime / 60);
  const sec = uptime % 60;
  log("STAT", `运行 ${min}m${sec}s | TCP连接:${stats.tcpConnections} | WS网页:${stats.wsConnections} | 消息:${stats.totalMessages} | 字节:${stats.bytesReceived}`);
}, 30000);

// ---- 优雅退出 ----
process.on("SIGINT", () => {
  log("SYS", "正在关闭...");
  for (const ws of wsClients) ws.close();
  for (const sock of activeSockets) sock.destroy();
  tcpServer.close();
  httpServer.close();
  setTimeout(() => process.exit(0), 500);
});

log("SYS", "==========================================");
log("SYS", "  RT_SPARK Wireless Bridge 已启动");
log("SYS", `  TCP:  等待 Air778E 连接  0.0.0.0:${TCP_PORT}`);
log("SYS", `  WS:   网页连接  ws://localhost:${WS_PORT}`);
log("SYS", `  HTTP: 健康检查  http://localhost:${WS_PORT}/health`);
log("SYS", "  按 Ctrl+C 退出");
log("SYS", "==========================================");
