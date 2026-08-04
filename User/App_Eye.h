#ifndef __APP_EYE_H
#define __APP_EYE_H

/** TM1640 眼睛动画：未连接→双眨（灵动），已连接→瞳孔移动（AI 生命力）。
 *  非阻塞，主循环周期调用。 */
void App_Eye_Update(void);

#endif
