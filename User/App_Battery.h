#ifndef __APP_BATTERY_H
#define __APP_BATTERY_H

/** 主循环周期调用：10ms 一次采样入滤波窗口；低电量态带 5s 冷却播报 */
void App_Battery_Update(void);

#endif
