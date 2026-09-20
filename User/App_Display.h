#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H
#include <stdint.h>

/* ===== 显示层（默认待机表情 EYE_01 + App 手动显示）=====
 * 开机默认显示 EYE_01「待机」表情并按各帧 duration_ms 循环；
 * 编程模式下 App 可选择其他表情/数字/关闭显示，退出编程模式后回到 EYE_01。
 * 表情数据见 User/Eye_Data.c（由 tools/gen_eye_data.py 构建期生成）。 */

/** 初始化：立即显示 EYE_01 待机表情（默认显示） */
void App_Display_Init(void);

/** 主循环周期调用：表情帧步进（非阻塞） */
void App_Display_Update(void);

/** 显示表情 EYE_01..EYE_10（1..10），按帧时长循环到新指令 */
void App_Display_ShowEye(uint8_t id);

/** 显示数字 0..100（3×5 字库，无前导零，居中） */
void App_Display_ShowNumber(uint8_t number);

/** 关闭显示（保持关闭到新指令） */
void App_Display_Off(void);

/** 回到默认待机表情 EYE_01（退出编程模式时调用） */
void App_Display_Release(void);

#endif
