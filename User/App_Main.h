#ifndef __APP_MAIN_H
#define __APP_MAIN_H

/** 应用层初始化（HAL_Init + App_System_Init + BSP_Init 之后调用） */
void App_Init(void);

/** 主循环调度框架，不返回 */
void App_Loop(void);

#endif
