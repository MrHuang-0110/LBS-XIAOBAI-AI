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
