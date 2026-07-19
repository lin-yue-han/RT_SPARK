const { SerialPort } = require("serialport");

const portsToTest = ["COM3", "COM4", "COM5"];

async function testPort(path) {
  return new Promise((resolve) => {
    try {
      const port = new SerialPort({ path, baudRate: 115200, autoOpen: false });
      let buffer = "";
      let timer;

      port.on("open", () => {
        port.write("AT\r\n");
        timer = setTimeout(() => {
          port.close();
          resolve({ path, ok: buffer.includes("OK"), resp: buffer.trim() });
        }, 800);
      });

      port.on("data", (chunk) => {
        buffer += chunk.toString("utf8");
      });

      port.on("error", (err) => {
        resolve({ path, ok: false, resp: err.message });
      });

      port.open((err) => {
        if (err) {
          resolve({ path, ok: false, resp: err.message });
        }
      });
    } catch (e) {
      resolve({ path, ok: false, resp: e.message });
    }
  });
}

async function main() {
  console.log("正在探测 Air778E AT 指令口...\n");
  for (const p of portsToTest) {
    const result = await testPort(p);
    if (result.ok) {
      console.log(`[成功] ${result.path} 是 AT 指令口！响应: ${result.resp}`);
      console.log("\n请用串口助手打开 " + result.path + "，然后发送以下指令：");
      console.log("=".repeat(50));
      console.log("AT+CFUN=1");
      console.log("AT+CGATT=1");
      console.log('AT+CGDCONT=1,"IP","CMNET"');
      console.log("AT+CGACT=1,1");
      console.log('AT+CIPSTART="TCP","frp-oil.com","32762"');
      console.log("AT+CIPMODE=1");
      console.log("AT+CIPATS=1,10");
      console.log("AT+SAVA");
      console.log("=".repeat(50));
      process.exit(0);
    } else {
      console.log(`[失败] ${result.path}: ${result.resp || "无响应"}`);
    }
  }
  console.log("\n未找到 AT 口，请手动尝试各端口。");
}

main();
