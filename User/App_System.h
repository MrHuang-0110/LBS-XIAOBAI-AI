#ifndef __APP_SYSTEM_H
#define __APP_SYSTEM_H

/** 系统时钟初始化：HSI 24MHz → PLL → 48MHz 系统时钟。
 *  必须 HAL_Init() 之后、BSP_Init() 之前调用。失败进入 APP_ErrorHandler 死循环。 */
void App_System_Init(void);

#endif
