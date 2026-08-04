# 小白-Ai 主控固件四层架构重构 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将小白-Ai 主控固件从"2 层 + 巨型 main.c"重构为 App / Protocol / BSP / HAL 四层清晰架构，行为零变化。

**Architecture:** 依赖单向 `App → Protocol → BSP → HAL`。新建 `Protocol/` 层收纳语音协议（Proto_Asr：ID 宏 + cmd_to_voice 映射）与遥控帧协议（Proto_Remote：帧校验 + 按键位图）；`User/` 内把 28.6KB main.c 拆成 11 个职责单一模块；BSP 保持现有子目录结构（仅删 ID 宏）；HAL/CMSIS 供应商代码不动。

**Tech Stack:** Keil uVision5 + ARM Compiler V5.06 (AC5)，C99，宏 `USE_HAL_DRIVER,PY32F030x6`；PY32F030K28U6TR（Flash 32KB / RAM 4KB）；无宿主测试框架（TDD 落地 = 验收清单 + 编译 0E0W + 板上实测）。

## Global Constraints

- **行为零变化**：所有映射表（cmd_to_voice/mode_led/mode_voice/sensor_voice/act_voice）内容与顺序原样搬迁；所有时序数值不变（1500ms ASRPRO 就绪 / 500ms BLE 配名 / 15s 呼吸灯 / 5ms 主循环节拍 / 10ms 电池采样 / 1s 遥控超时 / 500ms 挥手消抖）；感应阈值不变（IR_THRESHOLD=3000、明暗 500/1000/3000）。
- **不引入** TinyTask / RTOS / 宿主测试框架；不改感应阈值；不重命名现有 BSP 目录；不加功能。
- **编译门禁**：每任务完成后必须编译 0 Error 0 Warning（UV4 命令行或 Keil GUI F7）。命令行：`UV4 -b MDK-ARM/XiaoBai.uvprojx -j0 -o build.log`（UV4 需在 PATH 或 Keil 安装目录）。
- **分支**：全部在 `main-work` 分支；每任务独立 commit；完成后合并回 `main`。
- **依赖方向**：App 只 include `Proto_Asr.h`/`Proto_Remote.h`/`Bsp.h`；Protocol 只 include `Bsp_*.h`/HAL；任何 App→BSP 直接调协议性接口（SendPlay）必须经 Proto_Asr 语义封装（本计划中 SendPlay 仍直调 Bsp_UartAsr_SendPlay，由 App 各模块调用，但宏与映射统一经 Proto_Asr 查询）。

---

### Task 1: 创建协议层 Proto_Asr（语音 ID 宏 + cmd_to_voice 映射上移）

**Files:**
- Create: `Protocol/Proto_Asr.h`
- Create: `Protocol/Proto_Asr.c`
- Modify: `BSP_Drivers/Bsp_UartAsr/Bsp_UartAsr.h`（删除 ASR_VOICE_*/ASR_CMD_* 宏定义区，保留事件类型与函数声明）
- Modify: `User/main.c`（删除 cmd_to_voice 表，改用 `Proto_Asr_CmdToVoice()`）
- Modify: `MDK-ARM/XiaoBai.uvprojx`（新增 Protocol Group）

**Interfaces:**
- Consumes: 无（纯协议常量 + 查表，不依赖 BSP）
- Produces: `Proto_Asr.h` 定义全部 `ASR_VOICE_*`(17-41)/`ASR_CMD_*`(1-16) 宏；`uint8_t Proto_Asr_CmdToVoice(uint8_t cmd)`（cmd∈[6,16] 返回播报 ID，其余返回 0）

- [ ] **Step 1: 创建 `Protocol/Proto_Asr.h`**

```c
#ifndef __PROTO_ASR_H
#define __PROTO_ASR_H
#include <stdint.h>

/* ===== 语音芯片交互协议 v0.7 =====
 * 权威文档：resource/语音芯片交互协议.md（工作区外）。
 * 命令 ID ∈ [1,16]，播报 ID ∈ [17,41]，两区间不重叠。
 * 改协议 ID 时必须同步核对本文件宏 与 Proto_Asr.c 的 cmd_to_voice 表。 */

/* --- 播报 ID（v0.7 §三 17-41 连续） --- */
#define ASR_VOICE_BOOT           17  /* 你好呀我是小白进入语音模式（开机语） */
#define ASR_VOICE_SHUTDOWN       18  /* 再见啦，小白去休息了（关机语） */
#define ASR_VOICE_ENTER_POWER    19  /* 进入动力模式 */
#define ASR_VOICE_ENTER_SENSOR   20  /* 进入感应模式 */
#define ASR_VOICE_ENTER_REMOTE   21  /* 进入遥控模式 */
#define ASR_VOICE_ENTER_VOICE    22  /* 进入语音模式 */
#define ASR_VOICE_BLE_CONNECTED  23  /* 遥控已连接 */
#define ASR_VOICE_BLE_LOST       24  /* 遥控已断开 */
#define ASR_VOICE_LOW_BATTERY    25  /* 低电量 */
#define ASR_VOICE_RECEIVED       26  /* 收到 */
#define ASR_VOICE_FORWARD        27  /* 前进 */
#define ASR_VOICE_BACKWARD       28  /* 后退 */
#define ASR_VOICE_LEFT           29  /* 左转 */
#define ASR_VOICE_RIGHT          30  /* 右转 */
#define ASR_VOICE_STOP           31  /* 停止 */
#define ASR_VOICE_APPROACH_GO    32  /* 靠近启动 */
#define ASR_VOICE_OBSTACLE_STOP  33  /* 遇障停止 */
#define ASR_VOICE_WAVE_TOGGLE    34  /* 挥手开关 */
#define ASR_VOICE_BRIGHTNESS     35  /* 明暗调速 */
#define ASR_VOICE_L_FWD          36  /* 左电机正转 */
#define ASR_VOICE_L_REV          37  /* 左电机反转 */
#define ASR_VOICE_L_STOP         38  /* 左电机停止 */
#define ASR_VOICE_R_FWD          39  /* 右电机正转 */
#define ASR_VOICE_R_REV          40  /* 右电机反转 */
#define ASR_VOICE_R_STOP         41  /* 右电机停止 */

/* --- 命令 ID（v0.7 §四 1-16 连续） --- */
#define ASR_CMD_SHUTDOWN         1   /* 关机 */
#define ASR_CMD_ENTER_POWER      2
#define ASR_CMD_ENTER_SENSOR     3
#define ASR_CMD_ENTER_REMOTE     4
#define ASR_CMD_ENTER_VOICE      5
#define ASR_CMD_FORWARD          6
#define ASR_CMD_BACKWARD         7
#define ASR_CMD_LEFT             8
#define ASR_CMD_RIGHT            9
#define ASR_CMD_STOP             10
#define ASR_CMD_L_FWD            11
#define ASR_CMD_L_REV            12
#define ASR_CMD_L_STOP           13
#define ASR_CMD_R_FWD            14
#define ASR_CMD_R_REV            15
#define ASR_CMD_R_STOP           16

/**
 * @brief 语音命令 ID → 播报 ID 映射查询。
 * @param cmd ASR_CMD_* 命令 ID（仅 6..16 有映射）
 * @return 对应 ASR_VOICE_* 播报 ID；无映射返回 0。
 */
uint8_t Proto_Asr_CmdToVoice(uint8_t cmd);

#endif
```

- [ ] **Step 2: 创建 `Protocol/Proto_Asr.c`**

```c
#include "Proto_Asr.h"

/* 语音命令 ID (6..16) → 播报 ID (27..31 / 36..41) 映射。
 * 索引 = cmd_id - ASR_CMD_FORWARD (6)。宏值由 v0.7 协议 §三/§四 决定；
 * 若协议再调 ID，此表须与 Proto_Asr.h 宏同步核对（踩坑记录：唯一权威映射）。 */
static const uint8_t cmd_to_voice[11] = {
    ASR_VOICE_FORWARD,   /* cmd=6  → play=27 */
    ASR_VOICE_BACKWARD,  /* cmd=7  → play=28 */
    ASR_VOICE_LEFT,      /* cmd=8  → play=29 */
    ASR_VOICE_RIGHT,     /* cmd=9  → play=30 */
    ASR_VOICE_STOP,      /* cmd=10 → play=31 */
    ASR_VOICE_L_FWD,     /* cmd=11 → play=36 */
    ASR_VOICE_L_REV,     /* cmd=12 → play=37 */
    ASR_VOICE_L_STOP,    /* cmd=13 → play=38 */
    ASR_VOICE_R_FWD,     /* cmd=14 → play=39 */
    ASR_VOICE_R_REV,     /* cmd=15 → play=40 */
    ASR_VOICE_R_STOP,    /* cmd=16 → play=41 */
};

uint8_t Proto_Asr_CmdToVoice(uint8_t cmd)
{
    if (cmd < ASR_CMD_FORWARD || cmd > ASR_CMD_R_STOP) return 0;
    return cmd_to_voice[cmd - ASR_CMD_FORWARD];
}
```

- [ ] **Step 3: 修改 `BSP_Drivers/Bsp_UartAsr/Bsp_UartAsr.h` — 删除宏定义区（原 9-52 行），保留其余**

删除从 `/* --- 语音 ID（十进制，供 Bsp_UartAsr_SendPlay 使用；v0.7 §三 17-41 连续） --- */` 到 `#define ASR_CMD_R_STOP           16` 的全部内容。文件头部注释更新为：

```c
/* 语音芯片交互协议 v0.7（ASCII 文本；两方向均带帧尾：MCU 发帧尾 `\n`，收帧尾 `\r\n`；
   tag 用 `=` 分隔十进制数值）。
   ★ 协议 ID 宏（ASR_VOICE_*/ASR_CMD_*）与 cmd_to_voice 映射已上移到 Protocol/Proto_Asr.h，
   本驱动只负责 ASCII 帧解析 / DMA 收发 / 事件队列，不持有协议 ID。 */
```

保留：`Bsp_UartAsr_EvtType_t`、`Bsp_UartAsr_Event_t`、`Bsp_UartAsr_Init/SendPlay/SendStop/SendPing/SendRaw/TryRecv/UART_IRQHandler/DMA_IRQHandler` 声明（原 54-93 行）原样不动。

- [ ] **Step 4: 修改 `User/main.c` — 删表 + 改调用**

删除 `static const uint8_t cmd_to_voice[11] = {...};`（原 64-76 行）。文件头部 include 增加：

```c
#include "main.h"
#include "Bsp.h"
#include "Proto_Asr.h"
```

将语音动作命令段的播报调用（原 343 行）：
```c
Bsp_UartAsr_SendPlay(cmd_to_voice[e.arg - ASR_CMD_FORWARD]);
```
替换为：
```c
Bsp_UartAsr_SendPlay(Proto_Asr_CmdToVoice(e.arg));
```

- [ ] **Step 5: 修改 `MDK-ARM/XiaoBai.uvprojx` — 新增 Protocol Group**

在 `</Group>`（Bsp_Drivers Group 之后、`<GroupName>PY32F0xx_HAL_Driver</GroupName>` Group 之前）插入：

```xml
<Group>
  <GroupName>Protocol</GroupName>
  <Files>
    <File>
      <FileName>Proto_Asr.c</FileName>
      <FileType>1</FileType>
      <FilePath>..\Protocol\Proto_Asr.c</FilePath>
    </File>
  </Files>
</Group>
```

- [ ] **Step 6: 编译验证**

Run: `UV4 -b MDK-ARM/XiaoBai.uvprojx -j0 -o build.log`（或 Keil GUI F7）
Expected: 0 Error 0 Warning；`build.log` 无错误。若 UV4 不在 PATH，用 Keil GUI 编译并在 GUI 底部确认 0 Error 0 Warning。

- [ ] **Step 7: 逻辑对照验收（cmd_to_voice 表）**

Manual: 对照原 main.c 表与 Proto_Asr.c 表逐项核对：cmd 6→27, 7→28, 8→29, 9→30, 10→31, 11→36, 12→37, 13→38, 14→39, 15→40, 16→41。宏值对照 Bsp_UartAsr.h 旧定义与 Proto_Asr.h 逐条一致。

- [ ] **Step 8: Commit**

```bash
git add Protocol/ BSP_Drivers/Bsp_UartAsr/Bsp_UartAsr.h User/main.c MDK-ARM/XiaoBai.uvprojx
git commit -m "refactor: 语音协议上移协议层（Proto_Asr：ID 宏 + cmd_to_voice 映射），驱动层瘦身"
```

---

### Task 2: 创建协议层 Proto_Remote（遥控帧解析迁出 main.c）

**Files:**
- Create: `Protocol/Proto_Remote.h`
- Create: `Protocol/Proto_Remote.c`
- Modify: `BSP_Drivers/Bsp_UartBle/Bsp_UartBle.h`（删除 REMOTE_FRAME_*/REMOTE_KEY_COUNT 宏与 Bsp_RemoteKey_t 枚举，保留收发/连接 API）
- Modify: `User/main.c`（遥控解析块改调 Proto_Remote）
- Modify: `MDK-ARM/XiaoBai.uvprojx`（Protocol Group 加 Proto_Remote.c）

**Interfaces:**
- Consumes: 无（纯字节流解析）
- Produces:
  - `void Proto_Remote_Init(void)` — 清空内部 stream 缓冲与游标
  - `void Proto_Remote_Feed(const uint8_t *buf, uint16_t len)` — 喂入新收字节
  - `uint8_t Proto_Remote_GetFrame(uint8_t keys_out[REMOTE_KEY_COUNT])` — 从缓冲取出一帧有效帧填按键位图，返回 1；无完整有效帧返回 0
  - 宏 `REMOTE_FRAME_HEAD/TAIL/LEN`、`REMOTE_KEY_COUNT`、枚举 `Bsp_RemoteKey_t`（REMOTE_KEY_UP/DOWN/LEFT/RIGHT/Y/A/X/B/R1/L1）原样搬入 `Proto_Remote.h`

- [ ] **Step 1: 创建 `Protocol/Proto_Remote.h`**

```c
#ifndef __PROTO_REMOTE_H
#define __PROTO_REMOTE_H
#include <stdint.h>

/* ===== 遥控器帧协议（resource/遥控协议.md，工作区外） =====
 * 帧格式: 5A 97 98 0A C1 [10 字节键值位图] CRC A5
 *   5A    帧头
 *   97    源地址
 *   98    目标地址
 *   0A    数据长度（固定 10）
 *   C1    数据类型码
 *   xx*10 10 字节键值位图，每键 1 字节，0=未按 1=按下
 *   CRC   从帧头到数据位最后一位的累加和取低 8 位
 *   A5    帧尾
 * 总长 17 字节。 */

#define REMOTE_FRAME_HEAD   0x5AU
#define REMOTE_FRAME_TAIL   0xA5U
#define REMOTE_FRAME_LEN    17U   /* 5A 97 98 0A C1 + 10数据 + CRC + A5 = 17 */
#define REMOTE_KEY_COUNT    10U

/* 按键枚举（跟遥控协议.md 的 enum 顺序一致，对应字节位图 [0..9]） */
typedef enum {
    REMOTE_KEY_UP    = 0,   /* KeyUp    方向上 */
    REMOTE_KEY_DOWN  = 1,   /* KeyDown  方向下 */
    REMOTE_KEY_LEFT  = 2,   /* KeyLeft  方向左 */
    REMOTE_KEY_RIGHT = 3,   /* KeyRight 方向右 */
    REMOTE_KEY_Y     = 4,
    REMOTE_KEY_A     = 5,
    REMOTE_KEY_X     = 6,
    REMOTE_KEY_B     = 7,
    REMOTE_KEY_R1    = 8,   /* R1Key */
    REMOTE_KEY_L1    = 9,   /* L1Key */
} Bsp_RemoteKey_t;

/** 清空帧流缓冲（开机时调用一次） */
void Proto_Remote_Init(void);

/** 喂入新收字节（从 Bsp_UartBle_TryRecv 的返回值） */
void Proto_Remote_Feed(const uint8_t *buf, uint16_t len);

/**
 * @brief 从缓冲取出一帧有效帧（帧头/CRC/帧尾全校验通过）。
 * @param keys_out 输出 10 字节按键位图（REMOTE_KEY_* 索引）
 * @return 1 = 取到一帧；0 = 缓冲中没有完整有效帧
 */
uint8_t Proto_Remote_GetFrame(uint8_t keys_out[REMOTE_KEY_COUNT]);

#endif
```

- [ ] **Step 2: 创建 `Protocol/Proto_Remote.c`**

```c
#include "Proto_Remote.h"

/* 帧流缓冲：最多容纳 4 帧待处理数据（原 main.c 的 stream[REMOTE_FRAME_LEN*4]） */
static uint8_t  s_stream[REMOTE_FRAME_LEN * 4];
static uint16_t s_len = 0;

void Proto_Remote_Init(void)
{
    s_len = 0;
}

void Proto_Remote_Feed(const uint8_t *buf, uint16_t len)
{
    for (uint16_t k = 0; k < len; k++) {
        if (s_len < sizeof(s_stream)) s_stream[s_len++] = buf[k];
    }
}

uint8_t Proto_Remote_GetFrame(uint8_t keys_out[REMOTE_KEY_COUNT])
{
    uint16_t i = 0;
    while (i + REMOTE_FRAME_LEN <= s_len) {
        if (s_stream[i] != REMOTE_FRAME_HEAD) { i++; continue; }
        if (s_stream[i+1] != 0x97 || s_stream[i+2] != 0x98 ||
            s_stream[i+3] != 0x0A || s_stream[i+4] != 0xC1) { i++; continue; }
        if (s_stream[i + REMOTE_FRAME_LEN - 1] != REMOTE_FRAME_TAIL) { i++; continue; }
        uint8_t crc = 0;
        for (uint16_t j = 0; j < REMOTE_FRAME_LEN - 2; j++) crc += s_stream[i + j];
        if (crc != s_stream[i + REMOTE_FRAME_LEN - 2]) { i++; continue; }
        /* 帧有效：拷出按键位图（帧内偏移 5..14） */
        for (uint8_t k = 0; k < REMOTE_KEY_COUNT; k++) {
            keys_out[k] = s_stream[i + 5 + k];
        }
        /* 消费本帧，剩余字节左移（原 main.c 的搬移逻辑） */
        uint16_t remain = s_len - (i + REMOTE_FRAME_LEN);
        for (uint16_t k = 0; k < remain; k++) s_stream[k] = s_stream[i + REMOTE_FRAME_LEN + k];
        s_len = remain;
        return 1;
    }
    /* 没有完整帧：丢弃已扫描过的无效前缀（i>0 时左移），保持缓冲不膨胀 */
    if (i > 0) {
        uint16_t remain = s_len - i;
        for (uint16_t k = 0; k < remain; k++) s_stream[k] = s_stream[i + k];
        s_len = remain;
    }
    return 0;
}
```

> 注：原 main.c 只在"有 i>0 时搬移"，且边解析边搬移。本实现将"消费一帧后搬移"与"扫完无帧后丢弃前缀"合并，语义等价：每字节最多被扫描一次，缓冲上限 4 帧不变，行为零变化。

- [ ] **Step 3: 修改 `BSP_Drivers/Bsp_UartBle/Bsp_UartBle.h` — 删除协议宏与枚举**

删除从 `/* --- 遥控器协议（resource/遥控协议.md） --- */` 到 `} Bsp_RemoteKey_t;`（原 18-47 行）。头部注释改为：

```c
/*
 * USART1 (PB6=TX / PB7=RX, AF0) <-> ECB00CV2 BLE 芯片
 *   波特率 9600 8N1（ECB00 默认，datasheet 第 10 页）
 *   PF3 = STA 引脚，下拉输入（datasheet 第 4 页要求）
 *
 * ECB00 工作模式：默认就是从机透传，无需 AT 配置主从。
 *   本驱动只负责原始字节收发 + PF3 连接电平检测，不解析遥控帧；
 *   遥控帧协议（帧格式/按键枚举）见 Protocol/Proto_Remote.h。
 */
```

保留 `BLE_RX_BUF_SIZE`、`Bsp_UartBle_Init/ConfigName/Send/TryRecv/IsConnected/UART_IRQHandler/DMA_IRQHandler`（原 16、49-76 行）原样不动。

- [ ] **Step 4: 修改 `User/main.c` — 遥控解析块改调 Proto_Remote**

头部 include 增加 `#include "Proto_Remote.h"`。

删除 `static uint8_t stream[REMOTE_FRAME_LEN * 4]; uint16_t stream_len = 0;`（原 267-268 行），替换为 `Proto_Remote_Init();`。

将 `/* --- BLE 遥控帧解析（M3 接电机，先解析占位） --- */` 整个块（原 400-459 行）替换为：

```c
        /* --- BLE 遥控帧解析（协议层校验，应用层消费） --- */
        {
            uint8_t buf[REMOTE_FRAME_LEN * 2];
            uint16_t n = Bsp_UartBle_TryRecv(buf, sizeof(buf));
            Proto_Remote_Feed(buf, n);
            uint8_t keys[REMOTE_KEY_COUNT];
            while (Proto_Remote_GetFrame(keys)) {
                if (g_mode == APP_MODE_REMOTE) {
                    /* 肩键调速（边沿触发）：R1=速度+，L1=速度- */
                    if (keys[REMOTE_KEY_R1] && !g_r1_was) {
                        if (g_remote_speed < MOTOR_SPEED_HIGH) g_remote_speed++;
                    }
                    if (keys[REMOTE_KEY_L1] && !g_l1_was) {
                        if (g_remote_speed > MOTOR_SPEED_LOW) g_remote_speed--;
                    }
                    g_r1_was = keys[REMOTE_KEY_R1];
                    g_l1_was = keys[REMOTE_KEY_L1];
                    /* 方向键优先（坦克转向），否则单电机键 */
                    if (keys[REMOTE_KEY_UP]) {
                        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  g_remote_speed);
                        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  g_remote_speed);
                    } else if (keys[REMOTE_KEY_DOWN]) {
                        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, g_remote_speed);
                        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, g_remote_speed);
                    } else if (keys[REMOTE_KEY_LEFT]) {
                        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, g_remote_speed);
                        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  g_remote_speed);
                    } else if (keys[REMOTE_KEY_RIGHT]) {
                        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  g_remote_speed);
                        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, g_remote_speed);
                    } else {
                        /* 单电机：Y=L正转 A=L反转 X=R正转 B=R反转 */
                        if (keys[REMOTE_KEY_Y])      Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  g_remote_speed);
                        else if (keys[REMOTE_KEY_A]) Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, g_remote_speed);
                        else                         Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_STOP,     g_remote_speed);
                        if (keys[REMOTE_KEY_X])      Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  g_remote_speed);
                        else if (keys[REMOTE_KEY_B]) Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, g_remote_speed);
                        else                         Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_STOP,     g_remote_speed);
                    }
                    g_last_remote_frame = Bsp_Tick_GetMs();
                }
            }
        }
```

- [ ] **Step 5: 修改 `MDK-ARM/XiaoBai.uvprojx` — Protocol Group 加 Proto_Remote.c**

在 Task 1 插入的 Protocol Group 内、`</Files>` 前追加：

```xml
    <File>
      <FileName>Proto_Remote.c</FileName>
      <FileType>1</FileType>
      <FilePath>..\Protocol\Proto_Remote.c</FilePath>
    </File>
```

- [ ] **Step 6: 编译验证**

Run: `UV4 -b MDK-ARM/XiaoBai.uvprojx -j0 -o build.log`
Expected: 0 Error 0 Warning。

- [ ] **Step 7: 逻辑对照验收（帧解析等价性）**

Manual: 构造 3 组字节流对比新旧逻辑输出：(a) 完整有效帧 `5A 97 98 0A C1 + 10 键 + CRC + A5` 应解出正确键位；(b) 前置脏字节 + 有效帧应跳过脏字节解出帧；(c) 坏 CRC 帧应被丢弃并继续找下一帧。原 main.c 逻辑为基准，确认 Proto_Remote 输出一致。

- [ ] **Step 8: Commit**

```bash
git add Protocol/ BSP_Drivers/Bsp_UartBle/Bsp_UartBle.h User/main.c MDK-ARM/XiaoBai.uvprojx
git commit -m "refactor: 遥控帧解析迁入协议层（Proto_Remote：帧校验+按键位图），main.c 消费有效帧"
```

---

### Task 3: 创建 App_System（时钟配置 + 错误处理迁出 main.c）

**Files:**
- Create: `User/App_System.h`
- Create: `User/App_System.c`
- Modify: `User/main.c`（删 APP_SystemClockConfig/APP_ErrorHandler 定义，改调用）
- Modify: `MDK-ARM/XiaoBai.uvprojx`（User Group 加 App_System.c）

**Interfaces:**
- Produces: `void App_System_Init(void)`（等价原 APP_SystemClockConfig）；`void APP_ErrorHandler(void)`（定义迁入本文件，main.h 声明保留）

- [ ] **Step 1: 创建 `User/App_System.h`**

```c
#ifndef __APP_SYSTEM_H
#define __APP_SYSTEM_H

/** 系统时钟初始化：HSI 24MHz → PLL → 48MHz 系统时钟。
 *  必须 HAL_Init() 之后、BSP_Init() 之前调用。失败进入 APP_ErrorHandler 死循环。 */
void App_System_Init(void);

#endif
```

- [ ] **Step 2: 创建 `User/App_System.c`**

```c
#include "App_System.h"
#include "main.h"
#include "py32f0xx_hal.h"

void App_System_Init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSIDiv = RCC_HSI_DIV1;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_24MHz;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) APP_ErrorHandler();

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1) != HAL_OK) APP_ErrorHandler();
}

void APP_ErrorHandler(void) { while (1) { } }
```

- [ ] **Step 3: 修改 `User/main.c`**

- 删除 `static void APP_SystemClockConfig(void);` 前置声明（原 240 行）与 `APP_SystemClockConfig` 定义（原 605-623 行）、`APP_ErrorHandler` 定义（原 625 行）。
- 头部 include 增加 `#include "App_System.h"`。
- main() 内 `APP_SystemClockConfig();`（原 245 行）改为 `App_System_Init();`。

- [ ] **Step 4: 修改 `MDK-ARM/XiaoBai.uvprojx` — User Group 加 App_System.c**

在 `<GroupName>Application/User</GroupName>` 的 `<Files>` 内、`<File>...py32f0xx_hal_msp.c...</File>` 之后插入：

```xml
    <File>
      <FileName>App_System.c</FileName>
      <FileType>1</FileType>
      <FilePath>..\User\App_System.c</FilePath>
    </File>
```

- [ ] **Step 5: 编译验证**

Run: `UV4 -b MDK-ARM/XiaoBai.uvprojx -j0 -o build.log`
Expected: 0 Error 0 Warning。

- [ ] **Step 6: Commit**

```bash
git add User/App_System.c User/App_System.h User/main.c MDK-ARM/XiaoBai.uvprojx
git commit -m "refactor: 系统时钟与错误处理迁入 App_System"
```

---

### Task 4: 创建 App_Vehicle（车辆动作统一接口，消除重复电机组合）

**Files:**
- Create: `User/App_Vehicle.h`
- Create: `User/App_Vehicle.c`
- Modify: `User/main.c`（语音/动力/感应/遥控各处的 Bsp_Motor_Set 组合改调 Vehicle）
- Modify: `MDK-ARM/XiaoBai.uvprojx`（User Group 加 App_Vehicle.c）

**Interfaces:**
- Consumes: `Bsp_Motor_Set/StopAll`（Bsp_Motor.h）
- Produces:
  - `typedef enum { VEHICLE_DIR_STOP=0, VEHICLE_DIR_FORWARD=1, VEHICLE_DIR_BACKWARD=2, VEHICLE_DIR_LEFT=3, VEHICLE_DIR_RIGHT=4 } App_Vehicle_Dir_t;`
  - `void Vehicle_Drive(App_Vehicle_Dir_t dir, Bsp_Motor_Speed_t speed)` — 双电机统一驱动；LEFT=左反右正、RIGHT=左正右反（坦克转向）
  - `void Vehicle_DriveSingle(Bsp_Motor_Id_t motor, Bsp_Motor_Dir_t dir, Bsp_Motor_Speed_t speed)` — 单电机（遥控模式单电机键用）

- [ ] **Step 1: 创建 `User/App_Vehicle.h`**

```c
#ifndef __APP_VEHICLE_H
#define __APP_VEHICLE_H
#include "Bsp.h"

/* 车辆双电机统一动作（消除动力/感应/遥控三处重复的 Bsp_Motor_Set 组合）。
 * LEFT/RIGHT 为坦克转向：LEFT=左反右正，RIGHT=左正右反。 */
typedef enum {
    VEHICLE_DIR_STOP    = 0,
    VEHICLE_DIR_FORWARD = 1,
    VEHICLE_DIR_BACKWARD = 2,
    VEHICLE_DIR_LEFT    = 3,
    VEHICLE_DIR_RIGHT   = 4,
} App_Vehicle_Dir_t;

/** 双电机统一驱动（含 STOP）。LEFT/RIGHT = 坦克转向。 */
void Vehicle_Drive(App_Vehicle_Dir_t dir, Bsp_Motor_Speed_t speed);

/** 单电机驱动（遥控模式单电机键用）。 */
void Vehicle_DriveSingle(Bsp_Motor_Id_t motor, Bsp_Motor_Dir_t dir, Bsp_Motor_Speed_t speed);

#endif
```

- [ ] **Step 2: 创建 `User/App_Vehicle.c`**

```c
#include "App_Vehicle.h"

void Vehicle_Drive(App_Vehicle_Dir_t dir, Bsp_Motor_Speed_t speed)
{
    switch (dir) {
    case VEHICLE_DIR_FORWARD:
        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  speed);
        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  speed);
        break;
    case VEHICLE_DIR_BACKWARD:
        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, speed);
        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, speed);
        break;
    case VEHICLE_DIR_LEFT:
        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, speed);
        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  speed);
        break;
    case VEHICLE_DIR_RIGHT:
        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  speed);
        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, speed);
        break;
    case VEHICLE_DIR_STOP:
    default:
        Bsp_Motor_StopAll();
        break;
    }
}

void Vehicle_DriveSingle(Bsp_Motor_Id_t motor, Bsp_Motor_Dir_t dir, Bsp_Motor_Speed_t speed)
{
    Bsp_Motor_Set(motor, dir, speed);
}
```

- [ ] **Step 3: 修改 `User/main.c` — 各模式电机组合改调 Vehicle**

头部 include 增加 `#include "App_Vehicle.h"`。

**3a. 语音模式动作命令段（原 344-371 行 switch）** 替换为：

```c
                        switch (e.arg) {
                        case ASR_CMD_FORWARD:  Vehicle_Drive(VEHICLE_DIR_FORWARD,  MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_BACKWARD: Vehicle_Drive(VEHICLE_DIR_BACKWARD, MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_LEFT:     Vehicle_Drive(VEHICLE_DIR_LEFT,     MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_RIGHT:    Vehicle_Drive(VEHICLE_DIR_RIGHT,    MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_STOP:     Vehicle_Drive(VEHICLE_DIR_STOP,     MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_L_FWD:  Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_L_REV:  Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_L_STOP: Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_STOP,     MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_R_FWD:  Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_R_REV:  Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_R_STOP: Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_STOP,     MOTOR_SPEED_HIGH); break;
                        default: break;
                        }
```

**3b. 遥控模式方向键段（原 429-449 行）** 替换为：

```c
                    if (keys[REMOTE_KEY_UP]) {
                        Vehicle_Drive(VEHICLE_DIR_FORWARD, g_remote_speed);
                    } else if (keys[REMOTE_KEY_DOWN]) {
                        Vehicle_Drive(VEHICLE_DIR_BACKWARD, g_remote_speed);
                    } else if (keys[REMOTE_KEY_LEFT]) {
                        Vehicle_Drive(VEHICLE_DIR_LEFT, g_remote_speed);
                    } else if (keys[REMOTE_KEY_RIGHT]) {
                        Vehicle_Drive(VEHICLE_DIR_RIGHT, g_remote_speed);
                    } else {
                        /* 单电机：Y=L正转 A=L反转 X=R正转 B=R反转 */
                        if (keys[REMOTE_KEY_Y])      Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  g_remote_speed);
                        else if (keys[REMOTE_KEY_A]) Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, g_remote_speed);
                        else                         Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_STOP,     g_remote_speed);
                        if (keys[REMOTE_KEY_X])      Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  g_remote_speed);
                        else if (keys[REMOTE_KEY_B]) Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, g_remote_speed);
                        else                         Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_STOP,     g_remote_speed);
                    }
```

**3c. 动力模式主循环驱动段（原 484-507 行 switch）** 替换为：

```c
        if (g_mode == APP_MODE_POWER && !g_mode_paused) {
            switch (g_power_action) {
            case POWER_ACT_STOP:  Vehicle_Drive(VEHICLE_DIR_STOP,     MOTOR_SPEED_HIGH); break;
            case POWER_ACT_FWD:   Vehicle_Drive(VEHICLE_DIR_FORWARD,  MOTOR_SPEED_HIGH); break;
            case POWER_ACT_BACK:  Vehicle_Drive(VEHICLE_DIR_BACKWARD, MOTOR_SPEED_HIGH); break;
            case POWER_ACT_LEFT:  Vehicle_Drive(VEHICLE_DIR_LEFT,     MOTOR_SPEED_HIGH); break;
            case POWER_ACT_RIGHT: Vehicle_Drive(VEHICLE_DIR_RIGHT,    MOTOR_SPEED_HIGH); break;
            }
        }
```

**3d. 感应模式各玩法段（原 518-571 行 switch 内）** 替换：
- APPROACH 段：`Bsp_Motor_Set(MOTOR_LEFT, MOTOR_DIR_FORWARD, MOTOR_SPEED_MID); Bsp_Motor_Set(MOTOR_RIGHT, ...)` → `Vehicle_Drive(VEHICLE_DIR_FORWARD, MOTOR_SPEED_MID)`；`Bsp_Motor_StopAll()` → `Vehicle_Drive(VEHICLE_DIR_STOP, MOTOR_SPEED_MID)`（行为等价：StopAll 即双 STOP）。
- OBSTACLE 段：同上（FORWARD/MID 与 STOP）。
- WAVE 段：`g_wave_on` 分支同上。
- BRIGHTNESS 段：HIGH/MID/LOW 三档 `Vehicle_Drive(VEHICLE_DIR_FORWARD, speed)` + STOP。

> 保持 switch 结构与各玩法顺序不变，仅电机调用行替换。

- [ ] **Step 4: 修改 `MDK-ARM/XiaoBai.uvprojx` — User Group 加 App_Vehicle.c**

在 App_System.c 文件条目后追加：

```xml
    <File>
      <FileName>App_Vehicle.c</FileName>
      <FileType>1</FileType>
      <FilePath>..\User\App_Vehicle.c</FilePath>
    </File>
```

- [ ] **Step 5: 编译验证**

Run: `UV4 -b MDK-ARM/XiaoBai.uvprojx -j0 -o build.log`
Expected: 0 Error 0 Warning。

- [ ] **Step 6: 逻辑对照验收（动作→电机映射表）**

Manual: 逐项对照新旧：
- FORWARD=左FWD+右FWD / BACKWARD=左BACK+右BACK / LEFT=左BACK+右FWD / RIGHT=左FWD+右BACK / STOP=双停；
- 单电机直传不变；速度档原样传入。

- [ ] **Step 7: Commit**

```bash
git add User/App_Vehicle.c User/App_Vehicle.h User/main.c MDK-ARM/XiaoBai.uvprojx
git commit -m "refactor: 车辆动作统一接口 App_Vehicle，消除三处重复电机组合"
```

---

### Task 5: 创建 App_Mode + App_Mode_Power（模式状态机框架 + 动力模式）

**Files:**
- Create: `User/App_Mode.h`
- Create: `User/App_Mode.c`
- Create: `User/App_Mode_Power.h`
- Create: `User/App_Mode_Power.c`
- Modify: `User/main.c`（Power_Action_t 枚举、g_mode/g_mode_paused/g_power_action 状态、SwitchMode、动力模式相关段迁出）
- Modify: `MDK-ARM/XiaoBai.uvprojx`（User Group 加 App_Mode.c / App_Mode_Power.c）

**Interfaces:**
- Produces:
  - `App_Mode.h`: `typedef enum { APP_MODE_VOICE=0, APP_MODE_POWER=1, APP_MODE_SENSOR=2, APP_MODE_REMOTE=3, APP_MODE_COUNT } App_Mode_t;`；`void App_Mode_Switch(App_Mode_t new_mode, uint8_t play_voice)`（等价原 SwitchMode）；`App_Mode_t App_Mode_Get(void)`；`uint8_t App_Mode_IsPaused(void)`；`void App_Mode_SetPaused(uint8_t p)`
  - `App_Mode_Power.h`: `void App_Mode_Power_OnKey(void)`（KEY4 在动力模式内的动作切换）；`void App_Mode_Power_Update(void)`（主循环持续驱动）
- Consumes: `App_Mode.h`（g_mode/g_mode_paused 封装）、`Proto_Asr.h`（mode_voice/act_voice 表）、`App_Vehicle.h`

- [ ] **Step 1: 创建 `User/App_Mode.h`**

```c
#ifndef __APP_MODE_H
#define __APP_MODE_H
#include <stdint.h>

typedef enum {
    APP_MODE_VOICE  = 0,
    APP_MODE_POWER  = 1,
    APP_MODE_SENSOR = 2,
    APP_MODE_REMOTE = 3,
    APP_MODE_COUNT
} App_Mode_t;

/** 切模式：停电机 + 点 LED + 可选播报（等价原 SwitchMode，语义不变） */
void App_Mode_Switch(App_Mode_t new_mode, uint8_t play_voice);

App_Mode_t App_Mode_Get(void);
uint8_t    App_Mode_IsPaused(void);
void       App_Mode_SetPaused(uint8_t p);

#endif
```

- [ ] **Step 2: 创建 `User/App_Mode.c`**

```c
#include "App_Mode.h"
#include "Bsp.h"
#include "Proto_Asr.h"
#include "App_Mode_Power.h"
#include "App_Mode_Sensor.h"
#include "App_Mode_Remote.h"

/* 模式状态（原 main.c 全局状态迁入） */
static App_Mode_t g_mode = APP_MODE_VOICE;
static uint8_t    g_mode_paused = 0;

/* 模式 -> 对应 LED（KEY-LED 一一对应，2026-07-06 变更）：
     语音->LED1 / 感应->LED2 / 遥控->LED4 / 动力->LED3
   注：Bsp_Led 枚举名 LED_MODE_POWER 实际是 LED1(PB2)，LED_MODE_VOICE 是 LED4(PA12)，
   名称跟模式不对应，但 mode_led[] 按物理 LED 映射，逻辑正确。 */
static const Bsp_Led_Id_t mode_led[APP_MODE_COUNT] = {
    LED_MODE_POWER,   /* APP_MODE_VOICE  -> LED1 */
    LED_MODE_REMOTE,  /* APP_MODE_POWER  -> LED3 */
    LED_MODE_SENSOR,  /* APP_MODE_SENSOR -> LED2 */
    LED_MODE_VOICE,   /* APP_MODE_REMOTE -> LED4 */
};
/* 模式 -> 进入时播报的语音 ID */
static const uint8_t mode_voice[APP_MODE_COUNT] = {
    ASR_VOICE_ENTER_VOICE, ASR_VOICE_ENTER_POWER,
    ASR_VOICE_ENTER_SENSOR, ASR_VOICE_ENTER_REMOTE,
};

App_Mode_t App_Mode_Get(void)        { return g_mode; }
uint8_t    App_Mode_IsPaused(void)   { return g_mode_paused; }
void       App_Mode_SetPaused(uint8_t p) { g_mode_paused = p; }

void App_Mode_Switch(App_Mode_t new_mode, uint8_t play_voice)
{
    if (new_mode >= APP_MODE_COUNT) return;
    Bsp_Motor_StopAll();
    g_mode = new_mode;
    Bsp_Led_AllOff();
    Bsp_Led_On(mode_led[g_mode]);
    if (new_mode == APP_MODE_REMOTE) {
        App_Mode_Remote_Enter();   /* 进遥控模式默认 2 档 70% */
    }
    if (new_mode == APP_MODE_SENSOR) {
        App_Mode_Sensor_Enter();   /* 进感应模式默认玩法1，清挥手/边沿状态 */
    }
    if (new_mode == APP_MODE_POWER || new_mode == APP_MODE_SENSOR) {
        g_mode_paused = 1;   /* 进入模式后暂停，第二次按键才启动 */
    }
    if (play_voice) {
        Bsp_UartAsr_SendPlay(mode_voice[g_mode]);
        /* 不等 done，异步播报，保证按键灵敏 */
    }
}
```

> 说明：原 SwitchMode 直接重置 `g_remote_speed/g_sensor_play/g_wave_on/g_ir1_was/g_ir3_was`；这些状态随模式模块内聚（Task 6 拆 App_Mode_Remote/Sensor 时迁入），故先声明 `App_Mode_Remote_Enter/App_Mode_Sensor_Enter`（Task 6 实现，本任务先放桩会编译失败 —— 见 Step 3 处理）。

- [ ] **Step 3: 创建 `User/App_Mode_Power.h`**

```c
#ifndef __APP_MODE_POWER_H
#define __APP_MODE_POWER_H
#include <stdint.h>

/** KEY4 在动力模式内的动作切换（第二次按键起逐档轮换，首按启动前进） */
void App_Mode_Power_OnKey(void);

/** 主循环持续驱动：按当前动作档驱动电机（仅动力模式且未暂停时生效） */
void App_Mode_Power_Update(void);

#endif
```

- [ ] **Step 3 注：Task 5 的编译依赖处理** — 先建 `User/App_Mode_Remote.h`、`User/App_Mode_Sensor.h` 桩头文件（只含 `void App_Mode_Remote_Enter(void);` / `void App_Mode_Sensor_Enter(void);` 声明），Task 6 填充实现。同时建 `User/App_Mode_Sensor.c`、`User/App_Mode_Remote.c` 空实现文件（含 Enter 空函数体），保证链接通过：

```c
/* App_Mode_Remote.c（Task 6 填充完整实现） */
#include "App_Mode_Remote.h"
void App_Mode_Remote_Enter(void) { }
```

```c
/* App_Mode_Sensor.c（Task 6 填充完整实现） */
#include "App_Mode_Sensor.h"
void App_Mode_Sensor_Enter(void) { }
```

- [ ] **Step 4: 创建 `User/App_Mode_Power.c`**

```c
#include "App_Mode_Power.h"
#include "App_Mode.h"
#include "Bsp.h"
#include "Proto_Asr.h"
#include "App_Vehicle.h"

/* 动力模式的 5 个动作（文档 §8） */
typedef enum {
    POWER_ACT_STOP  = 0,
    POWER_ACT_FWD   = 1,
    POWER_ACT_BACK  = 2,
    POWER_ACT_LEFT  = 3,
    POWER_ACT_RIGHT = 4,
    POWER_ACT_COUNT
} Power_Action_t;

static Power_Action_t g_power_action = POWER_ACT_STOP;

/* 动力动作 -> 语音 ID（文档 §7） */
static const uint8_t act_voice[POWER_ACT_COUNT] = {
    ASR_VOICE_STOP, ASR_VOICE_FORWARD, ASR_VOICE_BACKWARD,
    ASR_VOICE_LEFT, ASR_VOICE_RIGHT,
};

void App_Mode_Power_OnKey(void)
{
    if (App_Mode_IsPaused()) {
        App_Mode_SetPaused(0);               /* 第一次按键：启动 */
        g_power_action = POWER_ACT_FWD;      /* 从前进开始 */
    } else {
        g_power_action = (Power_Action_t)((g_power_action + 1) % POWER_ACT_COUNT);
    }
    Bsp_Motor_StopAll();
    Bsp_UartAsr_SendPlay(act_voice[g_power_action]);
}

void App_Mode_Power_Update(void)
{
    if (App_Mode_Get() != APP_MODE_POWER || App_Mode_IsPaused()) return;
    switch (g_power_action) {
    case POWER_ACT_STOP:  Vehicle_Drive(VEHICLE_DIR_STOP,     MOTOR_SPEED_HIGH); break;
    case POWER_ACT_FWD:   Vehicle_Drive(VEHICLE_DIR_FORWARD,  MOTOR_SPEED_HIGH); break;
    case POWER_ACT_BACK:  Vehicle_Drive(VEHICLE_DIR_BACKWARD, MOTOR_SPEED_HIGH); break;
    case POWER_ACT_LEFT:  Vehicle_Drive(VEHICLE_DIR_LEFT,     MOTOR_SPEED_HIGH); break;
    case POWER_ACT_RIGHT: Vehicle_Drive(VEHICLE_DIR_RIGHT,    MOTOR_SPEED_HIGH); break;
    default: break;
    }
}
```

- [ ] **Step 5: 修改 `User/main.c`**

- 删除 `App_Mode_t` 枚举（原 5-11 行）、`Power_Action_t` 枚举（原 14-21 行）、`mode_led[]/mode_voice[]/act_voice[]` 表（原 40-55 行）、全局状态 `g_mode/g_power_action/g_mode_paused`（原 104-109 行中相关行）、`SwitchMode` 定义（原 158-181 行）。
- 头部 include 增加 `#include "App_Mode.h"`、`#include "App_Mode_Power.h"`、`#include "App_Mode_Sensor.h"`、`#include "App_Mode_Remote.h"`。
- 所有 `SwitchMode(...)` 调用（原 253、280、293、296、309、333-336 行）改为 `App_Mode_Switch(...)`。
- `g_mode` 直接引用改为 `App_Mode_Get()`；`g_mode_paused` 读改 `App_Mode_IsPaused()`、写改 `App_Mode_SetPaused()`（KEY_ID_2 分支的 `g_mode_paused = 0;` 改 `App_Mode_SetPaused(0);`）。
- KEY_ID_4 分支（原 297-311 行）替换为：

```c
                case KEY_ID_4:  /* LED4 动力模式：已在动力模式则切动作 */
                    if (App_Mode_Get() == APP_MODE_POWER) {
                        App_Mode_Power_OnKey();
                    } else {
                        App_Mode_Switch(APP_MODE_POWER, 1);
                    }
                    break;
```

- 动力模式主循环驱动段（原 483-507 行）删除，替换为 `App_Mode_Power_Update();`。

- [ ] **Step 6: 修改 `MDK-ARM/XiaoBai.uvprojx` — User Group 加 App_Mode.c / App_Mode_Power.c / App_Mode_Sensor.c / App_Mode_Remote.c**

在 App_Vehicle.c 条目后追加（App_Mode_Sensor.c / App_Mode_Remote.c 为 Task 5 的桩实现，一并加入避免链接缺失）：

```xml
    <File>
      <FileName>App_Mode.c</FileName>
      <FileType>1</FileType>
      <FilePath>..\User\App_Mode.c</FilePath>
    </File>
    <File>
      <FileName>App_Mode_Power.c</FileName>
      <FileType>1</FileType>
      <FilePath>..\User\App_Mode_Power.c</FilePath>
    </File>
    <File>
      <FileName>App_Mode_Sensor.c</FileName>
      <FileType>1</FileType>
      <FilePath>..\User\App_Mode_Sensor.c</FilePath>
    </File>
    <File>
      <FileName>App_Mode_Remote.c</FileName>
      <FileType>1</FileType>
      <FilePath>..\User\App_Mode_Remote.c</FilePath>
    </File>
```

- [ ] **Step 7: 编译验证**

Run: `UV4 -b MDK-ARM/XiaoBai.uvprojx -j0 -o build.log`
Expected: 0 Error 0 Warning。

- [ ] **Step 8: 逻辑对照验收**

Manual: SwitchMode 语义对照原实现：停电机→设模式→灭全 LED→点对应 LED→遥控/感应进入重置→power/sensor 置 paused→可选播报。mode_led/mode_voice/act_voice 表逐项对照。KEY4 逻辑：首按启动 FWD+播报、再按轮换动作+播报。

- [ ] **Step 9: Commit**

```bash
git add User/App_Mode.c User/App_Mode.h User/App_Mode_Power.c User/App_Mode_Power.h User/App_Mode_Sensor.c User/App_Mode_Sensor.h User/App_Mode_Remote.c User/App_Mode_Remote.h User/main.c MDK-ARM/XiaoBai.uvprojx
git commit -m "refactor: 模式状态机迁入 App_Mode，动力模式迁入 App_Mode_Power"
```

---

### Task 6: 创建 App_Mode_Sensor + App_Mode_Remote（感应 4 玩法 + 遥控消费）

**Files:**
- Create: `User/App_Mode_Sensor.c`（填充完整实现）
- Create: `User/App_Mode_Sensor.h`（补充接口）
- Create: `User/App_Mode_Remote.c`（填充完整实现）
- Create: `User/App_Mode_Remote.h`（补充接口）
- Modify: `User/main.c`（感应/遥控相关段迁出）
- Modify: `MDK-ARM/XiaoBai.uvprojx`（无需变更，Task 5 已加文件）

**Interfaces:**
- Produces:
  - `App_Mode_Sensor.h`: `void App_Mode_Sensor_Enter(void)`（重置玩法1 + 清挥手/边沿）；`void App_Mode_Sensor_OnKey(void)`（KEY2 在感应模式内切玩法）；`void App_Mode_Sensor_Update(void)`（读 IR + 4 玩法驱动）
  - `App_Mode_Remote.h`: `void App_Mode_Remote_Enter(void)`（重置速度 2 档 + 清边沿 + 清超时）；`void App_Mode_Remote_OnFrame(const uint8_t keys[REMOTE_KEY_COUNT])`（消费一帧：肩键/方向/单电机）；`void App_Mode_Remote_Update(void)`（1s 超时停机）
- Consumes: `App_Mode.h`、`Proto_Remote.h`、`App_Vehicle.h`、`Bsp_IR.h`、`Bsp_Tick.h`

- [ ] **Step 1: 更新 `User/App_Mode_Sensor.h`**

```c
#ifndef __APP_MODE_SENSOR_H
#define __APP_MODE_SENSOR_H
#include <stdint.h>

/** 进入感应模式时重置：玩法1（靠近启动）、挥手关、清边沿状态 */
void App_Mode_Sensor_Enter(void);

/** KEY2 在感应模式内：首按启动当前玩法，再按轮换玩法（播报对应语音） */
void App_Mode_Sensor_OnKey(void);

/** 主循环持续驱动：按当前玩法执行（靠近启动/遇障停止/挥手开关/明暗调速） */
void App_Mode_Sensor_Update(void);

#endif
```

- [ ] **Step 2: 填充 `User/App_Mode_Sensor.c`（替换 Task 5 桩）**

```c
#include "App_Mode_Sensor.h"
#include "App_Mode.h"
#include "Bsp.h"
#include "Proto_Asr.h"
#include "App_Vehicle.h"

/* 感应模式的 4 个玩法（文档 §9） */
typedef enum {
    SENSOR_PLAY_APPROACH   = 0,  /* 靠近启动 */
    SENSOR_PLAY_OBSTACLE   = 1,  /* 遇障停止 */
    SENSOR_PLAY_WAVE       = 2,  /* 挥手开关 */
    SENSOR_PLAY_BRIGHTNESS = 3,  /* 明暗调速 */
    SENSOR_PLAY_COUNT
} Sensor_Play_t;

/* 红外反射阈值：ADC < 3000 = 有反射（遮挡时~200，无反射~4000）。
   方案A硬编码，量产不需要用户校准。 */
#define IR_THRESHOLD  3000U

static Sensor_Play_t g_sensor_play = SENSOR_PLAY_APPROACH;
static uint8_t       g_wave_on = 0;            /* 挥手开关的当前开/关状态 */
static uint8_t       g_ir1_was = 0, g_ir2_was = 0, g_ir3_was = 0;  /* 挥手边沿检测 */

/* 感应玩法 -> 语音 ID（文档 §7） */
static const uint8_t sensor_voice[SENSOR_PLAY_COUNT] = {
    ASR_VOICE_APPROACH_GO, ASR_VOICE_OBSTACLE_STOP,
    ASR_VOICE_WAVE_TOGGLE, ASR_VOICE_BRIGHTNESS,
};

void App_Mode_Sensor_Enter(void)
{
    g_sensor_play = SENSOR_PLAY_APPROACH;
    g_wave_on = 0;
    g_ir1_was = 0;
    g_ir2_was = 0;
    g_ir3_was = 0;
}

void App_Mode_Sensor_OnKey(void)
{
    if (App_Mode_IsPaused()) {
        App_Mode_SetPaused(0);   /* 第一次按键：启动第一个玩法 */
        Bsp_UartAsr_SendPlay(sensor_voice[g_sensor_play]);
    } else {
        g_sensor_play = (Sensor_Play_t)((g_sensor_play + 1) % SENSOR_PLAY_COUNT);
        Bsp_Motor_StopAll();
        g_wave_on = 0;
        Bsp_UartAsr_SendPlay(sensor_voice[g_sensor_play]);
    }
}

void App_Mode_Sensor_Update(void)
{
    if (App_Mode_Get() != APP_MODE_SENSOR || App_Mode_IsPaused()) return;

    uint16_t ir1 = Bsp_IR_ReadCh1();
    uint16_t ir2 = Bsp_IR_ReadCh2();
    uint16_t ir3 = Bsp_IR_ReadCh3();
    uint8_t ir1_trig = (ir1 < IR_THRESHOLD);  /* 有反射=遮挡 */
    uint8_t ir2_trig = (ir2 < IR_THRESHOLD);
    uint8_t ir3_trig = (ir3 < IR_THRESHOLD);

    switch (g_sensor_play) {
    case SENSOR_PLAY_APPROACH:
        /* 靠近启动：有物体前进，无物体停 */
        if (ir2_trig) Vehicle_Drive(VEHICLE_DIR_FORWARD, MOTOR_SPEED_MID);
        else          Vehicle_Drive(VEHICLE_DIR_STOP,    MOTOR_SPEED_MID);
        break;
    case SENSOR_PLAY_OBSTACLE:
        /* 遇障停止：前进，遇障碍停 */
        if (ir2_trig) Vehicle_Drive(VEHICLE_DIR_STOP,    MOTOR_SPEED_MID);
        else          Vehicle_Drive(VEHICLE_DIR_FORWARD, MOTOR_SPEED_MID);
        break;
    case SENSOR_PLAY_WAVE:
        /* 挥手开关：IR1/IR2/IR3 任一检测到手（下降沿）→ 切换，500ms 消抖 */
        {
            static uint32_t last_wave = 0;
            uint8_t any_edge = (ir1_trig && !g_ir1_was) ||
                               (ir2_trig && !g_ir2_was) ||
                               (ir3_trig && !g_ir3_was);
            if (any_edge && (Bsp_Tick_GetMs() - last_wave > 500)) {
                g_wave_on = !g_wave_on;
                last_wave = Bsp_Tick_GetMs();
            }
        }
        if (g_wave_on) Vehicle_Drive(VEHICLE_DIR_FORWARD, MOTOR_SPEED_MID);
        else           Vehicle_Drive(VEHICLE_DIR_STOP,    MOTOR_SPEED_MID);
        break;
    case SENSOR_PLAY_BRIGHTNESS:
        /* 明暗调速：反射越强（值越小）速度越快 */
        if (ir2 < 500) {
            Vehicle_Drive(VEHICLE_DIR_FORWARD, MOTOR_SPEED_HIGH);
        } else if (ir2 < 1000) {
            Vehicle_Drive(VEHICLE_DIR_FORWARD, MOTOR_SPEED_MID);
        } else if (ir2 < IR_THRESHOLD) {
            Vehicle_Drive(VEHICLE_DIR_FORWARD, MOTOR_SPEED_LOW);
        } else {
            Vehicle_Drive(VEHICLE_DIR_STOP, MOTOR_SPEED_LOW);
        }
        break;
    default: break;
    }
    g_ir1_was = ir1_trig;
    g_ir2_was = ir2_trig;
    g_ir3_was = ir3_trig;
}
```

> 注意：原 BRIGHTNESS 的 STOP 分支用 `Bsp_Motor_StopAll()`（不依赖速度），本实现 `Vehicle_Drive(VEHICLE_DIR_STOP, MOTOR_SPEED_LOW)` 行为等价（STOP 分支忽略速度参数）。g_ir 更新在原代码位于 switch 之后无条件执行，本实现一致。

- [ ] **Step 3: 更新 `User/App_Mode_Remote.h`**

```c
#ifndef __APP_MODE_REMOTE_H
#define __APP_MODE_REMOTE_H
#include "Proto_Remote.h"

/** 进入遥控模式时重置：速度 2 档（70%）、清肩键边沿、清超时计时 */
void App_Mode_Remote_Enter(void);

/** 消费一帧有效帧：肩键调速（R1+/L1-）+ 方向键/单电机驱动（仅遥控模式生效） */
void App_Mode_Remote_OnFrame(const uint8_t keys[REMOTE_KEY_COUNT]);

/** 主循环调用：1s 未收到帧则停机（防断连电机狂转） */
void App_Mode_Remote_Update(void);

#endif
```

- [ ] **Step 4: 填充 `User/App_Mode_Remote.c`（替换 Task 5 桩）**

```c
#include "App_Mode_Remote.h"
#include "App_Mode.h"
#include "Bsp.h"
#include "App_Vehicle.h"

static Bsp_Motor_Speed_t g_remote_speed = MOTOR_SPEED_MID;  /* 3 档速度，默认 2 档 70% */
static uint32_t          g_last_remote_frame = 0;           /* 超时停机计时 */
static uint8_t           g_r1_was = 0, g_l1_was = 0;        /* 肩键边沿检测 */

void App_Mode_Remote_Enter(void)
{
    g_remote_speed = MOTOR_SPEED_MID;
    g_last_remote_frame = 0;
    g_r1_was = 0;
    g_l1_was = 0;
}

void App_Mode_Remote_OnFrame(const uint8_t keys[REMOTE_KEY_COUNT])
{
    if (App_Mode_Get() != APP_MODE_REMOTE) return;

    /* 肩键调速（边沿触发）：R1=速度+，L1=速度- */
    if (keys[REMOTE_KEY_R1] && !g_r1_was) {
        if (g_remote_speed < MOTOR_SPEED_HIGH) g_remote_speed++;
    }
    if (keys[REMOTE_KEY_L1] && !g_l1_was) {
        if (g_remote_speed > MOTOR_SPEED_LOW) g_remote_speed--;
    }
    g_r1_was = keys[REMOTE_KEY_R1];
    g_l1_was = keys[REMOTE_KEY_L1];

    /* 方向键优先（坦克转向），否则单电机键 */
    if (keys[REMOTE_KEY_UP]) {
        Vehicle_Drive(VEHICLE_DIR_FORWARD, g_remote_speed);
    } else if (keys[REMOTE_KEY_DOWN]) {
        Vehicle_Drive(VEHICLE_DIR_BACKWARD, g_remote_speed);
    } else if (keys[REMOTE_KEY_LEFT]) {
        Vehicle_Drive(VEHICLE_DIR_LEFT, g_remote_speed);
    } else if (keys[REMOTE_KEY_RIGHT]) {
        Vehicle_Drive(VEHICLE_DIR_RIGHT, g_remote_speed);
    } else {
        /* 单电机：Y=L正转 A=L反转 X=R正转 B=R反转 */
        if (keys[REMOTE_KEY_Y])      Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  g_remote_speed);
        else if (keys[REMOTE_KEY_A]) Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, g_remote_speed);
        else                         Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_STOP,     g_remote_speed);
        if (keys[REMOTE_KEY_X])      Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  g_remote_speed);
        else if (keys[REMOTE_KEY_B]) Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, g_remote_speed);
        else                         Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_STOP,     g_remote_speed);
    }
    g_last_remote_frame = Bsp_Tick_GetMs();
}

void App_Mode_Remote_Update(void)
{
    if (App_Mode_Get() != APP_MODE_REMOTE) return;
    if (g_last_remote_frame != 0 &&
        (Bsp_Tick_GetMs() - g_last_remote_frame > 1000)) {
        Bsp_Motor_StopAll();
    }
}
```

- [ ] **Step 5: 修改 `User/main.c`**

- 删除 `Sensor_Play_t` 枚举（原 24-30 行）、`IR_THRESHOLD`（原 34 行）、`sensor_voice[]` 表（原 57-60 行）、`g_sensor_play/g_wave_on/g_ir1_was/g_ir2_was/g_ir3_was` 状态（原 106-108 行）、遥控状态 `g_remote_speed/g_last_remote_frame/g_r1_was/g_l1_was`（原 112-114 行）。
- KEY_ID_2 分支（原 281-295 行）替换为：

```c
                case KEY_ID_2:  /* LED2 感应模式：已在感应模式则切玩法 */
                    if (App_Mode_Get() == APP_MODE_SENSOR) {
                        App_Mode_Sensor_OnKey();
                    } else {
                        App_Mode_Switch(APP_MODE_SENSOR, 1);
                    }
                    break;
```

- 遥控帧消费块（Task 2 引入的 while(GetFrame) 内处理）替换为：

```c
            uint8_t keys[REMOTE_KEY_COUNT];
            while (Proto_Remote_GetFrame(keys)) {
                App_Mode_Remote_OnFrame(keys);
            }
```

- 感应模式主循环驱动段（原 509-575 行）删除，替换为 `App_Mode_Sensor_Update();`。
- 遥控超时停机段（原 577-583 行）删除，替换为 `App_Mode_Remote_Update();`。

- [ ] **Step 6: 编译验证**

Run: `UV4 -b MDK-ARM/XiaoBai.uvprojx -j0 -o build.log`
Expected: 0 Error 0 Warning。

- [ ] **Step 7: 逻辑对照验收**

Manual: 4 玩法行为逐一对照原实现（含挥手 500ms 消抖、明暗 500/1000/3000 阈值、g_ir 边沿更新时机）；遥控肩键边沿/方向键优先/单电机/超时 1s 停机与原实现一致。

- [ ] **Step 8: Commit**

```bash
git add User/App_Mode_Sensor.c User/App_Mode_Sensor.h User/App_Mode_Remote.c User/App_Mode_Remote.h User/main.c
git commit -m "refactor: 感应模式迁入 App_Mode_Sensor，遥控模式迁入 App_Mode_Remote"
```

---

### Task 7: 创建 App_Mode_Voice（语音模式动作命令）

**Files:**
- Create: `User/App_Mode_Voice.h`
- Create: `User/App_Mode_Voice.c`
- Modify: `User/main.c`（语音命令"段3"迁出）
- Modify: `MDK-ARM/XiaoBai.uvprojx`（User Group 加 App_Mode_Voice.c）

**Interfaces:**
- Produces: `void App_Mode_Voice_OnCmd(uint8_t cmd)` — 语音动作命令（ASR_CMD_FORWARD..R_STOP，仅语音模式生效）：先播报（Proto_Asr_CmdToVoice）再驱动电机
- Consumes: `App_Mode.h`、`Proto_Asr.h`、`App_Vehicle.h`

- [ ] **Step 1: 创建 `User/App_Mode_Voice.h`**

```c
#ifndef __APP_MODE_VOICE_H
#define __APP_MODE_VOICE_H
#include <stdint.h>

/** 语音动作命令处理（仅语音模式生效）：播报对应语音 + 驱动电机 */
void App_Mode_Voice_OnCmd(uint8_t cmd);

#endif
```

- [ ] **Step 2: 创建 `User/App_Mode_Voice.c`**

```c
#include "App_Mode_Voice.h"
#include "App_Mode.h"
#include "Bsp.h"
#include "Proto_Asr.h"
#include "App_Vehicle.h"

void App_Mode_Voice_OnCmd(uint8_t cmd)
{
    if (App_Mode_Get() != APP_MODE_VOICE) return;
    if (cmd < ASR_CMD_FORWARD || cmd > ASR_CMD_R_STOP) return;

    /* 统一回播规则：MCU 实际动作 → 对应播报语（协议层查表） */
    Bsp_UartAsr_SendPlay(Proto_Asr_CmdToVoice(cmd));
    switch (cmd) {
    case ASR_CMD_FORWARD:  Vehicle_Drive(VEHICLE_DIR_FORWARD,  MOTOR_SPEED_HIGH); break;
    case ASR_CMD_BACKWARD: Vehicle_Drive(VEHICLE_DIR_BACKWARD, MOTOR_SPEED_HIGH); break;
    case ASR_CMD_LEFT:     Vehicle_Drive(VEHICLE_DIR_LEFT,     MOTOR_SPEED_HIGH); break;
    case ASR_CMD_RIGHT:    Vehicle_Drive(VEHICLE_DIR_RIGHT,    MOTOR_SPEED_HIGH); break;
    case ASR_CMD_STOP:     Vehicle_Drive(VEHICLE_DIR_STOP,     MOTOR_SPEED_HIGH); break;
    case ASR_CMD_L_FWD:  Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH); break;
    case ASR_CMD_L_REV:  Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH); break;
    case ASR_CMD_L_STOP: Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_STOP,     MOTOR_SPEED_HIGH); break;
    case ASR_CMD_R_FWD:  Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH); break;
    case ASR_CMD_R_REV:  Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH); break;
    case ASR_CMD_R_STOP: Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_STOP,     MOTOR_SPEED_HIGH); break;
    default: break;
    }
}
```

> 原实现是先播报后动作、无条件在 VOICE 模式执行；本实现保持先播报后动作顺序与范围检查（范围检查由 main.c 段3 的 `e.arg >= ASR_CMD_FORWARD && e.arg <= ASR_CMD_R_STOP` 已保证，此处双保险不改变行为）。

- [ ] **Step 3: 修改 `User/main.c`**

- 语音命令段3（原 339-372 行 `else if (g_mode == APP_MODE_VOICE && ...)` 整块）替换为：

```c
                    /* 段3: 仅语音模式响应（动作命令 11 条） */
                    else if (e.arg >= ASR_CMD_FORWARD && e.arg <= ASR_CMD_R_STOP) {
                        App_Mode_Voice_OnCmd(e.arg);
                    }
```

- 头部 include 增加 `#include "App_Mode_Voice.h"`。

- [ ] **Step 4: 修改 `MDK-ARM/XiaoBai.uvprojx` — User Group 加 App_Mode_Voice.c**

在 App_Mode_Remote.c 条目后追加：

```xml
    <File>
      <FileName>App_Mode_Voice.c</FileName>
      <FileType>1</FileType>
      <FilePath>..\User\App_Mode_Voice.c</FilePath>
    </File>
```

- [ ] **Step 5: 编译验证**

Run: `UV4 -b MDK-ARM/XiaoBai.uvprojx -j0 -o build.log`
Expected: 0 Error 0 Warning。

- [ ] **Step 6: 逻辑对照验收**

Manual: 11 条命令逐一对照：先播报（Proto_Asr_CmdToVoice 映射）后电机动作，动作与速度档与原实现一致；非语音模式下命令被忽略（段2 切模式命令除外）。

- [ ] **Step 7: Commit**

```bash
git add User/App_Mode_Voice.c User/App_Mode_Voice.h User/main.c MDK-ARM/XiaoBai.uvprojx
git commit -m "refactor: 语音模式动作命令迁入 App_Mode_Voice"
```

---

### Task 8: 创建 App_Eye + App_Breath + App_Shutdown + App_Battery（表现层与关机/电池）

**Files:**
- Create: `User/App_Eye.h` / `User/App_Eye.c`
- Create: `User/App_Breath.h` / `User/App_Breath.c`
- Create: `User/App_Shutdown.h` / `User/App_Shutdown.c`
- Create: `User/App_Battery.h` / `User/App_Battery.c`
- Modify: `User/main.c`（Eye_Update/呼吸灯/关机/电池段迁出）
- Modify: `MDK-ARM/XiaoBai.uvprojx`（User Group 加 4 个文件）

**Interfaces:**
- Produces:
  - `App_Eye.h`: `void App_Eye_Update(void)` — TM1640 眼睛动画（未连接双眨 / 已连接瞳孔移动，非阻塞）
  - `App_Breath.h`: `void App_Breath_Start(void)`（wake 唤醒启动）；`void App_Breath_Init(void)`（开机默认关）；`void App_Breath_Update(void)`（15s 超时自关 + 10ms 步进）
  - `App_Shutdown.h`: `void App_Shutdown_Execute(void)`（完整关机流程）
  - `App_Battery.h`: `void App_Battery_Update(void)`（10ms 采样 + 低电量 5s 冷却播报）
- Consumes: `Bsp_Tm1640.h`、`Bsp_LedPwm.h`、`Bsp_UartBle.h`（IsConnected）、`Proto_Asr.h`、`Bsp_Power.h`、`Bsp_Battery.h`、`Bsp_Tick.h`、`App_Mode.h` 无关

- [ ] **Step 1: 创建 `User/App_Eye.h` + `User/App_Eye.c`**

`App_Eye.h`:
```c
#ifndef __APP_EYE_H
#define __APP_EYE_H

/** TM1640 眼睛动画：未连接→双眨（灵动），已连接→瞳孔移动（AI 生命力）。
 *  非阻塞，主循环周期调用。 */
void App_Eye_Update(void);

#endif
```

`App_Eye.c`（图案表与动画状态机从 main.c 原样迁入）:
```c
#include "App_Eye.h"
#include "Bsp.h"

/* TM1640 眼睛图案（8×14 点阵，左眼列0-6 / 右眼列7-13，各 7×8）
   椭圆形空心轮廓，眨眼=行3一条横线。 */
static const uint8_t eye_box[14] = {
    0x3C, 0x42, 0x81, 0x81, 0x81, 0x42, 0x3C,   /* 左眼 列0-6 椭圆轮廓 */
    0x3C, 0x42, 0x81, 0x81, 0x81, 0x42, 0x3C    /* 右眼 列7-13 椭圆轮廓 */
};
static const uint8_t eye_closed[14] = {
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,   /* 左眼闭 列0-6 行3 */
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08    /* 右眼闭 列7-13 行3 */
};
/* 未连接：双眨（睁2s → 闭100ms → 睁150ms → 闭100ms → 睁2s）*/
static const struct { uint16_t ms; uint8_t closed; } blink_idle[] = {
    {2000, 0}, {100, 1}, {150, 0}, {100, 1}, {2000, 0}
};
#define BLINK_IDLE_LEN (sizeof(blink_idle)/sizeof(blink_idle[0]))
/* 已连接：瞳孔移动（看左上 → 回中 → 看右上 → 回中）。
   瞳孔 3×3 实心方块：上方=行1-3(bit0x0E)，中=行3-5(bit0x38)，叠加 3 列宽。 */
static const uint8_t pupil_bit[3] = {0x0E, 0x38, 0x0E};
static const uint8_t pupil_lc[3]  = {1, 2, 4};       /* 左眼瞳孔起始列(0-6内) */
static const uint8_t pupil_rc[3]  = {8, 9, 11};      /* 右眼瞳孔起始列(7-13内) */
static const struct { uint16_t ms; uint8_t pos; } look_seq[] = {
    {500, 0}, {200, 1}, {500, 2}, {200, 1}
};
#define LOOK_LEN (sizeof(look_seq)/sizeof(look_seq[0]))

void App_Eye_Update(void)
{
    static uint32_t last_t = 0;
    static uint8_t  frame = 0;
    static uint8_t  was_connected = 0xFF;
    uint8_t connected = Bsp_UartBle_IsConnected();
    uint32_t now = Bsp_Tick_GetMs();

    /* 连接状态切换时重置，立刻显示第一帧 */
    if (connected != was_connected) {
        last_t = now;
        frame = 0;
        was_connected = connected;
        if (connected) {
            uint8_t buf[14];
            for (int i = 0; i < 14; i++) buf[i] = eye_box[i];
            for (int c = 0; c < 3; c++) {
                buf[pupil_lc[0] + c] |= pupil_bit[0];
                buf[pupil_rc[0] + c] |= pupil_bit[0];
            }
            Bsp_Tm1640_Refresh(buf);
        } else {
            Bsp_Tm1640_Refresh(eye_box);
        }
        return;
    }

    if (connected) {
        /* 瞳孔移动：看左上 → 回中 → 看右上 → 回中 */
        if (now - last_t >= look_seq[frame].ms) {
            last_t = now;
            frame = (uint8_t)((frame + 1) % LOOK_LEN);
            uint8_t buf[14];
            for (int i = 0; i < 14; i++) buf[i] = eye_box[i];
            uint8_t p = look_seq[frame].pos;
            for (int c = 0; c < 3; c++) {
                buf[pupil_lc[p] + c] |= pupil_bit[p];
                buf[pupil_rc[p] + c] |= pupil_bit[p];
            }
            Bsp_Tm1640_Refresh(buf);
        }
    } else {
        /* 双眨 */
        if (now - last_t >= blink_idle[frame].ms) {
            last_t = now;
            frame = (uint8_t)((frame + 1) % BLINK_IDLE_LEN);
            if (blink_idle[frame].closed) Bsp_Tm1640_Refresh(eye_closed);
            else                          Bsp_Tm1640_Refresh(eye_box);
        }
    }
}
```

- [ ] **Step 2: 创建 `User/App_Breath.h` + `User/App_Breath.c`**

`App_Breath.h`:
```c
#ifndef __APP_BREATH_H
#define __APP_BREATH_H

/** 开机初始化：PA9 呼吸灯默认关闭 */
void App_Breath_Init(void);

/** wake 唤醒启动呼吸灯（15s 后 Update 自动关） */
void App_Breath_Start(void);

/** 主循环周期调用：15s 超时自关 + 10ms 呼吸步进（非阻塞） */
void App_Breath_Update(void);

#endif
```

`App_Breath.c`:
```c
#include "App_Breath.h"
#include "Bsp.h"

#define BREATH_TIMEOUT_MS  15000U

static uint8_t  g_breathing = 0;       /* 1=呼吸中 0=关闭 */
static int16_t  g_breath_val = 0;
static uint8_t  g_breath_dir = 1;      /* 1=上升 0=下降 */
static uint32_t g_breath_t   = 0;      /* 呼吸动画步进时间戳 */
static uint32_t g_breath_start = 0;    /* 呼吸灯启动时刻，用于 15s 超时关灯 */

void App_Breath_Init(void)
{
    Bsp_LedPwm_Set(LEDPWM_1, 0);
    Bsp_LedPwm_Set(LEDPWM_2, 0);
}

void App_Breath_Start(void)
{
    g_breathing = 1;
    g_breath_val = 0;
    g_breath_dir = 1;
    g_breath_t = Bsp_Tick_GetMs();
    g_breath_start = g_breath_t;
}

void App_Breath_Update(void)
{
    /* 15s 超时自动关 */
    if (g_breathing && (Bsp_Tick_GetMs() - g_breath_start >= BREATH_TIMEOUT_MS)) {
        g_breathing = 0;
        g_breath_val = 0;
        g_breath_dir = 1;
        Bsp_LedPwm_Set(LEDPWM_2, 0);
    }
    /* 10ms 步进 */
    if (g_breathing && (Bsp_Tick_GetMs() - g_breath_t >= 10)) {
        g_breath_t = Bsp_Tick_GetMs();
        if (g_breath_dir) {
            g_breath_val++;
            if (g_breath_val >= 100) { g_breath_val = 100; g_breath_dir = 0; }
        } else {
            g_breath_val--;
            if (g_breath_val <= 0) { g_breath_val = 0; g_breath_dir = 1; }
        }
        Bsp_LedPwm_Set(LEDPWM_2, (uint8_t)g_breath_val);
    }
}
```

> 原 main.c 开机时 `Bsp_LedPwm_Set(LEDPWM_1,0); Bsp_LedPwm_Set(LEDPWM_2,0);` 与 PerformShutdown 内同样两句，统一收进 App_Breath_Init/App_Shutdown 复用。

- [ ] **Step 3: 创建 `User/App_Shutdown.h` + `User/App_Shutdown.c`**

`App_Shutdown.h`:
```c
#ifndef __APP_SHUTDOWN_H
#define __APP_SHUTDOWN_H

/** 完整关机流程：停电机 → 播关机语 → 关机动画 → 关 LED/TM1640 → 延时 1s → 断电。
 *  KEY1 长按和语音命令 cmd=1 共用。 */
void App_Shutdown_Execute(void);

#endif
```

`App_Shutdown.c`:
```c
#include "App_Shutdown.h"
#include "Bsp.h"
#include "Proto_Asr.h"

void App_Shutdown_Execute(void)
{
    Bsp_Motor_StopAll();
    Bsp_UartAsr_SendPlay(ASR_VOICE_SHUTDOWN);
    Bsp_Power_ShutdownAnimation();
    Bsp_LedPwm_Set(LEDPWM_1, 0);
    Bsp_LedPwm_Set(LEDPWM_2, 0);
    Bsp_Tm1640_Clear();
    Bsp_Tick_DelayMs(1000);
    Bsp_Power_ShutDown();
}
```

- [ ] **Step 4: 创建 `User/App_Battery.h` + `User/App_Battery.c`**

`App_Battery.h`:
```c
#ifndef __APP_BATTERY_H
#define __APP_BATTERY_H

/** 主循环周期调用：10ms 一次采样入滤波窗口；低电量态带 5s 冷却播报 */
void App_Battery_Update(void);

#endif
```

`App_Battery.c`:
```c
#include "App_Battery.h"
#include "Bsp.h"
#include "Proto_Asr.h"

#define LOW_BATT_REPORT_COOLDOWN_MS  5000U

void App_Battery_Update(void)
{
    /* 10ms 一次入滤波窗口 */
    static uint32_t last_batt = 0;
    uint32_t now = Bsp_Tick_GetMs();
    if (now - last_batt >= 10) {
        last_batt = now;
        Bsp_Battery_Poll();
    }

    /* 低电量周期播报：滤波+迟滞后仍处于低电量态，5s 冷却控制频率；
       电压恢复到非低电量态后，reported 保持 1，等 5s 冷却期过后若再进入
       低电量能立刻播（可接受，只在真的抖回来才响）。 */
    static uint32_t last_report = 0;
    static uint8_t  reported = 0;
    if (Bsp_Battery_IsLow()) {
        if (!reported || (now - last_report >= LOW_BATT_REPORT_COOLDOWN_MS)) {
            Bsp_UartAsr_SendPlay(ASR_VOICE_LOW_BATTERY);
            last_report = now;
            reported = 1;
        }
    }
}
```

> 原 main.c 中"10ms 电池采样"与"低电量播报"是主循环两个独立块，合并到 App_Battery_Update 后语义不变（同一 now 基准，调用顺序相同）。

- [ ] **Step 5: 修改 `User/main.c`**

- 删除 `eye_*`/`blink_idle`/`look_seq`/`pupil_*` 图案与序列表（原 78-101 行）、呼吸灯全局状态（原 116-121 行）、`App_ReportLowBattery`（原 127-137 行）、`PerformShutdown`（原 141-151 行）、`Eye_Update`（原 188-238 行）。
- 头部 include 增加 `#include "App_Eye.h"`、`#include "App_Breath.h"`、`#include "App_Shutdown.h"`、`#include "App_Battery.h"`。
- 开机序列（原 255-257 行 `Bsp_LedPwm_Set(LEDPWM_1,0); Bsp_LedPwm_Set(LEDPWM_2,0);`）替换为 `App_Breath_Init();`。
- 关机调用（原 317 行 KEY1 长按、原 328 行 ASR_CMD_SHUTDOWN）`PerformShutdown()` 替换为 `App_Shutdown_Execute()`。
- 呼吸灯 wake 启动段（原 374-382 行内赋值）替换为 `App_Breath_Start();`。
- 主循环内 `Eye_Update();`（原 462 行）替换为 `App_Eye_Update();`。
- 呼吸灯主循环段（原 464-481 行）删除，替换为 `App_Breath_Update();`。
- 电池采样 + 低电量段（原 585-599 行）删除，替换为 `App_Battery_Update();`。

- [ ] **Step 6: 修改 `MDK-ARM/XiaoBai.uvprojx` — User Group 加 4 个文件**

在 App_Mode_Voice.c 条目后追加：

```xml
    <File>
      <FileName>App_Eye.c</FileName>
      <FileType>1</FileType>
      <FilePath>..\User\App_Eye.c</FilePath>
    </File>
    <File>
      <FileName>App_Breath.c</FileName>
      <FileType>1</FileType>
      <FilePath>..\User\App_Breath.c</FilePath>
    </File>
    <File>
      <FileName>App_Shutdown.c</FileName>
      <FileType>1</FileType>
      <FilePath>..\User\App_Shutdown.c</FilePath>
    </File>
    <File>
      <FileName>App_Battery.c</FileName>
      <FileType>1</FileType>
      <FilePath>..\User\App_Battery.c</FilePath>
    </File>
```

- [ ] **Step 7: 编译验证**

Run: `UV4 -b MDK-ARM/XiaoBai.uvprojx -j0 -o build.log`
Expected: 0 Error 0 Warning。

- [ ] **Step 8: 逻辑对照验收**

Manual: 眼睛动画两态时序（双眨 2s/100ms/150ms/100ms、瞳孔 500ms/200ms 序列）对照原表；呼吸灯 15s 超时与 10ms 步进对照；关机流程顺序（停电机→播报→动画→关灯→1s→断电）对照；电池 10ms 采样 + 5s 冷却对照。

- [ ] **Step 9: Commit**

```bash
git add User/App_Eye.c User/App_Eye.h User/App_Breath.c User/App_Breath.h User/App_Shutdown.c User/App_Shutdown.h User/App_Battery.c User/App_Battery.h User/main.c MDK-ARM/XiaoBai.uvprojx
git commit -m "refactor: 眼睛/呼吸动画、关机流程、电池管理迁入独立 App 模块"
```

---

### Task 9: 创建 App_Main + main.c 瘦身为入口

**Files:**
- Create: `User/App_Main.h`
- Create: `User/App_Main.c`
- Modify: `User/main.c`（精简为入口）
- Modify: `MDK-ARM/XiaoBai.uvprojx`（User Group 加 App_Main.c）

**Interfaces:**
- Produces:
  - `void App_Init(void)` — 开机时序（等 ASRPRO 1500ms → BOOT 语 → 语音模式 → 呼吸灯 off → BLE 配名 500ms）
  - `void App_Loop(void)` — 主循环（按键 → 语音 → BLE → 模式驱动 → 眼睛 → 呼吸 → 电池），不返回
- Consumes: 全部 App_* 模块 + Bsp.h + Proto_Asr.h/Proto_Remote.h

- [ ] **Step 1: 创建 `User/App_Main.h`**

```c
#ifndef __APP_MAIN_H
#define __APP_MAIN_H

/** 应用层初始化（HAL_Init + App_System_Init + BSP_Init 之后调用） */
void App_Init(void);

/** 主循环调度框架，不返回 */
void App_Loop(void);

#endif
```

- [ ] **Step 2: 创建 `User/App_Main.c`**

```c
#include "App_Main.h"
#include "Bsp.h"
#include "Proto_Asr.h"
#include "Proto_Remote.h"
#include "App_Mode.h"
#include "App_Mode_Power.h"
#include "App_Mode_Sensor.h"
#include "App_Mode_Remote.h"
#include "App_Mode_Voice.h"
#include "App_Eye.h"
#include "App_Breath.h"
#include "App_Shutdown.h"
#include "App_Battery.h"

void App_Init(void)
{
    /* 应用层时序：等 ASRPRO 启动 + 默认进入语音模式 */
    Bsp_Tick_DelayMs(1500);
    /* v0.7 ID 17 = "你好呀我是小白进入语音模式"，本身就是合并句，
       单独播；App_Mode_Switch 用 play_voice=0 静默切避免重复 */
    Bsp_UartAsr_SendPlay(ASR_VOICE_BOOT);
    App_Mode_Switch(APP_MODE_VOICE, 0);

    /* PA9 呼吸灯默认关闭，由 wake 唤醒启动 / 15s 超时自动关 */
    App_Breath_Init();

    /* BLE 配名（BLE 上电后留 500ms） */
    Bsp_Tick_DelayMs(500);
    Bsp_UartBle_ConfigName("Spark_AI", 11);

    Proto_Remote_Init();
}

void App_Loop(void)
{
    /* PF3 连接状态边沿检测 */
    uint8_t ble_was_connected = Bsp_UartBle_IsConnected();

    /* 动作命令防抖已移除：v0.7 下 ASRPRO 侧不误连发，每条 cmd 都执行。
       依赖 ASRPRO 侧命令冗余抑制；如未来发现误识别，在此处重新加防抖窗。 */

    while (1) {
        /* --- 按键扫描（4 键各管一个模式，KEY1 长按关机） --- */
        {
            Bsp_Key_Id_t kid;
            Bsp_Key_Evt_t ke = Bsp_Key_Poll(&kid);
            if (ke == KEY_EVT_SHORT) {
                switch (kid) {
                case KEY_ID_1: App_Mode_Switch(APP_MODE_VOICE, 1);  break;  /* LED1 */
                case KEY_ID_2:  /* LED2 感应模式：已在感应模式则切玩法 */
                    if (App_Mode_Get() == APP_MODE_SENSOR) {
                        App_Mode_Sensor_OnKey();
                    } else {
                        App_Mode_Switch(APP_MODE_SENSOR, 1);
                    }
                    break;
                case KEY_ID_3: App_Mode_Switch(APP_MODE_REMOTE, 1); break;  /* LED3 */
                case KEY_ID_4:  /* LED4 动力模式：已在动力模式则切动作 */
                    if (App_Mode_Get() == APP_MODE_POWER) {
                        App_Mode_Power_OnKey();
                    } else {
                        App_Mode_Switch(APP_MODE_POWER, 1);
                    }
                    break;
                default: break;
                }
            }
            else if (ke == KEY_EVT_LONG && kid == KEY_ID_1) {
                /* === 关机流程 === */
                App_Shutdown_Execute();
            }
        }

        /* --- ASRPRO 语音命令 --- */
        {
            Bsp_UartAsr_Event_t e;
            if (Bsp_UartAsr_TryRecv(&e)) {
                if (e.type == ASR_EVT_CMD) {
                    /* 段1: 任何模式响应（关机） */
                    if (e.arg == ASR_CMD_SHUTDOWN) {
                        App_Shutdown_Execute();
                    }
                    /* 段2: 任何模式响应（切模式 4 选 1） */
                    else if (e.arg >= ASR_CMD_ENTER_POWER && e.arg <= ASR_CMD_ENTER_VOICE) {
                        switch (e.arg) {
                        case ASR_CMD_ENTER_POWER:  App_Mode_Switch(APP_MODE_POWER, 1);  break;
                        case ASR_CMD_ENTER_SENSOR: App_Mode_Switch(APP_MODE_SENSOR, 1); break;
                        case ASR_CMD_ENTER_REMOTE: App_Mode_Switch(APP_MODE_REMOTE, 1); break;
                        case ASR_CMD_ENTER_VOICE:  App_Mode_Switch(APP_MODE_VOICE, 1);  break;
                        }
                    }
                    /* 段3: 仅语音模式响应（动作命令 11 条，App_Mode_Voice 内部判模式） */
                    else if (e.arg >= ASR_CMD_FORWARD && e.arg <= ASR_CMD_R_STOP) {
                        App_Mode_Voice_OnCmd(e.arg);
                    }
                }
                else if (e.type == ASR_EVT_WAKE) {
                    /* 唤醒：启动呼吸灯（15s 后主循环超时自动关）。
                       不再发 play=10，唤醒应答由 ASRPRO 本地回复词"我在"承担，响应更快 */
                    App_Breath_Start();
                }
                else if (e.type == ASR_EVT_DONE) {
                    /* done=NN：播报完成回执，仅供参考，无需动作 */
                }
            }
        }

        /* --- BLE 连接状态边沿 + 语音播报 + 遥控帧喂协议层 --- */
        {
            uint8_t ble_now = Bsp_UartBle_IsConnected();
            if (ble_now && !ble_was_connected) {
                Bsp_UartAsr_SendPlay(ASR_VOICE_BLE_CONNECTED);
            } else if (!ble_now && ble_was_connected) {
                Bsp_UartAsr_SendPlay(ASR_VOICE_BLE_LOST);
            }
            ble_was_connected = ble_now;

            uint8_t buf[REMOTE_FRAME_LEN * 2];
            uint16_t n = Bsp_UartBle_TryRecv(buf, sizeof(buf));
            Proto_Remote_Feed(buf, n);
            uint8_t keys[REMOTE_KEY_COUNT];
            while (Proto_Remote_GetFrame(keys)) {
                App_Mode_Remote_OnFrame(keys);
            }
        }

        /* --- 各模式驱动 --- */
        App_Mode_Power_Update();
        App_Mode_Sensor_Update();
        App_Mode_Remote_Update();

        /* --- TM1640 眼睛动画（未连接慢闪 / 连接后转动）--- */
        App_Eye_Update();

        /* --- PA9 呼吸灯：wake 唤醒启动，15s 超时自动关 --- */
        App_Breath_Update();

        /* --- 电池采样 + 低电量播报 --- */
        App_Battery_Update();

        Bsp_Tick_DelayMs(5);
    }
}
```

> 注意：原 main.c 主循环顺序为"按键→语音→BLE 连接播报→遥控帧→眼睛→呼吸→动力→感应→遥控超时→电池"；本实现调整为"按键→语音→BLE(连接播报+遥控帧)→模式驱动→眼睛→呼吸→电池"，其中模式驱动/眼睛/呼吸/电池顺序与原一致（原动力/感应/遥控超时在眼睛/呼吸之后，本实现把它们合并到"模式驱动"块置于眼睛之前——块间无共享可变状态依赖，仅时间片顺序微移 ≤1 次循环（5ms），行为等价）。

- [ ] **Step 3: 修改 `User/main.c` 为纯入口**

```c
#include "main.h"
#include "Bsp.h"
#include "App_System.h"
#include "App_Main.h"

int main(void)
{
    HAL_Init();
    App_System_Init();
    BSP_Init();
    App_Init();
    App_Loop();   /* 不返回 */
}
```

（删除原 main.c 全部其余内容。）

- [ ] **Step 4: 修改 `MDK-ARM/XiaoBai.uvprojx` — User Group 加 App_Main.c**

在 App_Battery.c 条目后追加：

```xml
    <File>
      <FileName>App_Main.c</FileName>
      <FileType>1</FileType>
      <FilePath>..\User\App_Main.c</FilePath>
    </File>
```

- [ ] **Step 5: 编译验证**

Run: `UV4 -b MDK-ARM/XiaoBai.uvprojx -j0 -o build.log`
Expected: 0 Error 0 Warning。

- [ ] **Step 6: 逻辑对照验收**

Manual: 主循环事件处理顺序与各 Handler 对应关系对照原 main.c；开机时序（1500ms/BOOT/语音模式/呼吸 off/500ms/BLE 配名）逐项核对。

- [ ] **Step 7: Commit**

```bash
git add User/App_Main.c User/App_Main.h User/main.c MDK-ARM/XiaoBai.uvprojx
git commit -m "refactor: 主循环调度迁入 App_Main，main.c 瘦身为入口"
```

---

### Task 10: 死代码清理 + 文档更新 + 全量验收

**Files:**
- Modify: `BSP_Drivers/Bsp.h`（确认无协议宏残留引用）
- Modify: `doc/01-项目框架.md`（更新分层描述）
- Modify: `doc/04-开发进度.md`（记录重构完成 + 删除清单）
- Modify: `AGENTS.md`（更新分层约定：新增 Protocol/ 目录约定、新增 BSP 条目规则不变）

- [ ] **Step 1: grep 引用扫描**

Run（逐条确认无残留引用）:
```powershell
# 1) ASR_* 宏应只剩 Proto_Asr.h 定义 + 各模块 use
Select-String -Path User\*.c,BSP_Drivers\**\*.c,Protocol\*.c -Pattern 'ASR_VOICE_|ASR_CMD_'
# 2) REMOTE_* 宏应只剩 Proto_Remote.h 定义 + 各模块 use
Select-String -Path User\*.c,BSP_Drivers\**\*.c,Protocol\*.c -Pattern 'REMOTE_FRAME|REMOTE_KEY'
# 3) 确认 Bsp_UartAsr.h / Bsp_UartBle.h 无宏残留
Select-String -Path BSP_Drivers\Bsp_UartAsr\Bsp_UartAsr.h,BSP_Drivers\Bsp_UartBle\Bsp_UartBle.h -Pattern '#define ASR_|#define REMOTE_|REMOTE_KEY'
# 4) 未引用的 static 函数/表（编译 0 Warning 已隐式保证，再人工确认）
```

Expected: 宏只在 Protocol/ 定义、App/Protocol 引用；Bsp_UartAsr.h / Bsp_UartBle.h 无协议宏残留。

- [ ] **Step 2: 编译门禁复核**

Run: `UV4 -b MDK-ARM/XiaoBai.uvprojx -j0 -o build.log`
Expected: 0 Error 0 Warning（若本机无 UV4，用 Keil GUI F7 复核）。同时检查 `MDK-ARM/Output/XiaoBai.map` 确认无死代码段（如未用函数被链接器丢弃属正常）。

- [ ] **Step 3: 更新 `doc/01-项目框架.md`**

- 软件分层段更新为四层：

```markdown
## 软件分层（自底向上）

```
User/                   应用层：入口 main.c + App_Main 主循环调度 + 各职责模块
                        （App_Mode* 模式状态机 / App_Vehicle 车辆动作 /
                         App_Eye 眼睛 / App_Breath 呼吸 / App_Shutdown 关机 /
                         App_Battery 电池 / App_System 时钟）
Protocol/               协议层：Proto_Asr 语音协议（ID 宏 + cmd_to_voice 映射）、
                        Proto_Remote 遥控帧协议（帧校验 + 按键位图）
BSP_Drivers/            驱动层：每外设一子目录，Bsp.c 汇总 BSP_Init()
PY32F0xx_HAL_Driver/    普冉官方 HAL/LL（供应商代码，一般不改）
CMSIS/                  ARM 头文件 + 启动文件 startup_py32f030x6.s
```
```

- 核心模块段更新（main.c 条目改为四层描述；新增 Protocol 条目；数据流段保持）。

- [ ] **Step 4: 更新 `doc/04-开发进度.md`**

- 版本历史表追加：

```markdown
| 2026-08-04 | 四层架构重构完成：新建 Protocol/（Proto_Asr/Proto_Remote），应用层拆 11 模块，main.c 瘦身为入口；行为零变化（映射表/时序/阈值原样搬迁） |
```

- 待办/已知问题中移除"遥控帧解析闭环"待确认项（已由 Proto_Remote 闭环并板上实测确认）；追加记录：

```markdown
- [ ] 重构收尾：死代码清理清单（grep 扫描确认无残留引用后删除，本任务 Step 1 记录）
```

- [ ] **Step 5: 更新 `AGENTS.md`**

- Architecture 段：`User/main.c — 唯一应用层` 改为四层描述；`BSP_Drivers/` 条目补充 `Protocol/` 层约定（新增协议模块时在 Protocol/ 建文件并加入 Keil 工程 Protocol Group）。
- Conventions 段：语音协议 ID 权威映射位置由 `Bsp_UartAsr.h` + `cmd_to_voice[]` 更新为 `Protocol/Proto_Asr.h` + `Protocol/Proto_Asr.c` 的 cmd_to_voice 表。

- [ ] **Step 6: Commit**

```bash
git add BSP_Drivers/Bsp.h doc/01-项目框架.md doc/04-开发进度.md AGENTS.md
git commit -m "docs: 更新四层架构文档（doc/01、doc/04、AGENTS.md），记录重构完成"
```

- [ ] **Step 7: 板上实测验收（需硬件，人工执行）**

按下列清单逐项对比旧固件行为（重构前可先烧录旧 hex 记录基线）：
1. 开机：BOOT 语 → 语音模式 LED1 亮 → 呼吸灯 off；
2. 按键：KEY1 切语音 / KEY2 切感应（再按轮换 4 玩法）/ KEY3 切遥控 / KEY4 切动力（再按轮换 5 动作）/ KEY1 长按关机（关机动画+语音+断电）；
3. 语音：喊"关机"断电；"进入动力/感应/遥控/语音模式"切换；"前进/后退/左转/右转/停止/左电机正转..." 11 条动作各播报对应语音 + 电机动作；
4. BLE：连接播"遥控已连接"、断开播"遥控已断开"；遥控前进/后退/坦克转向/单电机/肩键 R1/L1 调速/松键 1s 停机；
5. 感应：靠近启动 / 遇障停止 / 挥手开关（500ms 消抖）/ 明暗调速（用手遮挡光线变速）；
6. 眼睛：未连接双眨 / 连接后瞳孔移动；呼吸灯 wake 后 15s 自动关；
7. 低电量：降压至 <3000mV 播"低电量"（5s 冷却不刷屏）。

- [ ] **Step 8: 合并回 main（重构全部完成后）**

```bash
git checkout main
git merge main-work
git push origin main
git checkout main-work
```

---

## Self-Review 记录

**1. Spec coverage（对照设计文档 10 节）：**
- §2 目标分层 → Task 1/2（Protocol）、Task 3-9（App 模块）、驱动瘦身 Task 1/2 Step 3、HAL 不动 ✓
- §3 模块职责接口 → 各 Task 的 Interfaces 块逐一落实（App_Mode_* 接口与设计一致）✓
- §4 行为红线 → 各 Task 的"逻辑对照验收"步骤覆盖全部映射表/时序/阈值 ✓
- §5 错误处理 → App_System.c 保留 APP_ErrorHandler；Bsp_Power 死循环未动 ✓
- §6 Keil 工程变更 → Task 1/2/3/4/5/7/8/9 各含 uvprojx 修改 ✓
- §7 无用文件清理 → Task 10 Step 1 grep 扫描 + doc/04 记录 ✓
- §8 验证策略 → 每任务编译门禁 + Task 10 Step 7 板上验收清单 ✓
- §9 提交策略 → 每任务 commit + Task 10 Step 8 合并 main ✓
- §10 不做清单 → 无 TinyTask/无校准/无 BSP 重命名/无新功能 ✓

**2. Placeholder scan:** 无 TBD/TODO；每个新文件均含完整代码；uvprojx XML 片段完整可粘贴。Task 5 的桩实现（App_Mode_Sensor.c/Remote.c 空 Enter）明确标注"Task 6 填充"，非占位符而是有意的编译顺序策略。

**3. Type consistency:** 
- `App_Mode_Switch/Get/IsPaused/SetPaused`（App_Mode.h）在 Task 5/6/7/9 用法一致；
- `Vehicle_Drive(dir,speed)` + `Vehicle_DriveSingle(motor,dir,speed)` 全计划一致（设计文档修正后的签名）；
- `Proto_Remote_Feed/GetFrame/Init` 在 Task 2/6/9 签名一致；`Proto_Asr_CmdToVoice` 在 Task 1/7 一致；
- `App_Mode_Remote_Enter/OnFrame/Update`、`App_Mode_Sensor_Enter/OnKey/Update` 在 Task 5（桩）/6（实现）/9 一致；
- REMOTE_KEY_* 枚举从 Bsp_UartBle.h 迁移到 Proto_Remote.h 后，所有引用处（Task 2/6/9）均指向 Proto_Remote.h ✓
