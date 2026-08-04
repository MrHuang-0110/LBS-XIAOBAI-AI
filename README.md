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
- ASRPRO 离线语音识别（v0.7 协议），说"小白小白"唤醒
- 11 条动作命令：前进 / 后退 / 左转 / 右转 / 停止 / 左右单电机正反转停
- 语音切换 4 种模式，命令与播报 ID 分区管理（命令 1-16 / 播报 17-41）

### ⚡ 动力模式
- 5 档动作：停止 / 前进 / 后退 / 左转 / 右转（坦克转向）
- 按键逐档轮换，每档播报对应语音

### 📡 感应模式
- 4 种玩法：**靠近启动**（有物体前进）/ **遇障停止** / **挥手开关**（红外边沿 + 500ms 消抖）/ **明暗调速**（反射越强速度越快）
- 3 路红外反射传感器 + ADC 阈值判断

### 🎮 遥控模式
- BLE 透传（ECB00CV2）+ PF3 电平判连接
- 17 字节遥控帧协议（帧头/CRC/帧尾校验）
- 方向键坦克转向、单电机微操（Y/A/X/B）、肩键 R1/L1 三档调速
- **1s 无帧自动停机**——防断连电机狂转

### 👀 表现层
- TM1640 驱动 8×14 点阵眼睛：未连接双眨 / 已连接瞳孔移动（AI 生命力）
- PA9 呼吸灯：唤醒启动，15s 超时自动关

### 🔋 电源管理
- KEY1 长按 ≥2s 开机确认 + MOS 锁存，KEY1 长按关机（关机动画 + 语音 + 断电）
- 低电量检测：VREFINT 反算真实 VDDA（电池直供 MCU，VDDA 随电压变化），滤波 + 迟滞 + 5s 冷却播报

---

## 硬件规格

| 项目 | 规格 |
|---|---|
| 主控 | 普冉 **PY32F030K28U6TR**（Cortex-M0+，Flash 32KB，RAM 4KB，48MHz max） |
| 语音识别 | ASRPRO 芯片（USART2，9600 8N1，协议 v0.7） |
| BLE | ECB00CV2 透传模块（USART1，9600 8N1，PF3 STA 判连接） |
| 电机 | TIM3 4 通道 PWM 20kHz 双电机，3 档速度（40% / 70% / 100%） |
| 眼睛屏 | TM1640 驱动 8×14 点阵（16 列芯片用 14 列） |
| 传感器 | 3 路红外反射（PA1/PA2/PA3 + PF4 发射常亮）+ 1 路电池分压（PA0） |
| 按键 | 4 个（PB3/PB4/PB5/PB8，外部上拉），KEY1 兼开机/关机 |
| 电源 | PA15 MOS 锁存，上电长按确认 2s |

> 完整引脚分配见 `resource/小白IO分配.xlsx`（工作区外文档）。

---

## 软件架构

四层单向依赖：**App → Protocol → BSP → HAL**。应用层不直接碰协议细节，协议层不碰硬件寄存器，各层职责单一。

```
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
|---|---|
| `main.c` | 精简入口（13 行）：`HAL_Init → App_System_Init → BSP_Init → App_Init → App_Loop` |
| `App_Main` | 主循环调度框架（按键/语音/BLE/模式/动画/电池轮询） |
| `App_Mode` | 模式状态机 + 模式切换（`App_Mode_Switch`） |
| `App_Mode_Voice` | 语音模式 11 条动作命令 |
| `App_Mode_Power` | 动力模式 5 动作 |
| `App_Mode_Sensor` | 感应模式 4 玩法 |
| `App_Mode_Remote` | 遥控模式（帧消费 + 肩键调速 + 超时停机） |
| `App_Vehicle` | 车辆动作统一接口（消除重复电机组合） |
| `App_Eye` | TM1640 眼睛动画（双眨/瞳孔移动） |
| `App_Breath` | 呼吸灯动画（wake 启动 / 15s 超时） |
| `App_Shutdown` | 关机流程 |
| `App_Battery` | 电池采样 + 低电量播报 |
| `App_System` | 系统时钟配置 + 错误处理 |

### 协议层模块（Protocol/）

| 模块 | 职责 |
|---|---|
| `Proto_Asr` | 语音协议：ID 宏（`ASR_VOICE_*` 17-41 / `ASR_CMD_*` 1-16）+ 命令→播报映射表（唯一权威） |
| `Proto_Remote` | 遥控帧协议：流缓冲 + 帧校验（CRC）+ 按键位图提取 |

### 驱动层（BSP_Drivers/）

每外设一个子目录：`Bsp_Tick / Led / Power / LedPwm / Key / Motor / Adc / IR / Battery / UartAsr / UartBle / Tm1640`，`Bsp.c` 按固定顺序汇总初始化。

---

## 目录结构

```
├── User/                      # 应用层（入口 + 12 个 App 模块）
├── Protocol/                  # 协议层（Proto_Asr / Proto_Remote）
├── BSP_Drivers/               # 驱动层（每外设一子目录）
├── PY32F0xx_HAL_Driver/       # 普冉官方 HAL/LL（供应商代码）
├── CMSIS/                     # ARM CMSIS + 启动文件
├── MDK-ARM/                   # Keil 工程（XiaoBai.uvprojx）
└── doc/                       # 项目记忆库（框架/技术栈/踩坑/进度）
```

---

## 构建与烧录

### 环境要求

| 项 | 值 |
|---|---|
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
|---|---|---|
| 语音芯片交互 | **v0.7** | ASCII 文本：MCU 发 `play=NN\n`，收 `cmd=NN\r\n` / `done=NN\r\n` / `wake\r\n`；命令 ID 1-16，播报 ID 17-41 不重叠 |
| 遥控帧 | — | 17 字节：`5A 97 98 0A C1 + 10B 键位图 + CRC + A5`，CRC = 前 15 字节累加和低 8 位 |
| BLE 透传 | — | ECB00 默认从机透传，连接状态用 **PF3 电平**（高=已连接），不解析字符串 |

> 协议 ID 权威映射：`Protocol/Proto_Asr.h`（宏）+ `Protocol/Proto_Asr.c`（`cmd_to_voice[]` 表）。**改协议 ID 必须两处同步核对。**

---

## 使用说明

1. **开机**：长按 KEY1 ≥2s，BOOT 语后默认进入语音模式（LED1 亮）
2. **切换模式**：按键（KEY1 语音 / KEY2 感应 / KEY3 遥控 / KEY4 动力）或语音"进入 XX 模式"
3. **感应玩法**：KEY2 在感应模式下逐次轮换 4 种玩法
4. **动力动作**：KEY4 在动力模式下逐次轮换 5 种动作
5. **遥控**：BLE 连接后（PF3 高电平，播"遥控已连接"），方向键驾驶，肩键调速，1s 无帧自动停机
6. **唤醒**：对语音模块说唤醒词，呼吸灯亮起（15s 自动熄灭）
7. **关机**：长按 KEY1，播关机语 + 关机动画后断电

---

## 文档

| 文档 | 位置 |
|---|---|
| 项目记忆库（框架/技术栈/踩坑/进度） | `doc/`（`doc/README.md` 为索引） |
| 开发约定与硬性门禁 | `AGENTS.md` |
| 重构设计文档 | `docs/superpowers/specs/2026-08-04-firmware-layering-refactor-design.md` |
| 重构实施计划 | `docs/superpowers/plans/2026-08-04-firmware-layering-refactor.md` |
| BSP 实施计划 | `../docs/plan-bsp-v1.md`（工作区外） |
| 硬件引脚分配 | `../resource/小白IO分配.xlsx`（工作区外） |
| 语音协议 | `../resource/语音芯片交互协议.md`（工作区外） |

---

## 开发约定

- 裸机非阻塞：时间基准只用 `Bsp_Tick_GetMs()`，事件靠主循环轮询 + 中断置标志，禁止长阻塞
- 命名：函数 `Bsp_<模块>_<动作>`，类型 `Bsp_Xxx_Type_t`，枚举值大写；注释中文、代码英文
- 状态用 `static const` 查表驱动，枚举与表顺序严格对齐
- 分支工作流：日常开发在 `main-work`，完成里程碑合并回 `main` 并推送
- 质量门禁：**编译 0 Error 0 Warning + 代码审查 + 板上实测**

## License

MIT
