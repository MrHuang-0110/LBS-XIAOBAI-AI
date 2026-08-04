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
