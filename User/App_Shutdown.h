#ifndef __APP_SHUTDOWN_H
#define __APP_SHUTDOWN_H

/** 完整关机流程：停电机 → 播关机语 → 关机动画 → 关 LED/TM1640 → 延时 1s → 断电。
 *  KEY1 长按和语音命令 cmd=1 共用。 */
void App_Shutdown_Execute(void);

#endif
