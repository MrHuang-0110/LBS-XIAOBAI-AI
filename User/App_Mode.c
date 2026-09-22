#include "App_Mode.h"
#include "Bsp.h"
#include "Proto_Asr.h"
#include "Proto_Ble.h"
#include "App_Mode_Power.h"
#include "App_Mode_Sensor.h"
#include "App_Mode_Remote.h"

/* 模式状态（原 main.c 全局状态迁入） */
static App_Mode_t g_mode = APP_MODE_VOICE;
static uint8_t    g_mode_paused = 0;

/* 模式 → 物理 LED（2026-09-16 需求：KEY1+LED1=语音，KEY2+LED2=动力，
   KEY3+LED4=遥控，KEY4+LED3=感应）。编程模式由 App_Program 四灯跑马接管，
   此处不点单灯。 */
static const Bsp_Led_Id_t mode_led[APP_MODE_COUNT] = {
    LED_1,   /* APP_MODE_VOICE   */
    LED_2,   /* APP_MODE_POWER   */
    LED_3,   /* APP_MODE_SENSOR  */
    LED_4,   /* APP_MODE_REMOTE  */
    LED_1,   /* APP_MODE_PROGRAM（占位，实际不点） */
};

/* 模式 → 进入时播报的语音 ID（编程模式由 App_Program 播 ID 52） */
static const uint8_t mode_voice[APP_MODE_COUNT] = {
    ASR_VOICE_ENTER_VOICE, ASR_VOICE_ENTER_POWER,
    ASR_VOICE_ENTER_SENSOR, ASR_VOICE_ENTER_REMOTE,
    ASR_VOICE_ENTER_PROGRAM,
};

App_Mode_t App_Mode_Get(void)        { return g_mode; }
uint8_t    App_Mode_IsPaused(void)   { return g_mode_paused; }
void       App_Mode_SetPaused(uint8_t p) { g_mode_paused = p; }

void App_Mode_Switch(App_Mode_t new_mode, uint8_t play_voice)
{
    if (new_mode >= APP_MODE_COUNT) return;

    App_Mode_t prev = g_mode;

    Bsp_Motor_BrakeAll();      /* 模式切换统一短刹（原为滑行停止） */
    g_mode = new_mode;
    Bsp_Led_AllOff();
    if (new_mode != APP_MODE_PROGRAM) {
        Bsp_Led_On(mode_led[g_mode]);   /* 编程模式 LED 由 App_Program 跑马控制 */
    }
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

    /* D3 模式变化事件（仅真实切换时上报） */
    if (new_mode != prev) {
        uint8_t d[8] = {0};
        d[0] = (uint8_t)new_mode;
        d[1] = (uint8_t)prev;
        Proto_Ble_SendEvent(PROTO_EVT_MODE_CHANGE, d);
    }
}
