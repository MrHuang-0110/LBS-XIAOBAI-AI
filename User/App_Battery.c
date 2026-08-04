#include "App_Battery.h"
#include "Bsp.h"
#include "Proto_Asr.h"

#define LOW_BATT_REPORT_COOLDOWN_MS  5000U

void App_Battery_Update(void)
{
    /* 10ms 一次入滤波窗口 */
    static uint32_t last_batt = 0;
    uint32_t now = Bsp_Tick_GetMs();
    if (now - last_batt >= 10) {
        last_batt = now;
        Bsp_Battery_Poll();
    }

    /* 低电量周期播报：滤波+迟滞后仍处于低电量态，5s 冷却控制频率；
       电压恢复到非低电量态后，reported 保持 1，等 5s 冷却期过后若再进入
       低电量能立刻播（可接受，只在真的抖回来才响）。 */
    static uint32_t last_report = 0;
    static uint8_t  reported = 0;
    if (Bsp_Battery_IsLow()) {
        if (!reported || (now - last_report >= LOW_BATT_REPORT_COOLDOWN_MS)) {
            Bsp_UartAsr_SendPlay(ASR_VOICE_LOW_BATTERY);
            last_report = now;
            reported = 1;
        }
    }
}
