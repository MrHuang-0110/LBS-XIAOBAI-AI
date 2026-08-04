/* App_Mode_Sensor.c（感应模式：4 玩法驱动，原 main.c 逻辑迁入，Task 6） */
#include "App_Mode_Sensor.h"
#include "App_Mode.h"
#include "Bsp.h"
#include "Proto_Asr.h"
#include "App_Vehicle.h"

/* 感应模式的 4 个玩法（文档 §9） */
typedef enum {
    SENSOR_PLAY_APPROACH   = 0,  /* 靠近启动 */
    SENSOR_PLAY_OBSTACLE   = 1,  /* 遇障停止 */
    SENSOR_PLAY_WAVE       = 2,  /* 挥手开关 */
    SENSOR_PLAY_BRIGHTNESS = 3,  /* 明暗调速 */
    SENSOR_PLAY_COUNT
} Sensor_Play_t;

/* 红外反射阈值：ADC < 3000 = 有反射（遮挡时~200，无反射~4000）。
   方案A硬编码，量产不需要用户校准。 */
#define IR_THRESHOLD  3000U

static Sensor_Play_t g_sensor_play = SENSOR_PLAY_APPROACH;
static uint8_t       g_wave_on = 0;            /* 挥手开关的当前开/关状态 */
static uint8_t       g_ir1_was = 0, g_ir2_was = 0, g_ir3_was = 0;  /* 挥手边沿检测 */

/* 感应玩法 -> 语音 ID（文档 §7） */
static const uint8_t sensor_voice[SENSOR_PLAY_COUNT] = {
    ASR_VOICE_APPROACH_GO, ASR_VOICE_OBSTACLE_STOP,
    ASR_VOICE_WAVE_TOGGLE, ASR_VOICE_BRIGHTNESS,
};

void App_Mode_Sensor_Enter(void)
{
    g_sensor_play = SENSOR_PLAY_APPROACH;
    g_wave_on = 0;
    g_ir1_was = 0;
    g_ir2_was = 0;
    g_ir3_was = 0;
}

void App_Mode_Sensor_OnKey(void)
{
    if (App_Mode_IsPaused()) {
        App_Mode_SetPaused(0);   /* 第一次按键：启动第一个玩法 */
        Bsp_UartAsr_SendPlay(sensor_voice[g_sensor_play]);
    } else {
        g_sensor_play = (Sensor_Play_t)((g_sensor_play + 1) % SENSOR_PLAY_COUNT);
        Bsp_Motor_StopAll();
        g_wave_on = 0;
        Bsp_UartAsr_SendPlay(sensor_voice[g_sensor_play]);
    }
}

void App_Mode_Sensor_Update(void)
{
    if (App_Mode_Get() != APP_MODE_SENSOR || App_Mode_IsPaused()) return;

    uint16_t ir1 = Bsp_IR_ReadCh1();
    uint16_t ir2 = Bsp_IR_ReadCh2();
    uint16_t ir3 = Bsp_IR_ReadCh3();
    uint8_t ir1_trig = (ir1 < IR_THRESHOLD);  /* 有反射=遮挡 */
    uint8_t ir2_trig = (ir2 < IR_THRESHOLD);
    uint8_t ir3_trig = (ir3 < IR_THRESHOLD);

    switch (g_sensor_play) {
    case SENSOR_PLAY_APPROACH:
        /* 靠近启动：有物体前进，无物体停 */
        if (ir2_trig) Vehicle_Drive(VEHICLE_DIR_FORWARD, MOTOR_SPEED_MID);
        else          Vehicle_Drive(VEHICLE_DIR_STOP,    MOTOR_SPEED_MID);
        break;
    case SENSOR_PLAY_OBSTACLE:
        /* 遇障停止：前进，遇障碍停 */
        if (ir2_trig) Vehicle_Drive(VEHICLE_DIR_STOP,    MOTOR_SPEED_MID);
        else          Vehicle_Drive(VEHICLE_DIR_FORWARD, MOTOR_SPEED_MID);
        break;
    case SENSOR_PLAY_WAVE:
        /* 挥手开关：IR1/IR2/IR3 任一检测到手（下降沿）→ 切换，500ms 消抖 */
        {
            static uint32_t last_wave = 0;
            uint8_t any_edge = (ir1_trig && !g_ir1_was) ||
                               (ir2_trig && !g_ir2_was) ||
                               (ir3_trig && !g_ir3_was);
            if (any_edge && (Bsp_Tick_GetMs() - last_wave > 500)) {
                g_wave_on = !g_wave_on;
                last_wave = Bsp_Tick_GetMs();
            }
        }
        if (g_wave_on) Vehicle_Drive(VEHICLE_DIR_FORWARD, MOTOR_SPEED_MID);
        else           Vehicle_Drive(VEHICLE_DIR_STOP,    MOTOR_SPEED_MID);
        break;
    case SENSOR_PLAY_BRIGHTNESS:
        /* 明暗调速：反射越强（值越小）速度越快 */
        if (ir2 < 500) {
            Vehicle_Drive(VEHICLE_DIR_FORWARD, MOTOR_SPEED_HIGH);
        } else if (ir2 < 1000) {
            Vehicle_Drive(VEHICLE_DIR_FORWARD, MOTOR_SPEED_MID);
        } else if (ir2 < IR_THRESHOLD) {
            Vehicle_Drive(VEHICLE_DIR_FORWARD, MOTOR_SPEED_LOW);
        } else {
            Vehicle_Drive(VEHICLE_DIR_STOP, MOTOR_SPEED_LOW);
        }
        break;
    default: break;
    }
    g_ir1_was = ir1_trig;
    g_ir2_was = ir2_trig;
    g_ir3_was = ir3_trig;
}
