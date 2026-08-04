#ifndef __APP_MODE_VOICE_H
#define __APP_MODE_VOICE_H
#include <stdint.h>

/** 语音动作命令处理（仅语音模式生效）：播报对应语音 + 驱动电机 */
void App_Mode_Voice_OnCmd(uint8_t cmd);

#endif
