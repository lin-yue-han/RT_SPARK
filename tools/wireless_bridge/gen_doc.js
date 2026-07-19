const { Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell,
        AlignmentType, HeadingLevel, BorderStyle, WidthType, ShadingType,
        Header, Footer, PageNumber } = require("docx");
const fs = require("fs");

const border = { style: BorderStyle.SINGLE, size: 1, color: "CCCCCC" };
const borders = { top: border, bottom: border, left: border, right: border };

function cell(text, width, opts = {}) {
  return new TableCell({
    borders,
    width: { size: width, type: WidthType.DXA },
    shading: opts.shading ? { fill: opts.shading, type: ShadingType.CLEAR } : undefined,
    margins: { top: 60, bottom: 60, left: 100, right: 100 },
    children: [new Paragraph({
      alignment: opts.center ? AlignmentType.CENTER : AlignmentType.LEFT,
      children: [new TextRun({ text: text, bold: opts.bold, size: opts.size || 21 })]
    })]
  });
}

const doc = new Document({
  styles: {
    default: { document: { run: { font: "宋体", size: 24 } } },
    paragraphStyles: [
      { id: "Heading1", name: "Heading 1", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 36, bold: true, font: "黑体", color: "1F4E79" },
        paragraph: { spacing: { before: 360, after: 200 }, outlineLevel: 0 } },
      { id: "Heading2", name: "Heading 2", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 28, bold: true, font: "黑体", color: "2E75B6" },
        paragraph: { spacing: { before: 240, after: 160 }, outlineLevel: 1 } },
      { id: "Heading3", name: "Heading 3", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 24, bold: true, font: "黑体", color: "404040" },
        paragraph: { spacing: { before: 180, after: 120 }, outlineLevel: 2 } },
    ]
  },
  sections: [{
    properties: {
      page: {
        size: { width: 11906, height: 16838 },
        margin: { top: 1440, right: 1440, bottom: 1440, left: 1440 }
      }
    },
    headers: {
      default: new Header({ children: [new Paragraph({
        alignment: AlignmentType.RIGHT,
        children: [new TextRun({ text: "RT_SPARK 电缆监测系统 — 技术总结", size: 18, color: "888888" })]
      })] })
    },
    footers: {
      default: new Footer({ children: [new Paragraph({
        alignment: AlignmentType.CENTER,
        children: [
          new TextRun({ text: "第 ", size: 18 }),
          new TextRun({ children: [PageNumber.CURRENT], size: 18 }),
          new TextRun({ text: " 页", size: 18 })
        ]
      })] })
    },
    children: [
      // Title
      new Paragraph({
        alignment: AlignmentType.CENTER,
        spacing: { after: 400 },
        children: [new TextRun({ text: "RT_SPARK 电缆监测系统", size: 48, bold: true, font: "黑体", color: "1F4E79" })]
      }),
      new Paragraph({
        alignment: AlignmentType.CENTER,
        spacing: { after: 600 },
        children: [new TextRun({ text: "技术总结报告", size: 36, bold: true, font: "黑体", color: "2E75B6" })]
      }),

      // 1. Overview
      new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun("一、系统概述")] }),
      new Paragraph({ children: [new TextRun("本系统是一套基于 STM32 微控制器的电缆在线监测装置，通过 4G 无线模块（Air778E）将传感器数据实时上传至云端监控网页。系统实现了电缆舞动检测、环境温湿度监测、电机自动除雪控制等功能。")] }),
      new Paragraph({ spacing: { before: 120 }, children: [new TextRun({ text: "核心特点：", bold: true })] }),
      new Paragraph({ children: [new TextRun("• 完全无线传输：无 COM 口依赖，Air778E 4G 透传直连 frp 服务器")] }),
      new Paragraph({ children: [new TextRun("• 上电自动连接：配置保存到 Flash，断电后自动重连")] }),
      new Paragraph({ children: [new TextRun("• 实时波形显示：Canvas 60fps 渲染加速度波形")] }),
      new Paragraph({ children: [new TextRun("• 智能状态检测：五级状态机（IDLE / MONITOR / WARNING / SEVERE / ICE）")] }),
      new Paragraph({ children: [new TextRun("• 自动巡线除雪：三阶段状态机自动完成巡线作业")] }),

      // 2. Architecture
      new Paragraph({ heading: HeadingLevel.HEADING_1, spacing: { before: 400 }, children: [new TextRun("二、系统架构")] }),
      new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("2.1 数据流（上行）")] }),
      new Paragraph({ children: [new TextRun("STM32 传感器 → UART2 → Air778E → 4G → frp 服务器 → bridge.js → WebSocket → 网页")] }),
      new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("2.2 命令流（下行）")] }),
      new Paragraph({ children: [new TextRun("网页 → WebSocket → bridge.js → TCP → frp 服务器 → 4G → Air778E → UART2 → STM32")] }),
      new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("2.3 架构图")] }),
      new Paragraph({ children: [new TextRun({ text: "┌──────────┐    UART2    ┌─────────┐   4G   ┌────────────┐   TCP   ┌──────────┐   WS   ┌──────┐", font: "Consolas" })] }),
      new Paragraph({ children: [new TextRun({ text: "│  STM32   │◄───────────►│ Air778E │◄─────►│  frp 服务  │◄──────►│ bridge.js│◄─────►│ 网页 │", font: "Consolas" })] }),
      new Paragraph({ children: [new TextRun({ text: "│ 传感器   │             │ 4G 模块 │        │  frp-oil   │         │ 桥接服务 │        │前端  │", font: "Consolas" })] }),
      new Paragraph({ children: [new TextRun({ text: "└──────────┘             └─────────┘        └────────────┘         └──────────┘        └──────┘", font: "Consolas" })] }),

      // 3. Web Technology
      new Paragraph({ heading: HeadingLevel.HEADING_1, spacing: { before: 400 }, children: [new TextRun("三、网页技术栈")] }),
      new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("3.1 技术选型")] }),
      new Table({
        width: { size: 9360, type: WidthType.DXA },
        columnWidths: [2340, 3510, 3510],
        rows: [
          new TableRow({ children: [
            cell("技术项", 2340, { bold: true, shading: "D5E8F0", center: true }),
            cell("选择", 3510, { bold: true, shading: "D5E8F0", center: true }),
            cell("理由", 3510, { bold: true, shading: "D5E8F0", center: true })
          ]}),
          new TableRow({ children: [cell("前端框架", 2340), cell("纯原生 HTML/CSS/JS", 3510), cell("零依赖、单文件部署、加载快", 3510)] }),
          new TableRow({ children: [cell("通信协议", 2340), cell("WebSocket (RFC 6455)", 3510), cell("全双工、低延迟、原生支持", 3510)] }),
          new TableRow({ children: [cell("渲染引擎", 2340), cell("Canvas 2D API", 3510), cell("60fps 流畅、高 DPI 支持", 3510)] }),
          new TableRow({ children: [cell("数据格式", 2340), cell("JSON 流（按行解析）", 3510), cell("简单、可扩展、易调试", 3510)] }),
          new TableRow({ children: [cell("布局方案", 2340), cell("Flex + Grid + CSS 变量", 3510), cell("响应式、主题统一管理", 3510)] }),
        ]
      }),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 240 }, children: [new TextRun("3.2 通信模块")] }),
      new Paragraph({ children: [new TextRun({ text: "WebSocket 连接状态机：", bold: true })] }),
      new Paragraph({ children: [new TextRun("未连接 → 连接中 → 已连接 → 断开 → 自动重连。心跳检测每 10 秒发送一次，30 秒未收到服务端心跳则判定断开。")] }),
      new Paragraph({ children: [new TextRun({ text: "消息类型：", bold: true })] }),
      new Paragraph({ children: [new TextRun("system（系统事件）、galloping（舞动检测）、raw_accel（原始加速度）、env（温湿度）、motor（电机状态）、heartbeat（心跳包）")] }),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 240 }, children: [new TextRun("3.3 实时波形绘制")] }),
      new Paragraph({ children: [new TextRun({ text: "加速度波形：", bold: true })] }),
      new Paragraph({ children: [new TextRun("• 采样窗口：300 个点（约 15 秒历史）")] }),
      new Paragraph({ children: [new TextRun("• 自适应缩放：95% 分位数动态确定纵轴范围，过滤 >1000 的异常值")] }),
      new Paragraph({ children: [new TextRun("• 零线居中：虚线标示，渐变填充")] }),
      new Paragraph({ children: [new TextRun({ text: "温湿度曲线：", bold: true })] }),
      new Paragraph({ children: [new TextRun("• 双 Y 轴：温度（红色）、湿度（青色）")] }),
      new Paragraph({ children: [new TextRun("• 历史长度：120 个点（约 10 分钟）")] }),
      new Paragraph({ children: [new TextRun({ text: "渲染策略：", bold: true })] }),
      new Paragraph({ children: [new TextRun("60fps requestAnimationFrame 循环，高 DPI 支持（devicePixelRatio 缩放），仅在数据变化时重绘")] }),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 240 }, children: [new TextRun("3.4 状态监控面板")] }),
      new Table({
        width: { size: 9360, type: WidthType.DXA },
        columnWidths: [1560, 1560, 6240],
        rows: [
          new TableRow({ children: [
            cell("状态", 1560, { bold: true, shading: "D5E8F0", center: true }),
            cell("颜色", 1560, { bold: true, shading: "D5E8F0", center: true }),
            cell("触发条件", 6240, { bold: true, shading: "D5E8F0", center: true })
          ]}),
          new TableRow({ children: [cell("IDLE", 1560), cell("灰色", 1560), cell("振幅 < 0.02g，无异常", 6240)] }),
          new TableRow({ children: [cell("MONITOR", 1560), cell("蓝色", 1560), cell("振幅 0.02~0.08g，持续监测", 6240)] }),
          new TableRow({ children: [cell("WARNING", 1560), cell("黄色", 1560), cell("振幅 > 0.08g 或频率异常", 6240)] }),
          new TableRow({ children: [cell("SEVERE", 1560), cell("红色", 1560), cell("振幅 > 0.15g，高危风险", 6240)] }),
          new TableRow({ children: [cell("ICE", 1560), cell("青色", 1560), cell("低温高湿 + 低频振动", 6240)] }),
        ]
      }),
      new Paragraph({ spacing: { before: 120 }, children: [new TextRun("高危弹窗：SEVERE/ICE 状态自动弹出红色预警，15 秒自动关闭。实时日志：200 条滚动日志，带时间戳和标签分类。")] }),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 240 }, children: [new TextRun("3.5 控制面板")] }),
      new Paragraph({ children: [new TextRun({ text: "电机控制：", bold: true })] }),
      new Paragraph({ children: [new TextRun("前进、后退、停止、加热开关。定时驱动：按一下前进/后退，持续 6 秒后自动停止（STM32 端定时器实现）。")] }),
      new Paragraph({ children: [new TextRun({ text: "自动巡线除雪：", bold: true })] }),
      new Paragraph({ children: [new TextRun("Phase 0：中间(50%) → 右端(100%)，前进；Phase 1：右端(100%) → 左端(0%)，后退；Phase 2：左端(0%) → 中间(50%)，前进。到达终点自动转向，回中间后自动停止。")] }),
      new Paragraph({ children: [new TextRun({ text: "键盘快捷键：", bold: true })] }),
      new Paragraph({ children: [new TextRun("W/↑ 前进，S/↓ 后退，Space 停止，H 加热切换，R 重置检测器")] }),

      // 4. Algorithm Technology
      new Paragraph({ heading: HeadingLevel.HEADING_1, spacing: { before: 400 }, children: [new TextRun("四、算法技术栈（STM32 固件）")] }),
      new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("4.1 传感器数据层")] }),
      new Paragraph({ children: [new TextRun({ text: "BNO055 加速度读取：", bold: true })] }),
      new Paragraph({ children: [new TextRun("接口：I2C（软件 I2C，SCL→PD10, SDA→PD8）；输出：三轴加速度（m/s²）；采样频率：20 Hz（50ms 周期）；关键修正：原始数据 × 0.001 × 9.80665（单位转换为 m/s²）")] }),
      new Paragraph({ children: [new TextRun({ text: "SHT3X 温湿度读取：", bold: true })] }),
      new Paragraph({ children: [new TextRun("接口：I2C（与 BNO055 共享总线）；精度：温度 ±0.3°C，湿度 ±2%RH；采样间隔：5 秒")] }),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 240 }, children: [new TextRun("4.2 加速度预处理")] }),
      new Paragraph({ children: [new TextRun({ text: "去直流（DC Removal）：", bold: true })] }),
      new Paragraph({ children: [new TextRun("static_mean = Σ(x_i) / N（滑动窗口均值，N=50，约 2.5 秒）；filtered = x - static_mean。目的：去除重力分量，只保留振动信号。")] }),
      new Paragraph({ children: [new TextRun({ text: "中值滤波：", bold: true })] }),
      new Paragraph({ children: [new TextRun("窗口大小：5 点；目的：去除突发噪声（毛刺）；复杂度：O(1) 环形缓冲区实现。")] }),
      new Paragraph({ children: [new TextRun({ text: "低通滤波：", bold: true })] }),
      new Paragraph({ children: [new TextRun("y[n] = α × x[n] + (1-α) × y[n-1]，α = 0.3（截止频率约 3Hz，保留有效振动频段 0.1~5Hz）。")] }),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 240 }, children: [new TextRun("4.3 舞动检测算法")] }),
      new Paragraph({ heading: HeadingLevel.HEADING_3, children: [new TextRun("4.3.1 特征提取")] }),
      new Table({
        width: { size: 9360, type: WidthType.DXA },
        columnWidths: [2340, 3510, 3510],
        rows: [
          new TableRow({ children: [
            cell("特征", 2340, { bold: true, shading: "D5E8F0", center: true }),
            cell("计算方法", 3510, { bold: true, shading: "D5E8F0", center: true }),
            cell("物理意义", 3510, { bold: true, shading: "D5E8F0", center: true })
          ]}),
          new TableRow({ children: [cell("amp_dominant", 2340), cell("max(amp_x, amp_y, amp_z)", 3510), cell("主导振幅", 3510)] }),
          new TableRow({ children: [cell("amp_x/y/z_pp", 2340), cell("max - min（峰峰值）", 3510), cell("各轴振动强度", 3510)] }),
          new TableRow({ children: [cell("rms_accel", 2340), cell("√(Σ(x²)/N)", 3510), cell("有效加速度", 3510)] }),
          new TableRow({ children: [cell("vibr_energy", 2340), cell("Σ(x²)", 3510), cell("振动能量", 3510)] }),
          new TableRow({ children: [cell("zero_cross_rate", 2340), cell("过零次数 / N", 3510), cell("频率特征", 3510)] }),
          new TableRow({ children: [cell("dominant_freq", 2340), cell("过零法估算", 3510), cell("主导频率", 3510)] }),
          new TableRow({ children: [cell("displacement_est", 2340), cell("二重积分", 3510), cell("位移估算", 3510)] }),
          new TableRow({ children: [cell("torsion_deg", 2340), cell("atan2(y, x)", 3510), cell("扭转角度", 3510)] }),
          new TableRow({ children: [cell("confidence", 2340), cell("多特征加权", 3510), cell("置信度", 3510)] }),
        ]
      }),

      new Paragraph({ heading: HeadingLevel.HEADING_3, spacing: { before: 200 }, children: [new TextRun("4.3.2 状态机")] }),
      new Paragraph({ children: [new TextRun("五级状态：IDLE → MONITOR → WARNING → SEVERE → ICE。状态转换基于振幅、频率、温度、湿度的综合阈值判断。采用滞回设计，降级比升级更严格，避免状态抖动。")] }),

      new Paragraph({ heading: HeadingLevel.HEADING_3, spacing: { before: 200 }, children: [new TextRun("4.3.3 阈值参数")] }),
      new Table({
        width: { size: 9360, type: WidthType.DXA },
        columnWidths: [3120, 3120, 3120],
        rows: [
          new TableRow({ children: [
            cell("参数", 3120, { bold: true, shading: "D5E8F0", center: true }),
            cell("值", 3120, { bold: true, shading: "D5E8F0", center: true }),
            cell("说明", 3120, { bold: true, shading: "D5E8F0", center: true })
          ]}),
          new TableRow({ children: [cell("AMP_IDLE", 3120), cell("0.02 g", 3120), cell("静止阈值", 3120)] }),
          new TableRow({ children: [cell("AMP_WARNING", 3120), cell("0.08 g", 3120), cell("警告阈值", 3120)] }),
          new TableRow({ children: [cell("AMP_SEVERE", 3120), cell("0.15 g", 3120), cell("高危阈值", 3120)] }),
          new TableRow({ children: [cell("FREQ_LOW", 3120), cell("0.1 Hz", 3120), cell("低频警戒", 3120)] }),
          new TableRow({ children: [cell("FREQ_HIGH", 3120), cell("5.0 Hz", 3120), cell("高频警戒", 3120)] }),
          new TableRow({ children: [cell("ICE_TEMP", 3120), cell("0 °C", 3120), cell("结冰温度", 3120)] }),
          new TableRow({ children: [cell("ICE_HUMIDITY", 3120), cell("80%", 3120), cell("结冰湿度", 3120)] }),
        ]
      }),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 240 }, children: [new TextRun("4.4 电机控制算法")] }),
      new Paragraph({ children: [new TextRun({ text: "定时驱动：", bold: true })] }),
      new Paragraph({ children: [new TextRun("start_motor_with_timeout(dir)：删除旧定时器 → 启动电机（dir=1 前进，dir=-1 后退）→ 创建 6 秒单次定时器 → 到期调用 stop() → 发送电机停止 JSON。定时器采用 RT-Thread rt_timer_create，单次触发（ONE_SHOT），重复触发会重置定时器，不会累加。")] }),
      new Paragraph({ children: [new TextRun({ text: "位置估算（网页端）：", bold: true })] }),
      new Paragraph({ children: [new TextRun("position += speed × dt；position = clamp(position, 0, 100)。速度：前进 +1%/s，后退 -1%/s（模拟值，实际可用编码器校准）。")] }),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 240 }, children: [new TextRun("4.5 JSON 序列化")] }),
      new Paragraph({ children: [new TextRun("由于 RT-Thread 的 rt_snprintf 不支持 %f，采用定点转换函数：")] }),
      new Paragraph({ children: [new TextRun({ text: "f2s_3(buf, v)：将浮点数转为 3 位小数字符串。int_part = (int)(v)；frac = (int)((v - int_part) × 1000 + 0.5)。无浮点格式化，无动态内存分配。单条 JSON 最大 512 字节，每行一条 JSON，以 \\n 结尾。", font: "Consolas" })] }),

      // 5. Communication Protocol
      new Paragraph({ heading: HeadingLevel.HEADING_1, spacing: { before: 400 }, children: [new TextRun("五、通信协议")] }),
      new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun("5.1 上行协议（STM32 → 网页）")] }),
      new Paragraph({ children: [new TextRun({ text: "舞动检测：", bold: true, font: "Consolas" })] }),
      new Paragraph({ children: [new TextRun({ text: '{"type":"galloping","ts":12345,"state":"SEVERE","amp_dominant":0.234,...}', font: "Consolas", size: 18 })] }),
      new Paragraph({ children: [new TextRun({ text: "原始加速度：", bold: true, font: "Consolas" })] }),
      new Paragraph({ children: [new TextRun({ text: '{"type":"raw_accel","ts":12345,"ax":0.123,"ay":0.456,"az":9.789}', font: "Consolas", size: 18 })] }),
      new Paragraph({ children: [new TextRun({ text: "温湿度：", bold: true, font: "Consolas" })] }),
      new Paragraph({ children: [new TextRun({ text: '{"type":"env","ts":12345,"temperature":-5.2,"humidity":85.3}', font: "Consolas", size: 18 })] }),
      new Paragraph({ children: [new TextRun({ text: "电机状态：", bold: true, font: "Consolas" })] }),
      new Paragraph({ children: [new TextRun({ text: '{"type":"motor","ts":12345,"motor_state":"forward","position":67}', font: "Consolas", size: 18 })] }),
      new Paragraph({ children: [new TextRun({ text: "心跳：", bold: true, font: "Consolas" })] }),
      new Paragraph({ children: [new TextRun({ text: '{"type":"heartbeat","ts":12345}', font: "Consolas", size: 18 })] }),

      new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 240 }, children: [new TextRun("5.2 下行协议（网页 → STM32）")] }),
      new Paragraph({ children: [new TextRun("纯文本命令，以 \\n 结尾：forward, backward, stop, heater_on, heater_off, reset_detector")] }),

      // 6. Key Design Decisions
      new Paragraph({ heading: HeadingLevel.HEADING_1, spacing: { before: 400 }, children: [new TextRun("六、关键设计决策")] }),
      new Table({
        width: { size: 9360, type: WidthType.DXA },
        columnWidths: [2340, 3510, 3510],
        rows: [
          new TableRow({ children: [
            cell("决策", 2340, { bold: true, shading: "D5E8F0", center: true }),
            cell("选择", 3510, { bold: true, shading: "D5E8F0", center: true }),
            cell("理由", 3510, { bold: true, shading: "D5E8F0", center: true })
          ]}),
          new TableRow({ children: [cell("无线架构", 2340), cell("4G 透传（Air778E）", 3510), cell("测试时无物理连线，支持远程部署", 3510)] }),
          new TableRow({ children: [cell("网页框架", 2340), cell("纯原生 JS", 3510), cell("零依赖，单文件部署，加载快", 3510)] }),
          new TableRow({ children: [cell("波形渲染", 2340), cell("Canvas 2D", 3510), cell("高性能，60fps 流畅，支持高 DPI", 3510)] }),
          new TableRow({ children: [cell("去直流算法", 2340), cell("滑动窗口均值", 3510), cell("简单高效，适合 MCU 实时处理", 3510)] }),
          new TableRow({ children: [cell("电机定时", 2340), cell("6 秒自动停止", 3510), cell("防止失控，安全冗余", 3510)] }),
          new TableRow({ children: [cell("JSON 格式化", 2340), cell("定点转换", 3510), cell("避免 printf 浮点支持问题", 3510)] }),
        ]
      }),

      // 7. Deployment
      new Paragraph({ heading: HeadingLevel.HEADING_1, spacing: { before: 400 }, children: [new TextRun("七、部署步骤")] }),
      new Paragraph({ children: [new TextRun({ text: "1. 配置 Air778E（一次性）：", bold: true })] }),
      new Paragraph({ children: [new TextRun({ text: 'AT+CFUN=1\nAT+CGATT=1\nAT+CGDCONT=1,"IP","CMNET"\nAT+CGACT=1,1\nAT+CIPSTART="TCP","frp-oil.com","32762"\nAT+CIPMODE=1\nAT+CIPATS=1,10\nAT+SAVA', font: "Consolas" })] }),
      new Paragraph({ children: [new TextRun({ text: "2. 编译烧录 STM32：包含 uart_recv.c、dtu_sender.c、galloping_detect.c、main.c", bold: true })] }),
      new Paragraph({ children: [new TextRun({ text: "3. 启动 bridge.js：node bridge.js", bold: true })] }),
      new Paragraph({ children: [new TextRun({ text: "4. 打开网页：cable-monitor.html 或 http://localhost:8080", bold: true })] }),
      new Paragraph({ children: [new TextRun({ text: "5. 拔掉 Air778E USB 线，接到 STM32 UART2，上电自动连接", bold: true })] }),

      // 8. File List
      new Paragraph({ heading: HeadingLevel.HEADING_1, spacing: { before: 400 }, children: [new TextRun("八、项目文件清单")] }),
      new Paragraph({ children: [new TextRun({ text: "固件端：", bold: true })] }),
      new Paragraph({ children: [new TextRun("• main.c — 主入口、电机控制、加热控制")] }),
      new Paragraph({ children: [new TextRun("• dtu_sender.c / dtu_sender.h — 4G 数据发送模块")] }),
      new Paragraph({ children: [new TextRun("• uart_recv.c — UART2 无线命令接收")] }),
      new Paragraph({ children: [new TextRun("• galloping_detect.c / galloping_detect.h — 舞动检测算法")] }),
      new Paragraph({ children: [new TextRun("• gy_bn0055.c / gy_bn0055.h — BNO055 驱动")] }),
      new Paragraph({ children: [new TextRun("• sht3x.c / sht3x.h — SHT3X 温湿度驱动")] }),
      new Paragraph({ children: [new TextRun({ text: "服务端：", bold: true })] }),
      new Paragraph({ children: [new TextRun("• bridge.js — TCP ↔ WebSocket 无线桥接服务")] }),
      new Paragraph({ children: [new TextRun("• cable-monitor.html — 监控网页前端")] }),
    ]
  }]
});

Packer.toBuffer(doc).then(buffer => {
  fs.writeFileSync("RT_SPARK_技术总结.docx", buffer);
  console.log("Word 文档已生成: RT_SPARK_技术总结.docx");
});
