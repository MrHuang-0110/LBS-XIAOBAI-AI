#ifndef BSP_LED_H
#define BSP_LED_H
#include "py32f0xx_hal.h"

/* 物理 LED 枚举（2026-09-16 起改为物理编号，消除历史“枚举名≠引脚/模式”错位）。
 * 逻辑模式 → LED 的映射统一在 App_Mode.c 的 mode_led[] 维护。 */
typedef enum {
    LED_1 = 0,   /* PB2  */
    LED_2 = 1,   /* PA10 */
    LED_3 = 2,   /* PA11 */
    LED_4 = 3,   /* PA12 */
    LED_COUNT
} Bsp_Led_Id_t;

void Bsp_Led_Init(void);
void Bsp_Led_On(Bsp_Led_Id_t id);
void Bsp_Led_Off(Bsp_Led_Id_t id);
void Bsp_Led_Toggle(Bsp_Led_Id_t id);
void Bsp_Led_AllOff(void);

#endif
