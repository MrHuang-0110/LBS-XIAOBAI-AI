#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H
#include <stdint.h>

/* ===== 显示层（编程模式手动显示 + 原自动动画所有权切换）=====
 * 编程模式下 App 可选择表情/数字/关闭显示；手动显示期间禁止原 BLE 眨眼/瞳孔
 * 动画覆盖；退出编程模式调用 App_Display_Release() 恢复自动动画。
 * 表情严格按 V0.2 JSON 的 duration_ms 循环（数据见 User/Eye_Data.c）。 */

/** 初始化：默认交给 App_Eye 自动动画 */
void App_Display_Init(void);

/** 主循环周期调用：手动表情帧步进（非阻塞） */
void App_Display_Update(void);

/** 显示表情 EYE_01..EYE_10（1..10），按帧时长循环到新指令 */
void App_Display_ShowEye(uint8_t id);

/** 显示数字 0..100（3×5 字库，无前导零，居中） */
void App_Display_ShowNumber(uint8_t number);

/** 关闭显示（保持关闭到新指令） */
void App_Display_Off(void);

/** 释放手动显示，恢复 App_Eye 自动动画 */
void App_Display_Release(void);

#endif
