const { Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell,
        AlignmentType, HeadingLevel, BorderStyle, WidthType, ShadingType,
        Header, Footer, PageNumber, PageBreak } = require("docx");
const fs = require("fs");

const border = { style: BorderStyle.SINGLE, size: 1, color: "AAAAAA" };
const borders = { top: border, bottom: border, left: border, right: border };
const tableWidth = 10056; // A4 content width with 1-inch margins

function cell(text, width, opts = {}) {
  return new TableCell({
    borders,
    width: { size: width, type: WidthType.DXA },
    shading: opts.shading ? { fill: opts.shading, type: ShadingType.CLEAR } : undefined,
    margins: { top: 60, bottom: 60, left: 100, right: 100 },
    verticalAlign: "center",
    children: [new Paragraph({
      alignment: opts.center ? AlignmentType.CENTER : AlignmentType.LEFT,
      children: [new TextRun({
        text: text,
        bold: opts.bold,
        size: opts.size || 21,
        font: opts.font || "宋体"
      })]
    })]
  });
}

function p(text, opts = {}) {
  return new Paragraph({
    spacing: opts.before ? { before: opts.before } : undefined,
    children: [new TextRun({
      text: text,
      bold: opts.bold,
      size: opts.size || 21,
      font: "宋体"
    })]
  });
}

function monoP(text) {
  return new Paragraph({
    children: [new TextRun({ text: text, font: "Consolas", size: 18 })]
  });
}

const doc = new Document({
  styles: {
    default: { document: { run: { font: "宋体", size: 24 } } },
    paragraphStyles: [
      { id: "Heading1", name: "Heading 1", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 36, bold: true, font: "黑体", color: "1A3B5C" },
        paragraph: { spacing: { before: 400, after: 200 }, outlineLevel: 0 } },
      { id: "Heading2", name: "Heading 2", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 28, bold: true, font: "黑体", color: "2E5C8A" },
        paragraph: { spacing: { before: 300, after: 160 }, outlineLevel: 1 } },
      { id: "Heading3", name: "Heading 3", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 24, bold: true, font: "黑体", color: "404040" },
        paragraph: { spacing: { before: 200, after: 120 }, outlineLevel: 2 } },
    ]
  },
  sections: [{
    properties: {
      page: { size: { width: 11906, height: 16838 }, margin: { top: 1440, right: 1440, bottom: 1440, left: 1440 } }
    },
    headers: { default: new Header({ children: [p("RT_SPARK 电缆监测系统 — 交接文档", { size: 18 })] }) },
    footers: { default: new Footer({ children: [new Paragraph({ alignment: AlignmentType.CENTER, children: [new TextRun({ children: [PageNumber.CURRENT], size: 18 })] })] }) },
    children: [
      // 封面
      new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 600 }, children: [new TextRun({ text: "RT_SPARK 电缆监测系统", size: 52, bold: true, font: "黑体", color: "1A3B5C" })] }),
      new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 300 }, children: [new TextRun({ text: "交接文档", size: 40, bold: true, font: "黑体", color: "2E5C8A" })] }),
      new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 1200 }, children: [new TextRun({ text: "项目代号：RT_SPARK | 版本：v1.0 | 日期：2026-07-18", size: 22, color: "666666" })] }),
      new Paragraph({ alignment: AlignmentType.CENTER, children: [new TextRun({ text: "星火一号 — 电缆舞动在线监测与自动除雪装置", size: 24, font: "楷体" })] }),
      new Paragraph({ children: [new PageBreak()] }),

      // 1. 项目概述
      new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("一、项目概述")] }),
      new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("1.1 项目目的")] }),
      p("本项目为基于 STM32 微控制器的架空输电线路电缆在线监测装置，代号「星火一号」。主要实现以下功能："),
      p("• 实时监测电缆振动加速度（三轴），通过算法检测电缆舞动状态（静止/监测/警告/高危/结冰）"),
      p("• 实时监测环境温湿度，用于结冰风险评估"),
      p("• 通过 4G 无线模块（Air778E）将数据实时上传至云端监控网页，实现远程可视化"),
      p("• 通过网页远程控制电机（前进/后退/停止/加热），实现自动巡线除雪"),
      p("• 高危状态自动弹窗预警，并支持一键启动自动除雪"),
      new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("1.2 主要构成")] }),
      p("系统由三大模块组成："),
      p("1. 硬件端（STM32 主板）：负责传感器采集、数据处理、电机驱动、4G 通信"),
      p("2. 服务端（bridge.js）：负责 TCP 与 WebSocket 双向桥接，运行在电脑上"),
      p("3. 网页端（cable-monitor.html）：负责数据可视化、状态监控、远程控制"),
      new Paragraph({ children: [new PageBreak()] }),

      // 2. 硬件平台
      new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("二、硬件平台")] }),
      new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("2.1 主控板")] }),
      new Table({
        width: { size: tableWidth, type: WidthType.DXA },
        columnWidths: [2500, 7556],
        rows: [
          new TableRow({ children: [cell("型号", 2500, { bold: true, shading: "D5E8F0", center: true }), cell("RT-Thread Spark / STM32F407 系列", 7556)] }),
          new TableRow({ children: [cell("主频", 2500, { bold: true, shading: "D5E8F0", center: true }), cell("168 MHz (Cortex-M4)", 7556)] }),
          new TableRow({ children: [cell("Flash", 2500, { bold: true, shading: "D5E8F0", center: true }), cell("1 MB", 7556)] }),
          new TableRow({ children: [cell("RAM", 2500, { bold: true, shading: "D5E8F0", center: true }), cell("192 KB SRAM", 7556)] }),
          new TableRow({ children: [cell("操作系统", 2500, { bold: true, shading: "D5E8F0", center: true }), cell("RT-Thread 4.1.x", 7556)] }),
          new TableRow({ children: [cell("调试接口", 2500, { bold: true, shading: "D5E8F0", center: true }), cell("ST-Link V2（USB 虚拟串口 COM9）", 7556)] }),
        ]
      }),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("2.2 主要传感器")] }),
      new Table({
        width: { size: tableWidth, type: WidthType.DXA },
        columnWidths: [2200, 2700, 2500, 2656],
        rows: [
          new TableRow({ children: [
            cell("传感器", 2200, { bold: true, shading: "D5E8F0", center: true }),
            cell("型号", 2700, { bold: true, shading: "D5E8F0", center: true }),
            cell("接口", 2500, { bold: true, shading: "D5E8F0", center: true }),
            cell("主要参数", 2656, { bold: true, shading: "D5E8F0", center: true })
          ]}),
          new TableRow({ children: [
            cell("加速度/姿态", 2200), cell("BNO055", 2700), cell("I2C (UART1 备用)", 2500),
            cell("±16g，三轴 + 欧拉角，自带融合算法", 2656)
          ]}),
          new TableRow({ children: [
            cell("温湿度", 2200), cell("SHT3X (SHT30/31)", 2700), cell("I2C", 2500),
            cell("±0.3°C / ±2%RH，带 CRC 校验", 2656)
          ]}),
        ]
      }),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("2.3 通信模块")] }),
      new Table({
        width: { size: tableWidth, type: WidthType.DXA },
        columnWidths: [2500, 7556],
        rows: [
          new TableRow({ children: [cell("型号", 2500, { bold: true, shading: "D5E8F0", center: true }), cell("合宙 Air778E / 4G Cat.1 模块", 7556)] }),
          new TableRow({ children: [cell("制式", 2500, { bold: true, shading: "D5E8F0", center: true }), cell("4G LTE Cat.1，下行 10Mbps / 上行 5Mbps", 7556)] }),
          new TableRow({ children: [cell("接口", 2500, { bold: true, shading: "D5E8F0", center: true }), cell("UART（TTL 电平，3.3V），波特率 115200", 7556)] }),
          new TableRow({ children: [cell("SIM 卡", 2500, { bold: true, shading: "D5E8F0", center: true }), cell("中国移动 Nano-SIM（APN=CMNET）", 7556)] }),
          new TableRow({ children: [cell("透传模式", 2500, { bold: true, shading: "D5E8F0", center: true }), cell("AT+CIPMODE=1，上电自动连 frp 服务器", 7556)] }),
        ]
      }),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("2.4 执行机构")] }),
      new Table({
        width: { size: tableWidth, type: WidthType.DXA },
        columnWidths: [2500, 7556],
        rows: [
          new TableRow({ children: [cell("电机驱动", 2500, { bold: true, shading: "D5E8F0", center: true }), cell("双路直流电机（左右轮），通过 GPIO 引脚控制方向", 7556)] }),
          new TableRow({ children: [cell("加热片", 2500, { bold: true, shading: "D5E8F0", center: true }), cell("继电器控制，高电平触发（PA8）", 7556)] }),
          new TableRow({ children: [cell("继电器", 2500, { bold: true, shading: "D5E8F0", center: true }), cell("5V 高电平触发继电器模块，控制加热片电源", 7556)] }),
        ]
      }),
      new Paragraph({ children: [new PageBreak()] }),

      // 3. 串口信息
      new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("三、串口信息")] }),
      new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("3.1 串口分配表")] }),
      new Table({
        width: { size: tableWidth, type: WidthType.DXA },
        columnWidths: [1600, 2000, 2200, 1900, 2356],
        rows: [
          new TableRow({ children: [
            cell("串口", 1600, { bold: true, shading: "D5E8F0", center: true }),
            cell("引脚", 2000, { bold: true, shading: "D5E8F0", center: true }),
            cell("功能", 2200, { bold: true, shading: "D5E8F0", center: true }),
            cell("波特率", 1900, { bold: true, shading: "D5E8F0", center: true }),
            cell("备注", 2356, { bold: true, shading: "D5E8F0", center: true })
          ]}),
          new TableRow({ children: [
            cell("UART1", 1600), cell("TX=PA9, RX=PA10", 2000),
            cell("ST-Link VCP / 调试输出", 2200), cell("115200", 1900),
            cell("仅在开发调试时插电脑", 2356)
          ]}),
          new TableRow({ children: [
            cell("UART2", 1600), cell("TX=PA2, RX=PA3", 2000),
            cell("Air778E 4G 数据通信", 2200), cell("115200", 1900),
            cell("正式运行时的唯一通信口", 2356)
          ]}),
          new TableRow({ children: [
            cell("UART3", 1600), cell("TX=PB10, RX=PB11", 2000),
            cell("FinSH 控制台（可选）", 2200), cell("115200", 1900),
            cell("当前已关闭，留给 BNO055 使用", 2356)
          ]}),
        ]
      }),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("3.2 通信方式说明")] }),
      p("• 正式测试/运行时：Air778E 插在 UART2（PA2/PA3），通过 4G 网络无线传输，不插任何电脑。"),
      p("• 开发调试时：可用 ST-Link 虚拟串口（COM9）查看调试输出，但 bridge.js 不再读取 COM9。"),
      p("• Air778E 配置阶段：通过 USB 虚拟串口（COM3/COM4/COM5，具体见设备管理器）发送 AT 指令配置。"),
      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("3.3 Air778E 配置指令（一次性）")] }),
      monoP("AT+CFUN=1                          ; 开启射频"),
      monoP("AT+CGATT=1                         ; 附着到 4G 网络"),
      monoP("AT+CGDCONT=1,\"IP\",\"CMNET\"        ; 设置 APN（中国移动）"),
      monoP("AT+CGACT=1,1                       ; 激活 PDP 上下文"),
      monoP("AT+CIPSTART=\"TCP\",\"frp-oil.com\",\"32762\"  ; 连接 frp 服务器"),
      monoP("AT+CIPMODE=1                       ; 进入透传模式"),
      monoP("AT+CIPATS=1,10                     ; 掉线后 10 秒自动重连"),
      monoP("AT+SAVA                            ; 保存配置到 Flash"),
      p("配置完成后，拔掉 USB 线，Air778E 上电后自动连接 frp 服务器。"),
      new Paragraph({ children: [new PageBreak()] }),

      // 4. 硬件接口信息
      new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("四、硬件接口信息")] }),
      new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("4.1 GPIO 引脚分配表")] }),
      new Table({
        width: { size: tableWidth, type: WidthType.DXA },
        columnWidths: [1600, 2200, 2000, 2000, 2256],
        rows: [
          new TableRow({ children: [
            cell("功能", 1600, { bold: true, shading: "D5E8F0", center: true }),
            cell("引脚", 2200, { bold: true, shading: "D5E8F0", center: true }),
            cell("模式", 2000, { bold: true, shading: "D5E8F0", center: true }),
            cell("电平", 2000, { bold: true, shading: "D5E8F0", center: true }),
            cell("备注", 2256, { bold: true, shading: "D5E8F0", center: true })
          ]}),
          new TableRow({ children: [cell("左电机 AIN1", 1600), cell("PA0", 2200), cell("GPIO 输出", 2000), cell("HIGH/LOW", 2000), cell("左轮正转控制", 2256)] }),
          new TableRow({ children: [cell("左电机 AIN2", 1600), cell("PA1", 2200), cell("GPIO 输出", 2000), cell("HIGH/LOW", 2000), cell("左轮反转控制", 2256)] }),
          new TableRow({ children: [cell("右电机 BIN1", 1600), cell("PB15", 2200), cell("GPIO 输出", 2000), cell("HIGH/LOW", 2000), cell("右轮正转控制", 2256)] }),
          new TableRow({ children: [cell("右电机 BIN2", 1600), cell("PB14", 2200), cell("GPIO 输出", 2000), cell("HIGH/LOW", 2000), cell("右轮反转控制", 2256)] }),
          new TableRow({ children: [cell("加热继电器", 1600), cell("PA8", 2200), cell("GPIO 输出", 2000), cell("HIGH=触发", 2000), cell("高电平触发继电器", 2256)] }),
          new TableRow({ children: [cell("I2C SCL", 1600), cell("PD10", 2200), cell("开漏输出", 2000), cell("3.3V", 2000), cell("BNO055 + SHT3X 共享", 2256)] }),
          new TableRow({ children: [cell("I2C SDA", 1600), cell("PD8", 2200), cell("开漏输出", 2000), cell("3.3V", 2000), cell("BNO055 + SHT3X 共享", 2256)] }),
          new TableRow({ children: [cell("UART2 TX", 1600), cell("PA2", 2200), cell("推挽输出", 2000), cell("3.3V TTL", 2000), cell("接 Air778E RX", 2256)] }),
          new TableRow({ children: [cell("UART2 RX", 1600), cell("PA3", 2200), cell("输入浮空", 2000), cell("3.3V TTL", 2000), cell("接 Air778E TX", 2256)] }),
        ]
      }),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("4.2 电机真值表")] }),
      new Table({
        width: { size: tableWidth, type: WidthType.DXA },
        columnWidths: [2000, 2000, 2000, 2000, 2000, 1956],
        rows: [
          new TableRow({ children: [
            cell("动作", 2000, { bold: true, shading: "D5E8F0", center: true }),
            cell("AIN1", 2000, { bold: true, shading: "D5E8F0", center: true }),
            cell("AIN2", 2000, { bold: true, shading: "D5E8F0", center: true }),
            cell("BIN1", 2000, { bold: true, shading: "D5E8F0", center: true }),
            cell("BIN2", 2000, { bold: true, shading: "D5E8F0", center: true }),
            cell("效果", 1956, { bold: true, shading: "D5E8F0", center: true })
          ]}),
          new TableRow({ children: [cell("前进", 2000), cell("HIGH", 2000), cell("LOW", 2000), cell("HIGH", 2000), cell("LOW", 2000), cell("左右轮正转", 1956)] }),
          new TableRow({ children: [cell("后退", 2000), cell("LOW", 2000), cell("HIGH", 2000), cell("LOW", 2000), cell("HIGH", 2000), cell("左右轮反转", 1956)] }),
          new TableRow({ children: [cell("停止", 2000), cell("LOW", 2000), cell("LOW", 2000), cell("LOW", 2000), cell("LOW", 2000), cell("制动", 1956)] }),
        ]
      }),
      p("注意：电机使用 GPIO 直接驱动，没有 PWM 调速。当前为全速（100%）或停止（0%）两种状态。"),
      new Paragraph({ children: [new PageBreak()] }),

      // 5. 网页信息
      new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("五、网页信息与数据流")] }),
      new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("5.1 网页文件位置")] }),
      monoP("D:\\AI_Intro\\RT-ThreadStudio\\workspace\\RT_SPARK\\tools\\wireless_bridge\\cable-monitor.html"),
      monoP("D:\\AI_Intro\\RT-ThreadStudio\\workspace\\RT_SPARK\\tools\\wireless_bridge\\bridge.js"),
      p("打开方式：直接双击 cable-monitor.html 在浏览器中打开，无需服务器。bridge.js 用 Node.js 运行。"),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("5.2 数据流（上行：STM32 → 网页）")] }),
      monoP("STM32 传感器 → UART2 → Air778E → 4G → frp-oil.com:32762 → bridge.js(TCP) → WebSocket → 网页"),
      p("数据以 JSON 格式每行一条发送，以 \\n 结尾。主要消息类型："),
      new Table({
        width: { size: tableWidth, type: WidthType.DXA },
        columnWidths: [1800, 8256],
        rows: [
          new TableRow({ children: [cell("type", 1800, { bold: true, shading: "D5E8F0", center: true }), cell("说明", 8256, { bold: true, shading: "D5E8F0", center: true })] }),
          new TableRow({ children: [cell("galloping", 1800), cell("舞动检测数据，包含状态、振幅、频率、位移等", 8256)] }),
          new TableRow({ children: [cell("raw_accel", 1800), cell("原始加速度（ax, ay, az），用于实时波形绘制", 8256)] }),
          new TableRow({ children: [cell("env", 1800), cell("温湿度数据（temperature, humidity）", 8256)] }),
          new TableRow({ children: [cell("motor", 1800), cell("电机状态反馈（motor_state, position）", 8256)] }),
          new TableRow({ children: [cell("heartbeat", 1800), cell("心跳包，每 10 秒发送一次", 8256)] }),
          new TableRow({ children: [cell("boot", 1800), cell("系统启动消息，上电时发送一次", 8256)] }),
        ]
      }),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("5.3 命令流（下行：网页 → STM32）")] }),
      monoP("网页 → WebSocket → bridge.js → TCP → frp-oil.com:32762 → 4G → Air778E → UART2 → STM32"),
      p("命令为纯文本，以 \\n 结尾。bridge.js 不再读取任何 COM 口，所有命令通过 TCP 无线发送。"),
      new Table({
        width: { size: tableWidth, type: WidthType.DXA },
        columnWidths: [2000, 7556],
        rows: [
          new TableRow({ children: [cell("命令", 2000, { bold: true, shading: "D5E8F0", center: true }), cell("效果", 7556, { bold: true, shading: "D5E8F0", center: true })] }),
          new TableRow({ children: [cell("forward", 2000), cell("电机前进，6 秒后自动停止（RT-Thread 定时器）", 7556)] }),
          new TableRow({ children: [cell("backward", 2000), cell("电机后退，6 秒后自动停止", 7556)] }),
          new TableRow({ children: [cell("stop", 2000), cell("立即停止电机", 7556)] }),
          new TableRow({ children: [cell("heater_on", 2000), cell("继电器吸合，加热片通电（PA8=HIGH）", 7556)] }),
          new TableRow({ children: [cell("heater_off", 2000), cell("继电器断开，加热片断电（PA8=LOW）", 7556)] }),
          new TableRow({ children: [cell("reset_detector", 2000), cell("重置舞动检测器状态为 IDLE", 7556)] }),
        ]
      }),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("5.4 bridge.js 启动方式")] }),
      monoP("cd D:\\AI_Intro\\RT-ThreadStudio\\workspace\\RT_SPARK\\tools\\wireless_bridge"),
      monoP("node bridge.js"),
      p("默认参数：TCP 服务器=frp-oil.com:32762，WebSocket=ws://localhost:8080。"),
      p("可选参数：--tcp-host=xxx --tcp-port=xxx --ws-port=xxx"),
      p("健康检查：http://localhost:8080/health"),
      new Paragraph({ children: [new PageBreak()] }),

      // 6. 代码结构
      new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("六、代码结构与文件职责")] }),
      new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("6.1 固件端（applications/ 目录）")] }),
      new Table({
        width: { size: tableWidth, type: WidthType.DXA },
        columnWidths: [3000, 6956],
        rows: [
          new TableRow({ children: [cell("文件名", 3000, { bold: true, shading: "D5E8F0", center: true }), cell("职责说明", 6956, { bold: true, shading: "D5E8F0", center: true })] }),
          new TableRow({ children: [cell("main.c", 3000), cell("主入口、电机控制（GPIO）、加热继电器、电机定时器（6 秒自动停止）、UART2 命令接收线程初始化", 6956)] }),
          new TableRow({ children: [cell("dtu_sender.c/.h", 3000), cell("4G 数据发送模块。优先通过 UART2 发送 JSON，回退到控制台（调试时）。提供 galloping/env/motor/heartbeat/boot 等发送函数", 6956)] }),
          new TableRow({ children: [cell("uart_recv.c", 3000), cell("UART2 接收驱动（PA2/PA3）。接收来自 Air778E 的文本命令，解析并执行（forward/backward/stop/heater_on/heater_off/reset_detector）", 6956)] }),
          new TableRow({ children: [cell("galloping_detect.c/.h", 3000), cell("舞动检测算法。包括滑动窗口去直流、中值滤波、低通滤波、9 维特征提取、五级状态机、阈值判断", 6956)] }),
          new TableRow({ children: [cell("gy_bn0055.c/.h", 3000), cell("BNO055 九轴传感器驱动。通过 I2C（软件 I2C，SCL=PD10, SDA=PD8）读取三轴加速度、欧拉角。采样周期 50ms", 6956)] }),
          new TableRow({ children: [cell("sht3x.c/.h", 3000), cell("SHT3X 温湿度传感器驱动。通过 I2C 读取温度、湿度，带 CRC 校验。采样周期 5 秒", 6956)] }),
        ]
      }),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("6.2 服务端与网页端")] }),
      new Table({
        width: { size: tableWidth, type: WidthType.DXA },
        columnWidths: [3000, 6956],
        rows: [
          new TableRow({ children: [cell("文件名", 3000, { bold: true, shading: "D5E8F0", center: true }), cell("职责说明", 6956, { bold: true, shading: "D5E8F0", center: true })] }),
          new TableRow({ children: [cell("bridge.js", 3000), cell("纯无线桥接服务。维护 TCP 连接（frp 服务器）和 WebSocket 服务器。上行：TCP 数据广播给 WebSocket 客户端；下行：WebSocket 命令通过 TCP 发送。零 COM 口依赖", 6956)] }),
          new TableRow({ children: [cell("cable-monitor.html", 3000), cell("监控网页。WebSocket 通信、Canvas 实时波形、状态监控面板、电机控制、自动巡线除雪、高危弹窗、日志记录", 6956)] }),
        ]
      }),
      new Paragraph({ children: [new PageBreak()] }),

      // 7. 代码逻辑与算法
      new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("七、核心代码逻辑与算法")] }),
      new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("7.1 主线程流程（main.c）")] }),
      p("1. 初始化：引脚配置（GPIO、I2C、UART2）、电机引脚初始化、加热继电器初始化"),
      p("2. 传感器初始化：BNO055 自检、SHT3X 自检"),
      p("3. 启动任务：传感器采集线程（20Hz）、环境采集线程（0.2Hz）、舞动检测线程（20Hz）、4G 发送线程（0.2Hz）、UART2 接收线程"),
      p("4. 主循环：周期性发送心跳包"),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("7.2 舞动检测算法（galloping_detect.c）")] }),
      new Paragraph({ heading: HeadingLevel.HEADING_3, children: [new TextRun("7.2.1 预处理流水线")] }),
      p("输入：原始加速度 ax_raw, ay_raw, az_raw（单位 m/s²）"),
      p("Step 1：去直流（滑动窗口均值）"),
      monoP("static_mean = Σ(x_i) / N，N=50（2.5 秒窗口）"),
      monoP("filtered = x - static_mean"),
      p("Step 2：中值滤波（5 点窗口）"),
      p("Step 3：低通滤波（α=0.3，截止频率约 3Hz）"),
      p("Step 4：输出 clean_ax, clean_ay, clean_az（用于特征提取）"),

      new Paragraph({ heading: HeadingLevel.HEADING_3, spacing: { before: 200 }, children: [new TextRun("7.2.2 特征提取")] }),
      p("基于预处理后数据，每 1 秒提取一次特征（50 个样本窗口）："),
      new Table({
        width: { size: tableWidth, type: WidthType.DXA },
        columnWidths: [2500, 3500, 3956],
        rows: [
          new TableRow({ children: [
            cell("特征名", 2500, { bold: true, shading: "D5E8F0", center: true }),
            cell("计算方式", 3500, { bold: true, shading: "D5E8F0", center: true }),
            cell("物理意义", 3956, { bold: true, shading: "D5E8F0", center: true })
          ]}),
          new TableRow({ children: [cell("amp_dominant", 2500), cell("max(amp_x, amp_y, amp_z)", 3500), cell("三轴中的最大振幅", 3956)] }),
          new TableRow({ children: [cell("amp_x/y/z_pp", 2500), cell("max - min（峰峰值）", 3500), cell("各轴峰峰值振幅", 3956)] }),
          new TableRow({ children: [cell("rms_accel", 2500), cell("√(Σ(x²) / N)", 3500), cell("有效加速度（RMS）", 3956)] }),
          new TableRow({ children: [cell("vibr_energy", 2500), cell("Σ(x²)", 3500), cell("振动能量", 3956)] }),
          new TableRow({ children: [cell("zero_cross_rate", 2500), cell("过零次数 / 样本数", 3500), cell("过零率（频率特征）", 3956)] }),
          new TableRow({ children: [cell("dominant_freq", 2500), cell("过零法估算主导频率", 3500), cell("主导振动频率", 3956)] }),
          new TableRow({ children: [cell("displacement_est", 2500), cell("二重积分", 3500), cell("位移估算（单位 m）", 3956)] }),
          new TableRow({ children: [cell("torsion_deg", 2500), cell("atan2(ay, ax)", 3500), cell("电缆扭转角度", 3956)] }),
          new TableRow({ children: [cell("confidence", 2500), cell("多特征加权归一化", 3500), cell("状态判断置信度", 3956)] }),
        ]
      }),

      new Paragraph({ heading: HeadingLevel.HEADING_3, spacing: { before: 200 }, children: [new TextRun("7.2.3 状态机")] }),
      p("状态转换基于综合阈值判断，采用滞回设计防止抖动："),
      p("IDLE（静止）→ MONITOR（监测）：当 amp_dominant > AMP_IDLE（0.02g）"),
      p("MONITOR → WARNING（警告）：当 amp_dominant > AMP_WARNING（0.08g）或频率超出 0.1~5Hz"),
      p("WARNING → SEVERE（高危）：当 amp_dominant > AMP_SEVERE（0.15g）"),
      p("任意状态 → ICE（结冰）：当温度 < 0°C 且湿度 > 80% 且低频振动"),
      p("降级条件比升级更严格，防止状态频繁切换。"),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("7.3 电机定时控制（main.c）")] }),
      p("电机命令（forward/backward）触发后，自动启动 6 秒定时器："),
      monoP("start_motor_with_timeout(dir):"),
      monoP("  1. 删除旧定时器（rt_timer_delete）"),
      monoP("  2. 启动电机（dir=1 前进，dir=-1 后退）"),
      monoP("  3. 创建 6 秒单次定时器（RT_TIMER_FLAG_ONE_SHOT）"),
      monoP("  4. 定时器到期 → motor_timeout() → stop() → 发送 motor=stop JSON"),
      p("安全机制：重复触发会重置定时器，不会累加。停止命令（stop）直接删除定时器。"),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("7.4 自动巡线除雪（网页端）")] }),
      p("三阶段状态机，通过网页 position 变量模拟："),
      p("Phase 0：中间(50%) → 右端(100%)，前进。到达 100% 后自动进入 Phase 1。"),
      p("Phase 1：右端(100%) → 左端(0%)，后退。到达 0% 后自动进入 Phase 2。"),
      p("Phase 2：左端(0%) → 中间(50%)，前进。到达 50% 后自动停止，显示完成。"),
      p("注意：当前 position 为网页端模拟值，实际可用编码器反馈校准。"),
      new Paragraph({ children: [new PageBreak()] }),

      // 8. JSON 格式规范
      new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("八、JSON 通信格式规范")] }),
      new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("8.1 上行数据（STM32 → 网页）")] }),
      p("每条 JSON 以 \\n 结尾，每行一条。浮点数采用定点转换（3~4 位小数），无 %f 格式。"),
      p("galloping 示例："),
      monoP('{"type":"galloping","ts":12345,"state":"SEVERE","amp_dominant":0.234,"amp_x_pp":0.123,"amp_y_pp":0.200,"amp_z_pp":0.050,"displacement_est":0.0123,"dominant_freq":1.25,"zero_cross_rate":2.5,"rms_accel":0.150,"vibr_energy":0.0225,"torsion_deg":15.30,"confidence":0.85}'),
      p("raw_accel 示例："),
      monoP('{"type":"raw_accel","ts":12345,"ax":0.123,"ay":0.456,"az":9.789}'),
      p("env 示例："),
      monoP('{"type":"env","ts":12345,"temperature":-5.2,"humidity":85.3}'),
      p("motor 示例："),
      monoP('{"type":"motor","ts":12345,"motor_state":"forward","position":67}'),
      p("heartbeat 示例："),
      monoP('{"type":"heartbeat","ts":12345}'),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("8.2 下行命令（网页 → STM32）")] }),
      p("纯文本命令，以 \\n 结尾：forward, backward, stop, heater_on, heater_off, reset_detector"),
      new Paragraph({ children: [new PageBreak()] }),

      // 9. 部署与运维
      new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("九、部署与运维指南")] }),
      new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("9.1 首次部署步骤")] }),
      p("1. 配置 Air778E（通过 USB 串口助手，COM3/4/5，115200）："),
      monoP("AT+CFUN=1"),
      monoP("AT+CGATT=1"),
      monoP("AT+CGDCONT=1,\"IP\",\"CMNET\""),
      monoP("AT+CGACT=1,1"),
      monoP("AT+CIPSTART=\"TCP\",\"frp-oil.com\",\"32762\""),
      monoP("AT+CIPMODE=1"),
      monoP("AT+CIPATS=1,10"),
      monoP("AT+SAVA"),
      p("2. 拔掉 Air778E USB 线，接到 STM32 UART2（PA2/PA3）"),
      p("3. 编译烧录 STM32（RT-ThreadStudio → Build → 下载）"),
      p("4. 启动 bridge.js：node bridge.js"),
      p("5. 打开 cable-monitor.html"),
      p("6. 给 STM32 上电，Air778E 自动连接 frp，网页显示数据"),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("9.2 日常运维检查清单")] }),
      p("• 检查 TCP 连接状态：bridge.js 日志是否显示 [TCP] 已连接"),
      p("• 检查心跳：网页是否每 10 秒收到 heartbeat 消息"),
      p("• 检查 Air778E 信号：AT+CSQ 返回信号强度（0~31，>15 正常）"),
      p("• 检查电机状态：发送 forward 命令后，bridge.js 是否收到 motor 状态反馈"),
      p("• 检查加热状态：PA8 输出电平是否与 heater_on/off 命令一致"),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 300 }, children: [new TextRun("9.3 常见问题排查")] }),
      new Table({
        width: { size: tableWidth, type: WidthType.DXA },
        columnWidths: [2500, 7556],
        rows: [
          new TableRow({ children: [cell("问题", 2500, { bold: true, shading: "D5E8F0", center: true }), cell("排查方法", 7556, { bold: true, shading: "D5E8F0", center: true })] }),
          new TableRow({ children: [cell("TCP 未连接", 2500), cell("检查 Air778E 信号（AT+CSQ）、SIM 卡余额、frp 服务器是否在线", 7556)] }),
          new TableRow({ children: [cell("无数据", 2500), cell("检查 UART2 接线（PA2/PA3 是否反接）、Air778E 波特率是否为 115200", 7556)] }),
          new TableRow({ children: [cell("命令无响应", 2500), cell("检查 uart_recv.c 是否已编译进固件、UART2 中断是否正常触发", 7556)] }),
          new TableRow({ children: [cell("波形异常", 2500), cell("检查 BNO055 数据转换系数（raw * 0.001 * 9.80665）、I2C 总线是否冲突", 7556)] }),
          new TableRow({ children: [cell("网页显示离线", 2500), cell("检查 bridge.js 是否运行、WebSocket 端口是否被占用（8080）", 7556)] }),
        ]
      }),
      new Paragraph({ children: [new PageBreak()] }),

      // 10. 修改记录
      new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("十、修改记录")] }),
      new Table({
        width: { size: tableWidth, type: WidthType.DXA },
        columnWidths: [1400, 1800, 2000, 4756],
        rows: [
          new TableRow({ children: [
            cell("日期", 1400, { bold: true, shading: "D5E8F0", center: true }),
            cell("修改人", 1800, { bold: true, shading: "D5E8F0", center: true }),
            cell("修改内容", 2000, { bold: true, shading: "D5E8F0", center: true }),
            cell("详细说明", 4756, { bold: true, shading: "D5E8F0", center: true })
          ]}),
          new TableRow({ children: [cell("2026-07-18", 1400), cell("CodeBuddy", 1800), cell("bridge.js 彻底移除 COM 口", 2000), cell("bridge.js 不再依赖任何 SerialPort，纯 TCP↔WebSocket 桥接", 4756)] }),
          new TableRow({ children: [cell("2026-07-18", 1400), cell("CodeBuddy", 1800), cell("main.c 去 static", 2000), cell("heater_on、heater_off、stop 去掉 static，供 uart_recv.c 外部引用", 4756)] }),
          new TableRow({ children: [cell("2026-07-18", 1400), cell("CodeBuddy", 1800), cell("dtu_sender 注释更新", 2000), cell("注释从 COM9 输出改为 UART2→Air778E 无线输出", 4756)] }),
          new TableRow({ children: [cell("2026-07-18", 1400), cell("CodeBuddy", 1800), cell("cable-monitor.html 状态更新", 2000), cell("命令发送状态统一显示 [无线]，移除 [有线] fallback", 4756)] }),
        ]
      }),
      new Paragraph({ spacing: { before: 400 }, alignment: AlignmentType.CENTER, children: [new TextRun({ text: "—— 文档结束 ——", size: 22, color: "888888" })] }),
    ]
  }]
});

Packer.toBuffer(doc).then(buffer => {
  fs.writeFileSync("RT_SPARK_交接文档.docx", buffer);
  console.log("交接文档已生成: RT_SPARK_交接文档.docx");
});
