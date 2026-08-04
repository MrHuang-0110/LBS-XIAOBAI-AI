# AGENTS.md — 小白-Ai 主控固件

「小白-Ai」AI 玩具小车的主控 MCU 固件（STM32 风格裸机程序）。主控为普冉 PY32F030K28U6TR（Cortex-M0+，Flash 32KB，RAM 4KB），语音识别（ASRPRO 串口协议 v0.7）、BLE 遥控、4 种玩法模式（语音/动力/感应/遥控）。Git：远程 `origin` = `https://github.com/MrHuang-0110/LBS-XIAOBAI-AI.git`，默认分支 `main`；`.gitignore` 已排除 Keil 构建产物，**提交前用 `git status` 确认无 `MDK-ARM/Output`、`*.o/*.hex/*.map` 等产物入库**。

项目记忆库在 `doc/`（`doc/README.md` 为索引：框架/技术栈/踩坑/进度）。**task 开始前先读相关记忆，task 结束后必须更新。**

## Rules（硬性门禁 — 每次 task / 每次代码改动必须依次通过）

1. **审查（Review）**：改动后用 code review 检查正确性/边界/资源占用；涉及串口协议解析、中断、电源、IO 的改动另做安全审查（security review）。
2. **TDD（测试先行）**：实现前先写下可验证的行为断言/验收清单；实现后逐条验证。本工程无宿主测试框架（Keil AC5 嵌入式），TDD 落地为：**验收清单 + 编译 0 Error 0 Warning + 板上实测**；若为可剥离的纯逻辑，优先抽离并配 host 测试（引入框架前先记入 `doc/04` 待办）。
3. **Code Simple 优化**：用 code-simplifier 检查并简化——删除死代码/重复/过度设计，保持最小可维护实现；简化后重新过 1、2 两项。

纯文档/流程类改动（如本记忆系统）：以文档一致性复核代替代码门禁，仍须记录到 `doc/04-开发进度.md`。

## Commands

无命令行构建脚本，只能通过 Keil uVision5 构建（ARM Compiler **V5.06**，AC5，勿切换 AC6）：

- 打开工程：`MDK-ARM/XiaoBai.uvprojx`（Target: `PY32F030_Project`），uVision 里 F7 编译 / F8 烧录
- 命令行编译（需 Keil 安装路径中的 `UV4.exe`）：`UV4 -b "MDK-ARM/XiaoBai.uvprojx" -j0 -o build.log`
- 产物：`MDK-ARM/Output/XiaoBai.hex`（烧录）、`XiaoBai.axf`、`XiaoBai.map`
- 无测试框架；"验证"= 编译 0 Error 0 Warning + 板上实测
- 编译宏：`USE_HAL_DRIVER,PY32F030x6`；C99

## Architecture

- `User/main.c` — 唯一应用层，约 28KB：4 模式状态机（`App_Mode_t`）、主循环 `while(1)` 轮询按键/语音事件、LED 映射与 TM1640 眼睛动画、关机/低电量流程。入口 `main()` = `HAL_Init()` → `APP_SystemClockConfig()` → `BSP_Init()` → 模式循环
- `BSP_Drivers/` — 板级驱动，每个外设一个子目录（`Bsp_Tick/Led/Power/LedPwm/Key/Motor/Adc/IR/Battery/UartAsr/UartBle/Tm1640`）；`Bsp.c` 的 `BSP_Init()` 按固定顺序汇总初始化（电源锁存失败 `while(1)` 死循环）
- `PY32F0xx_HAL_Driver/` — 普冉官方 HAL/LL 库（`Inc`/`Src`），供应商代码，一般不改
- `CMSIS/` — ARM CMSIS 头文件、DSP Lib、启动文件 `Device/PY32F0xx/Source/arm/startup_py32f030x6.s`
- `User/py32f0xx_it.c` — 中断入口，转发给 BSP 的 `IRQHandler`（如 `Bsp_UartAsr_UART_IRQHandler`）
- `User/py32f0xx_hal_conf.h` — HAL 模块开关（`HAL_*_MODULE_ENABLED`），HSE/HSI 24MHz

## Conventions

- 注释用中文（含需求背景、日期、坑），代码/标识符用英文；关键决策处写清"为什么"
- BSP 命名：函数 `Bsp_<模块>_<动作>`（`Bsp_UartAsr_SendPlay`）、类型 `Bsp_Xxx_Type_t`、枚举值大写（`ASR_CMD_*`、`KEY_EVT_*`）；头文件 include guard `__BSP_X_H`
- 只依赖 HAL 标准 API（`HAL_UART_Transmit` 等），新增外设驱动时在 `BSP_Drivers/Bsp_<X>/` 建子目录并加入 `Bsp.h` 汇总 include 和 Keil 工程
- 裸机非阻塞：时间基准用 `Bsp_Tick_GetMs()`/`Bsp_Tick_DelayMs()`；事件靠主循环轮询（`Bsp_Key_Poll`、`Bsp_UartAsr_TryRecv`）+ 中断置标志，禁止长阻塞
- 状态用 `static const` 查表驱动（`mode_led[]`、`cmd_to_voice[]` 等），枚举与表顺序严格对齐
- 语音协议版本 v0.7：命令 ID 1-16、播报 ID 17-41 不重叠；改 ID 必须同步核对 `cmd_to_voice` 映射表和 `Bsp_UartAsr.h` 宏
- 新增 BSP 时在 `MDK-ARM/XiaoBai.uvprojx` 的 `Bsp_Drivers` Group 里加 `<File>` 条目，否则编译不过
- 资源紧张（32KB Flash / 4KB RAM）：警惕大数组与 `printf` 全家桶
- 外部文档在工作区外：`../docs/plan-bsp-v1.md`、`../resource/小白IO分配.xlsx`、`../resource/语音芯片交互协议.md`（代码注释中以 `resource/` 相对引用）

## Notes

- 详细记忆见 `doc/`：框架 `01`、技术栈 `02`、踩坑 `03`、进度 `04`；本文件只放高频要点。
- 踩坑先查 `doc/03-踩坑记录.md`（如 LED 枚举名≠物理引脚、语音 ID 映射、AC5 勿切 AC6）。
