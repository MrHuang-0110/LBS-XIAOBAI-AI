# 小白-Ai 主控固件四层架构重构设计

- 日期：2026-08-04
- 分支：`main-work`（完成后合并回 `main`）
- 范围：纯分层重构，**行为零变化**；遥控帧解析功能保留（只挪位置），感应阈值/时序数值全部保持现状
- 基调：TinyTask 等调度框架**不引入**，继续裸机 `while(1)` 轮询 + 状态机

## 1. 背景与动机

现状分层名义上是"App / BSP / HAL 三层"，但实际耦合严重：

1. `User/main.c`（28.6KB 单文件）混杂五类职责：应用状态机与全部业务逻辑、遥控帧协议解析（`stream[]` 缓冲 + 帧校验）、表现层动画（TM1640 眼睛 / PA9 呼吸灯）、语音命令→播报映射表（`cmd_to_voice[]`）、系统时钟配置。
2. **协议层整体缺失**：语音协议 ID 宏（`ASR_VOICE_*`/`ASR_CMD_*`）躺在驱动层 `Bsp_UartAsr.h`，命令→播报映射表躺在应用层；遥控帧解析完全写在 `main.c` 的 `while(1)` 里。
3. 应用逻辑与驱动直接粘连：各模式直接拼 `Bsp_Motor_Set(左/右)`，动力/感应/遥控三处重复电机组合代码。

目标：应用层 / 驱动层 / 协议层 / 底层完全区分，代码可读、稳定、可维护。

## 2. 目标分层

依赖方向单向：`App → Protocol → BSP → HAL`。App 不直接碰 BSP 的协议性接口；各模式动作统一经 `App_Vehicle` 走电机。

```
User/                       应用层（现有目录，内部重组）
  main.c                    精简为入口：HAL_Init → App_System_Init → App_Init → App_Loop
  App_Main.c/h              主循环框架 + 全局事件分发（按键→语音→BLE→各模式）
  App_Mode.c/h              模式枚举 + SwitchMode + 映射表（mode_led/mode_voice/sensor_voice/act_voice）
  App_Mode_Voice.c/h        语音模式（11 条动作命令执行）
  App_Mode_Power.c/h        动力模式（5 动作驱动）
  App_Mode_Sensor.c/h       感应模式（4 玩法 + 挥手边沿）
  App_Mode_Remote.c/h       遥控模式（消费有效帧、肩键档位、超时停机）
  App_Vehicle.c/h           车辆动作统一接口（新增，消除三处重复电机组合）
  App_Eye.c/h               TM1640 眼睛动画（图案表 + 动画状态机）
  App_Breath.c/h            呼吸灯动画（wake 启动 / 15s 超时）
  App_Shutdown.c/h          关机流程（PerformShutdown）
  App_System.c/h            时钟配置（APP_SystemClockConfig 从 main.c 抽出）
  （py32f0xx_it.c / py32f0xx_hal_conf.h / system_py32f0xx.c / main.h 保留原位）
Protocol/                   协议层（新建目录）
  Proto_Asr.h/c             语音协议：ASR_VOICE_*/ASR_CMD_* 宏（从 Bsp_UartAsr.h 上移）
                           + cmd_to_voice[] 映射表 + Proto_Asr_CmdToVoice() 查询
  Proto_Remote.h/c          遥控帧协议：stream 缓冲 + 帧校验 + 按键位图提取
                           （从 main.c 抽出，输出"有效帧"，边沿/动作仍归应用层）
BSP_Drivers/                驱动层（现状保留，每外设一子目录）
  Bsp_UartAsr.c/h           只留 ASCII 帧解析 + DMA 收发 + 事件队列（删除 ID 宏，其余不动）
  ...（其他 Bsp_* 不变）
PY32F0xx_HAL_Driver/ + CMSIS/   底层（供应商代码，不动）
```

方案对比（已选定方案 A）：
- A（选定）：目录四层 + 应用层多模块，Keil 工程只加一个 Protocol Group，风险最小。
- B：激进改名 `User/→App/`、`BSP_Drivers/→Drivers/`，工程路径大改，收益低、风险高，排除。
- C：只抽遥控帧解析，main.c 不拆，分层不彻底，排除。

## 3. 模块职责与接口

**App_Main（调度框架）** — 唯一保留"轮询编排"职责的文件：

```c
while(1) {
    App_Key_Handler()        /* 按键事件 → 模式切换/关机 */
    App_Asr_Handler()        /* 语音事件 → 关机/切模式/动作 */
    App_Ble_Handler()        /* 连接边沿播报 + 遥控帧喂给 App_Mode_Remote */
    App_Mode_Update()        /* 各模式驱动（内部按 g_mode 分发） */
    App_Eye_Update()         /* 眼睛动画 */
    App_Breath_Update()      /* 呼吸灯 */
    App_Battery_Update()     /* 电池轮询 + 低电量播报 */
    Bsp_Tick_DelayMs(5)
}
```

| 模块 | 对外接口 | 依赖 |
|---|---|---|
| `App_Vehicle` | `Vehicle_Drive(dir, speed)`（dir ∈ STOP/FWD/BACK/LEFT/RIGHT，LEFT/RIGHT 为坦克转向）+ `Vehicle_DriveSingle(motor, dir, speed)`（单电机，遥控模式用） | `Bsp_Motor` |
| `App_Mode_*` | `Mode_Enter() / Mode_Update() / Mode_Exit()` | `App_Vehicle`, `Proto_Asr`, `Proto_Remote` |
| `Proto_Asr` | `Proto_Asr_CmdToVoice(cmd)`、`ASR_*` 宏 | `Bsp_UartAsr`（事件已含 cmd/done/wake） |
| `Proto_Remote` | `Proto_Remote_FeedByte() / Proto_Remote_GetFrame()`（有效帧含 10 键位图） | `Bsp_UartBle_TryRecv` |
| `App_Eye` / `App_Breath` / `App_Shutdown` | `Eye_Update() / Breath_Update() / Shutdown()` | `Bsp_Tm1640`, `Bsp_LedPwm`, `Proto_Asr`, `Bsp_Power` |

## 4. 行为保持红线（重构验收锚点）

- `cmd_to_voice[]`、`mode_led[]`/`mode_voice[]`/`sensor_voice[]`/`act_voice[]` 表的**内容和顺序原样搬迁**，只换宿主文件。
- 遥控帧 `stream[]` 缓冲、帧校验、按键位图提取逻辑原样搬进 `Proto_Remote`；肩键边沿、方向键/单电机键映射、1s 超时停机留在 `App_Mode_Remote`。
- `ASR_EVT_*` 事件类型、`Bsp_UartAsr_TryRecv` 机制不动；ID 宏与映射表上移 `Proto_Asr`。
- 时序数值不变：1500ms ASRPRO 就绪、500ms BLE 配名、15s 呼吸灯超时、5ms 主循环节拍、10ms 电池采样、1s 遥控超时。
- 感应阈值 `IR_THRESHOLD=3000`、明暗调速 500/1000 阈值、挥手 500ms 消抖均不变。

## 5. 错误处理（沿用现状）

- 电源锁存失败：`Bsp_Power` 内部 `while(1){}` 死循环，不动。
- HAL 初始化失败：`APP_ErrorHandler()` 死循环，位置移至 `App_System.c`。
- 协议解析失败（CRC 错/帧头不匹配）：`Proto_Remote` 沿用"跳过字节继续找帧头"，行为不变。
- 语音事件未知 ID：`Proto_Asr_CmdToVoice()` 返回 0 / 空操作，不新增防御分支。

## 6. Keil 工程变更（MDK-ARM/XiaoBai.uvprojx）

- 新增 `Protocol` Group：`Proto_Asr.c`、`Proto_Remote.c`。
- `User` Group 加入新增 `App_*.c`（App_Main/App_Mode/App_Mode_Voice/App_Mode_Power/App_Mode_Sensor/App_Mode_Remote/App_Vehicle/App_Eye/App_Breath/App_Shutdown/App_System）。
- `Bsp_UartAsr.h` 删除 ID 宏定义区（宏上移 Proto_Asr）；其余 BSP 文件不动。
- 保持 AC5（`uAC6=0`）、C99、宏 `USE_HAL_DRIVER,PY32F030x6` 不变。

## 7. 无用文件清理（重构收尾）

- 用 grep 做全工程引用扫描，确认无引用的源文件/头文件/宏/表才删除；
- 删除清单与依据记入 `doc/04-开发进度.md`；
- 注意共用依赖不可误删（如 `Bsp_Adc` 被 Battery/IR 共用）。

## 8. 验证策略（TDD 落地，无宿主测试框架）

1. **验收清单**（重构前写好，逐条核对）：
   - 编译 0 Error 0 Warning；
   - 开机：BOOT 语 → 语音模式 LED1 → 呼吸灯 off；
   - 按键：4 键切模式、KEY2/KEY4 模式内切玩法/动作、KEY1 长按关机；
   - 语音：关机、切模式、11 条动作命令播报 ID 正确（对照 cmd_to_voice 表）；
   - BLE：连接/断开播报、前进/后退/转向/单电机/肩键调速/1s 超时停机；
   - 感应：4 玩法行为与旧固件一致；
   - 眼睛/呼吸动画时序与旧固件一致。
2. **编译门禁**：每完成一个模块即编译，不攒到最后。
3. **板上实测**：编译通过后烧录，按验收清单逐项对比旧固件行为。

## 9. 提交策略（符合 AGENTS.md 分支工作流）

- 全部在 `main-work` 分支进行；
- 顺序：协议层 → 驱动层瘦身 → App 各模块 → main.c 瘦身，每步可编译；
- 完成后 `git checkout main` → `git merge main-work` → 推送，切回 `main-work`。

## 10. 明确不做（YAGNI）

- 不引入 TinyTask / 任何 RTOS / 调度框架；
- 不改感应阈值、不做量产校准、不补测试框架；
- 不重命名现有 BSP 目录；
- 不新增功能。
