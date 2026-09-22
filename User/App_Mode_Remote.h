#ifndef __APP_MODE_REMOTE_H
#define __APP_MODE_REMOTE_H
#include "Proto_Remote.h"

/** 进入遥控模式时重置：速度 2 档（70%）、清肩键边沿、清超时计时 */
void App_Mode_Remote_Enter(void);

/** 消费一帧有效帧：肩键调速（R1+/L1-）+ 方向键/单电机驱动（仅遥控模式生效） */
void App_Mode_Remote_OnFrame(const uint8_t keys[REMOTE_KEY_COUNT]);

/** 主循环调用：没收到帧时不动作（保持原速）；超过 2s 无任何帧才刹停
 *  （防断连电机狂转；PF3 掉线另有 App_Main 立即刹）。
 *  松开（无键帧）在 OnFrame 里立即短刹，不经过本函数。 */
void App_Mode_Remote_Update(void);

/** 诊断：取走并清零“全部合法 C1 帧”计数（每秒调用一次） */
uint16_t App_Mode_Remote_TakeFrames(void);

/** 诊断：取走并清零“含按键 C1 帧”计数（每秒调用一次） */
uint16_t App_Mode_Remote_TakeKeyed(void);

/** 诊断：取走并清零本窗口内出现过的 C1 按键位或（bit0..9） */
uint16_t App_Mode_Remote_TakeKeyMask(void);

#endif
