#include "App_Mode.h"
#include "Bsp.h"
#include "Proto_Asr.h"
#include "App_Mode_Power.h"
#include "App_Mode_Sensor.h"
#include "App_Mode_Remote.h"

/* 模式状态（原 main.c 全局状态迁入） */
static App_Mode_t g_mode = APP_MODE_VOICE;
static uint8_t    g_mode_paused = 0;

/* 模式 -> 对应 LED（KEY-LED 一一对应，2026-07-06 变更）：
     语音->LED1 / 感应->LED2 / 遥控->LED4 / 动力->LED3
   注：Bsp_Led 枚举名 LED_MODE_POWER 实际是 LED1(PB2)，LED_MODE_VOICE 是 LED4(PA12)，
   名称跟模式不对应，但 mode_led[] 按物理 LED 映射，逻辑正确。 */
static const Bsp_Led_Id_t mode_led[APP_MODE_COUNT] = {
    LED_MODE_POWER,   /* APP_MODE_VOICE  -> LED1 */
    LED_MODE_REMOTE,  /* APP_MODE_POWER  -> LED3 */
    LED_MODE_SENSOR,  /* APP_MODE_SENSOR -> LED2 */
    LED_MODE_VOICE,   /* APP_MODE_REMOTE -> LED4 */
};
/* 模式 -> 进入时播报的语音 ID */
static const uint8_t mode_voice[APP_MODE_COUNT] = {
    ASR_VOICE_ENTER_VOICE, ASR_VOICE_ENTER_POWER,
    ASR_VOICE_ENTER_SENSOR, ASR_VOICE_ENTER_REMOTE,
};

App_Mode_t App_Mode_Get(void)        { return g_mode; }
uint8_t    App_Mode_IsPaused(void)   { return g_mode_paused; }
void       App_Mode_SetPaused(uint8_t p) { g_mode_paused = p; }

void App_Mode_Switch(App_Mode_t new_mode, uint8_t play_voice)
{
    if (new_mode >= APP_MODE_COUNT) return;
    Bsp_Motor_StopAll();
    g_mode = new_mode;
    Bsp_Led_AllOff();
    Bsp_Led_On(mode_led[g_mode]);
    if (new_mode == APP_MODE_REMOTE) {
        App_Mode_Remote_Enter();   /* 进遥控模式默认 2 档 70% */
    }
    if (new_mode == APP_MODE_SENSOR) {
        App_Mode_Sensor_Enter();   /* 进感应模式默认玩法1，清挥手/边沿状态 */
    }
    if (new_mode == APP_MODE_POWER || new_mode == APP_MODE_SENSOR) {
        g_mode_paused = 1;   /* 进入模式后暂停，第二次按键才启动 */
    }
    if (play_voice) {
        Bsp_UartAsr_SendPlay(mode_voice[g_mode]);
        /* 不等 done，异步播报，保证按键灵敏 */
    }
}
