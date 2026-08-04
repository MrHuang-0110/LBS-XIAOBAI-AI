#include "App_Shutdown.h"
#include "Bsp.h"
#include "Proto_Asr.h"

void App_Shutdown_Execute(void)
{
    Bsp_Motor_StopAll();
    Bsp_UartAsr_SendPlay(ASR_VOICE_SHUTDOWN);
    Bsp_Power_ShutdownAnimation();
    Bsp_LedPwm_Set(LEDPWM_1, 0);
    Bsp_LedPwm_Set(LEDPWM_2, 0);
    Bsp_Tm1640_Clear();
    Bsp_Tick_DelayMs(1000);
    Bsp_Power_ShutDown();
}
