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
