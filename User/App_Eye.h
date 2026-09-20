#ifndef APP_EYE_H
#define APP_EYE_H
#include <stdint.h>

/** TM1640 眼睛自动动画：未连接→双眨（灵动），已连接→瞳孔移动（AI 生命力）。
 *  非阻塞，主循环周期调用；手动显示（App_Display）期间自动跳过。 */
void App_Eye_Update(void);

/**
 * @brief 显示所有权切换。
 * @param manual 1=交由手动显示（App_Display），自动动画暂停；0=恢复自动动画并立即重画当前帧。
 */
void App_Eye_SetManual(uint8_t manual);

#endif
