/* App_Mode_Remote.c（遥控模式：帧消费 + 超时停机，原 main.c 逻辑迁入，Task 6） */
#include "App_Mode_Remote.h"
#include "App_Mode.h"
#include "Bsp.h"
#include "App_Vehicle.h"

static Bsp_Motor_Speed_t g_remote_speed = MOTOR_SPEED_MID;  /* 3 档速度，默认 2 档 70% */
static uint32_t          g_last_remote_frame = 0;           /* 超时停机计时 */
static uint8_t           g_r1_was = 0, g_l1_was = 0;        /* 肩键边沿检测 */

void App_Mode_Remote_Enter(void)
{
    g_remote_speed = MOTOR_SPEED_MID;
    g_last_remote_frame = 0;
    g_r1_was = 0;
    g_l1_was = 0;
}

void App_Mode_Remote_OnFrame(const uint8_t keys[REMOTE_KEY_COUNT])
{
    if (App_Mode_Get() != APP_MODE_REMOTE) return;

    /* 肩键调速（边沿触发）：R1=速度+，L1=速度- */
    if (keys[REMOTE_KEY_R1] && !g_r1_was) {
        if (g_remote_speed < MOTOR_SPEED_HIGH) g_remote_speed++;
    }
    if (keys[REMOTE_KEY_L1] && !g_l1_was) {
        if (g_remote_speed > MOTOR_SPEED_LOW) g_remote_speed--;
    }
    g_r1_was = keys[REMOTE_KEY_R1];
    g_l1_was = keys[REMOTE_KEY_L1];

    /* 方向键优先（坦克转向），否则单电机键 */
    if (keys[REMOTE_KEY_UP]) {
        Vehicle_Drive(VEHICLE_DIR_FORWARD, g_remote_speed);
    } else if (keys[REMOTE_KEY_DOWN]) {
        Vehicle_Drive(VEHICLE_DIR_BACKWARD, g_remote_speed);
    } else if (keys[REMOTE_KEY_LEFT]) {
        Vehicle_Drive(VEHICLE_DIR_LEFT, g_remote_speed);
    } else if (keys[REMOTE_KEY_RIGHT]) {
        Vehicle_Drive(VEHICLE_DIR_RIGHT, g_remote_speed);
    } else {
        /* 单电机：Y=L正转 A=L反转 X=R正转 B=R反转 */
        if (keys[REMOTE_KEY_Y])      Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  g_remote_speed);
        else if (keys[REMOTE_KEY_A]) Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, g_remote_speed);
        else                         Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_STOP,     g_remote_speed);
        if (keys[REMOTE_KEY_X])      Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  g_remote_speed);
        else if (keys[REMOTE_KEY_B]) Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, g_remote_speed);
        else                         Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_STOP,     g_remote_speed);
    }
    g_last_remote_frame = Bsp_Tick_GetMs();
}

void App_Mode_Remote_Update(void)
{
    if (App_Mode_Get() != APP_MODE_REMOTE) return;
    if (g_last_remote_frame != 0 &&
        (Bsp_Tick_GetMs() - g_last_remote_frame > 1000)) {
        Bsp_Motor_StopAll();
    }
}
