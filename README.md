# 小白-Ai · AI 玩具小车主控固件

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

「小白-Ai」AI 玩具小车的主控 MCU 固件——一个集 **语音交互、双电机驱动、BLE 遥控、点阵眼睛表情** 于一体的智能玩具小车。裸机（无 RTOS）单主循环架构，四层清晰分层，代码可读、稳定、易维护。

## 目录

- [功能特性](#功能特性)
- [硬件规格](#硬件规格)
- [软件架构](#软件架构)
- [目录结构](#目录结构)
- [构建与烧录](#构建与烧录)
- [通信协议](#通信协议)
- [使用说明](#使用说明)
- [文档](#文档)
- [开发约定](#开发约定)

---

## 功能特性

### 🎙️ 语音模式

- ASRPRO 离线语音识别（v0.8 协议），说"小白小白"唤醒
- 11 条动作命令：前进 / 后退 / 左转 / 右转 / 停止 / 左右单电机正反转停
- 语音切换 4 种模式；命令 ID 1-16 / 42-51，播报 ID 17-41 / 52-63 分区管理
- 唤醒 `wake` 起呼吸灯、休眠 `sleep` 立即熄灭

### ⚡ 动力模式

- 5 档动作：停止 / 前进 / 后退 / 左转 / 右转（坦克转向）
- 按键逐档轮换，每档播报对应语音

### 📡 感应模式

- 4 种玩法：**靠近启动**（有物体前进）/ **遇障停止** / **挥手开关**（红外边沿 + 500ms 消抖）/ **明暗调速**（反射越强速度越快）
- 3 路红外反射传感器 + ADC 阈值判断

### 🎮 遥控模式

- BLE 透传（ECB02）+ PF3 电平判连接
- 兼容原 17 字节遥控帧；新增 BLE v2：`C2` 编程请求 / `D2` 响应（DONE/查询/错误）/ `D3` 事件
- 方向键坦克转向、单电机微操（Y/A/X/B）、肩键 **L1 加速 / R1 减速** 三档调速
- **1s 无帧自动停机**——防断连电机狂转

### 🧩 编程模式（第 5 模式）

- 上位机（Windows Blockly）负责循环 / 条件 / 等待与流程编排，主控作为 BLE 指令执行器
- 定时电机与组合移动、持续动作、功率档、单电机保留、刹停；等待红外（左/中/右、`>`/`<`、0–100）与等待词条（ASR_01–ASR_10）
- 表情 EYE_01–EYE_10 按原帧时长循环、数字 0–100、关闭显示；P01–P10 播报
- 需要回报的任务完成后立即 `DONE` 并每 200ms 重发，直到下一条指令；空闲心跳 300ms，前台有效 C2 也刷新会话，1s 无有效流量自动刹车退出

### 👀 表现层

- TM1640 驱动 8×14 点阵眼睛：开机默认 **EYE_01 待机表情**（V0.2 十套动画按帧时长循环），支持数字与关闭显示
- PA9 呼吸灯：跟随语音唤醒状态（`wake` 启动、`sleep` 熄灭，无本地超时）

### 🔋 电源管理

- KEY1 长按 ≥2s 开机确认 + MOS 锁存，KEY1 长按关机（关机动画 + 语音 + 断电）
- 低电量检测：VREFINT 反算真实 VDDA（电池直供 MCU，VDDA 随电压变化），滤波 + 迟滞 + 5s 冷却播报

---

## 硬件规格

| 项目 | 规格 |
| --- | --- |
| 主控 | 普冉 **PY32F030K28U6TR**（Cortex-M0+，Flash 32KB，RAM 4KB，48MHz max） |
| 语音识别 | ASRPRO 芯片（USART2，9600 8N1，协议 v0.7） |
| BLE | ECB02 透传模块（USART1，9600 8N1，PF3 STA 判连接） |
| 电机 | TIM3 4 通道 PWM 20kHz 双电机，3 档速度（40% / 70% / 100%） |
| 眼睛屏 | TM1640 驱动 8×14 点阵（16 列芯片用 14 列） |
| 传感器 | 3 路红外反射（PA1/PA2/PA3 + PF4 发射常亮）+ 1 路电池分压（PA0） |
| 按键 | 4 个（PB3/PB4/PB5/PB8，外部上拉），KEY1 兼开机/关机 |
| 电源 | PA15 MOS 锁存，上电长按确认 2s |

> 完整引脚分配见 `resource/小白IO分配.xlsx`（工作区外文档）。

---

## 软件架构

四层单向依赖：**App → Protocol → BSP → HAL**。应用层不直接碰协议细节，协议层不碰硬件寄存器，各层职责单一。

```text
┌─────────────────────────────────────────────┐
│  App 应用层        User/                     │
│  模式状态机 · 车辆动作 · 动画 · 电源流程      │
├─────────────────────────────────────────────┤
│  Protocol 协议层   Protocol/                 │
│  语音协议 v0.7 · 遥控帧协议                  │
├─────────────────────────────────────────────┤
│  BSP 驱动层        BSP_Drivers/              │
│  每外设一子目录，只做硬件操作                │
├─────────────────────────────────────────────┤
│  HAL 底层          PY32F0xx_HAL_Driver/      │
│  普冉官方 HAL/LL + CMSIS（供应商代码）       │
└─────────────────────────────────────────────┘
```

### 应用层模块（User/）

| 模块 | 职责 |
| --- | --- |
| `main.c` | 精简入口（13 行）：`HAL_Init → App_System_Init → BSP_Init → App_Init → App_Loop` |
| `App_Main` | 主循环调度框架（按键/语音/BLE/模式/动画/电池轮询） |
| `App_Mode` | 模式状态机 + 模式切换（`App_Mode_Switch`） |
| `App_Mode_Voice` | 语音模式 11 条动作命令 |
| `App_Mode_Power` | 动力模式 5 动作 |
| `App_Mode_Sensor` | 感应模式 4 玩法 |
| `App_Mode_Remote` | 遥控模式（帧消费 + L1 加速/R1 减速 + 超时短刹） |
| `App_Program` | 编程模式执行器（跑马/心跳/任务/DONE 重报/查询与事件） |
| `App_Vehicle` | 车辆动作统一接口（消除重复电机组合） |
| `App_Display` / `Eye_Data` | 待机与手动表情、数字显示（数据由 `tools/gen_eye_data.py` 构建期生成） |
| `App_Breath` | 呼吸灯动画（wake 启动 / sleep 熄灭） |
| `App_Shutdown` | 关机流程 |
| `App_Battery` | 电池采样 + 低电量播报 |
| `App_System` | 系统时钟配置 + 错误处理 |

### 协议层模块（Protocol/）

| 模块 | 职责 |
| --- | --- |
| `Proto_Asr` | 语音协议：ID 宏（`ASR_VOICE_*` 17-41/52-63、`ASR_CMD_*` 1-16/42-51）+ `cmd_to_voice` 映射（唯一权威） |
| `Proto_Ble` | BLE v2：多类型 17 字节帧解析（C1/C2/D2/D3）+ 校验 + 响应/事件编码 |
| `Proto_Remote` | C1 遥控键位定义（帧解析统一在 `Proto_Ble`） |

### 驱动层（BSP_Drivers/）

每外设一个子目录：`Bsp_Tick / Led / Power / LedPwm / Key / Motor / Adc / IR / Battery / UartAsr / UartBle / Tm1640`，`Bsp.c` 按固定顺序汇总初始化。

---

## 目录结构

```text
├── User/                      # 应用层（入口 + App 模块）
├── Protocol/                  # 协议层（Proto_Asr / Proto_Ble / Proto_Remote）
├── BSP_Drivers/               # 驱动层（每外设一子目录）
├── PY32F0xx_HAL_Driver/       # 普冉官方 HAL/LL（供应商代码）
├── CMSIS/                     # ARM CMSIS + 启动文件
├── MDK-ARM/                   # Keil 工程（XiaoBai.uvprojx）
├── tests/host/                # 宿主 C 测试（gcc，协议/执行器/显示）
├── tools/xiaobai_programmer/  # Windows Blockly 编程上位机（PySide6 + 离线 Blockly）
└── doc/                       # 项目记忆库（框架/技术栈/踩坑/进度）
```

---

## 构建与烧录

### 环境要求

| 项 | 值 |
| --- | --- |
| IDE | Keil uVision5（工程 `MDK-ARM/XiaoBai.uvprojx`） |
| 编译器 | **ARM Compiler V5.06（AC5）** ⚠️ 勿切换 AC6 |
| 器件包 | Puya.PY32F0xx_DFP.1.1.9 |
| 编译宏 | `USE_HAL_DRIVER,PY32F030x6`；C99 |

### 命令行构建

```bat
UV4 -b MDK-ARM/XiaoBai.uvprojx -j0 -o build.log
```

### Keil GUI

打开 `MDK-ARM/XiaoBai.uvprojx` → F7 编译 → F8 烧录（SWD，UL2CM3）。

### 产物

- 烧录文件：`MDK-ARM/Output/XiaoBai.hex`
- 调试文件：`XiaoBai.axf` / `XiaoBai.map`

> ⚠️ 若命令行构建耗时显示 `00:00:00`，通常是残留的 UV4 GUI 进程导致假成功——先结束 UV4 进程再构建。

---

## 通信协议

| 协议 | 版本 | 要点 |
| --- | --- | --- |
| 语音芯片交互 | **v0.8** | ASCII 文本：MCU 发 `play=NN\n`；收 `cmd=NN\r\n` / `done=NN\r\n` / `wake\r\n` / `sleep\r\n`；命令 ID 1-16/42-51，播报 ID 17-41/52-63 |
| BLE 协议 | **v2** | 17 字节外壳：`5A SRC DST 0A TYPE DATA[10] CRC A5`；`C1` 遥控 / `C2` 编程请求 / `D2` 完成·查询·错误 / `D3` 事件 |
| 遥控帧 | v1 | 原 C1 帧完全兼容：`5A 97 98 0A C1 + 10B 键位图 + CRC + A5` |
| BLE 透传 | — | ECB02 从机透传，连接状态用 **PF3 电平**（高=已连接），收发按保护间隔错峰调度 |

> 协议 ID 权威映射：`Protocol/Proto_Asr.h`（宏）+ `Protocol/Proto_Asr.c`（`cmd_to_voice[]` 表）。**改协议 ID 必须两处同步核对。**

---

## 使用说明

1. **开机**：长按 KEY1 ≥2s，BOOT 语后默认进入语音模式（LED1 亮）
2. **切换模式**：按键（KEY1+LED1 语音 / KEY2+LED2 动力 / KEY3+LED4 遥控 / KEY4+LED3 感应）或语音"XX 模式"
3. **动力动作**：KEY2 在动力模式下逐次轮换 5 种动作
4. **感应玩法**：KEY4 在感应模式下逐次轮换 4 种玩法
5. **遥控**：BLE 连接后（PF3 高电平，播"遥控已连接"），方向键驾驶，肩键调速，1s 无帧自动停机
6. **唤醒**：说唤醒词 → 呼吸灯亮起；语音端进入休眠（`sleep`）时立即熄灭
7. **编程模式**：用 `tools/xiaobai_programmer` 上位机（打开编程页自动进入；四灯跑马；任一模式键短按退出）
8. **关机**：长按 KEY1，播关机语 + 关机动画后断电

---

## 文档

| 文档 | 位置 |
| --- | --- |
| 项目记忆库（框架/技术栈/踩坑/进度） | `doc/`（`doc/README.md` 为索引） |
| 开发约定与硬性门禁 | `AGENTS.md` |
| 重构设计文档 | `docs/superpowers/specs/2026-08-04-firmware-layering-refactor-design.md` |
| 重构实施计划 | `docs/superpowers/plans/2026-08-04-firmware-layering-refactor.md` |
| BSP 实施计划 | `../docs/plan-bsp-v1.md`（工作区外） |
| 硬件引脚分配 | `../resource/小白IO分配.xlsx`（工作区外） |
| 语音协议 v0.8 | `resource/语音芯片交互协议.md` |
| BLE 协议 v2（编程模式） | `docs/BLE协议v2-编程模式.md` + 黄金帧 `docs/ble_v2_golden_frames.json` |
| 编程上位机使用说明 | `tools/xiaobai_programmer/README.md` |
| 上位机协议/指令/用法/发送周期 | `docs/上位机编程协议与用法.md` |
| 手机 APP BLE 控制协议（APP 端开发） | `docs/手机APP-BLE控制协议.md` |
| 需求来源 | `docs/小白控制功能表.md` |

---

## 开发约定

- 裸机非阻塞：时间基准只用 `Bsp_Tick_GetMs()`，事件靠主循环轮询 + 中断置标志，禁止长阻塞
- 命名：函数 `Bsp_<模块>_<动作>`，类型 `Bsp_Xxx_Type_t`，枚举值大写；注释中文、代码英文
- 状态用 `static const` 查表驱动，枚举与表顺序严格对齐
- 分支工作流：日常开发在 `main-work`，完成里程碑合并回 `main` 并推送
- 质量门禁：**编译 0 Error 0 Warning + 代码审查 + 板上实测**

## License

MIT
