#ifndef __APP_BREATH_H
#define __APP_BREATH_H

/** 开机初始化：PA9 呼吸灯默认关闭 */
void App_Breath_Init(void);

/** wake 唤醒启动呼吸灯（15s 后 Update 自动关） */
void App_Breath_Start(void);

/** 主循环周期调用：15s 超时自关 + 10ms 呼吸步进（非阻塞） */
void App_Breath_Update(void);

#endif
