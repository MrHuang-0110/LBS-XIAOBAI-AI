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
