#include "App_Breath.h"
#include "Bsp.h"

#define BREATH_TIMEOUT_MS  15000U

static uint8_t  g_breathing = 0;       /* 1=呼吸中 0=关闭 */
static int16_t  g_breath_val = 0;
static uint8_t  g_breath_dir = 1;      /* 1=上升 0=下降 */
static uint32_t g_breath_t   = 0;      /* 呼吸动画步进时间戳 */
static uint32_t g_breath_start = 0;    /* 呼吸灯启动时刻，用于 15s 超时关灯 */

void App_Breath_Init(void)
{
    Bsp_LedPwm_Set(LEDPWM_1, 0);
    Bsp_LedPwm_Set(LEDPWM_2, 0);
}

void App_Breath_Start(void)
{
    g_breathing = 1;
    g_breath_val = 0;
    g_breath_dir = 1;
    g_breath_t = Bsp_Tick_GetMs();
    g_breath_start = g_breath_t;
}

void App_Breath_Update(void)
{
    /* 15s 超时自动关 */
    if (g_breathing && (Bsp_Tick_GetMs() - g_breath_start >= BREATH_TIMEOUT_MS)) {
        g_breathing = 0;
        g_breath_val = 0;
        g_breath_dir = 1;
        Bsp_LedPwm_Set(LEDPWM_2, 0);
    }
    /* 10ms 步进 */
    if (g_breathing && (Bsp_Tick_GetMs() - g_breath_t >= 10)) {
        g_breath_t = Bsp_Tick_GetMs();
        if (g_breath_dir) {
            g_breath_val++;
            if (g_breath_val >= 100) { g_breath_val = 100; g_breath_dir = 0; }
        } else {
            g_breath_val--;
            if (g_breath_val <= 0) { g_breath_val = 0; g_breath_dir = 1; }
        }
        Bsp_LedPwm_Set(LEDPWM_2, (uint8_t)g_breath_val);
    }
}
