#include "App_Mode_Power.h"
#include "App_Mode.h"
#include "Bsp.h"
#include "Proto_Asr.h"
#include "App_Vehicle.h"

/* 动力模式的 5 个动作（文档 §8） */
typedef enum {
    POWER_ACT_STOP  = 0,
    POWER_ACT_FWD   = 1,
    POWER_ACT_BACK  = 2,
    POWER_ACT_LEFT  = 3,
    POWER_ACT_RIGHT = 4,
    POWER_ACT_COUNT
} Power_Action_t;

static Power_Action_t g_power_action = POWER_ACT_STOP;

/* 动力动作 -> 语音 ID（文档 §7） */
static const uint8_t act_voice[POWER_ACT_COUNT] = {
    ASR_VOICE_STOP, ASR_VOICE_FORWARD, ASR_VOICE_BACKWARD,
    ASR_VOICE_LEFT, ASR_VOICE_RIGHT,
};

void App_Mode_Power_OnKey(void)
{
    if (App_Mode_IsPaused()) {
        App_Mode_SetPaused(0);               /* 第一次按键：启动 */
        g_power_action = POWER_ACT_FWD;      /* 从前进开始 */
    } else {
        g_power_action = (Power_Action_t)((g_power_action + 1) % POWER_ACT_COUNT);
    }
    Bsp_Motor_StopAll();
    Bsp_UartAsr_SendPlay(act_voice[g_power_action]);
}

void App_Mode_Power_Update(void)
{
    if (App_Mode_Get() != APP_MODE_POWER || App_Mode_IsPaused()) return;
    switch (g_power_action) {
    case POWER_ACT_STOP:  Vehicle_Drive(VEHICLE_DIR_STOP,     MOTOR_SPEED_HIGH); break;
    case POWER_ACT_FWD:   Vehicle_Drive(VEHICLE_DIR_FORWARD,  MOTOR_SPEED_HIGH); break;
    case POWER_ACT_BACK:  Vehicle_Drive(VEHICLE_DIR_BACKWARD, MOTOR_SPEED_HIGH); break;
    case POWER_ACT_LEFT:  Vehicle_Drive(VEHICLE_DIR_LEFT,     MOTOR_SPEED_HIGH); break;
    case POWER_ACT_RIGHT: Vehicle_Drive(VEHICLE_DIR_RIGHT,    MOTOR_SPEED_HIGH); break;
    default: break;
    }
}
