#ifndef APP_BREATH_H
#define APP_BREATH_H

/* 呼吸灯跟随 ASRPRO 唤醒状态（2026-09-16 需求）：
 *   - ASRPRO 发 wake → App_Breath_Start()
 *   - ASRPRO 发 sleep（sys_sleep_hook）→ App_Breath_Stop() 立即熄灭
 *   - 删除 MCU 本地 15s 猜测超时，亮灭完全由语音端事件驱动 */

/** 开机初始化：PA9 呼吸灯默认关闭 */
void App_Breath_Init(void);

/** 唤醒启动呼吸灯（持续到收到 sleep） */
void App_Breath_Start(void);

/** 立即熄灭呼吸灯 */
void App_Breath_Stop(void);

/** 主循环周期调用：10ms 呼吸步进（非阻塞） */
void App_Breath_Update(void);

#endif
