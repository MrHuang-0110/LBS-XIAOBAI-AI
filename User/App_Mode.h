#ifndef APP_MODE_H
#define APP_MODE_H
#include <stdint.h>

/* 5 模式：语音/动力/感应/遥控 + 编程（App 会话进入）。
 * 编程模式的 LED 跑马/任务/回报由 App_Program 负责，本模块只维护模式值。 */
typedef enum {
    APP_MODE_VOICE   = 0,
    APP_MODE_POWER   = 1,
    APP_MODE_SENSOR  = 2,
    APP_MODE_REMOTE  = 3,
    APP_MODE_PROGRAM = 4,
    APP_MODE_COUNT
} App_Mode_t;

/** 切模式：刹停电机 + 点模式 LED + 可选播报（编程模式下 LED 交 App_Program 跑马）。
 *  模式实际变化时上报 D3 MODE_CHANGE 事件。 */
void App_Mode_Switch(App_Mode_t new_mode, uint8_t play_voice);

App_Mode_t App_Mode_Get(void);
uint8_t    App_Mode_IsPaused(void);
void       App_Mode_SetPaused(uint8_t p);

#endif
