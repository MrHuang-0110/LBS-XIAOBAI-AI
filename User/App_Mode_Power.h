#ifndef __APP_MODE_POWER_H
#define __APP_MODE_POWER_H

/** KEY4 在动力模式内的动作切换（第二次按键起逐档轮换，首按启动前进） */
void App_Mode_Power_OnKey(void);

/** 主循环持续驱动：按当前动作档驱动电机（仅动力模式且未暂停时生效） */
void App_Mode_Power_Update(void);

#endif
