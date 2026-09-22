/* App_Mode_Remote.c（遥控模式：C1 帧消费 + 松开立即短刹 + 无帧超时刹） */
#include "App_Mode_Remote.h"
#include "App_Mode.h"
#include "Bsp.h"
#include "App_Vehicle.h"

/* 松开 = 收到无键帧，立即短刹（遥控按住期间不发无键帧，刹车不用等窗口）。
 * 注意：整段没有帧时**不**刹（那种情况保持原速，由下面超时兜底），
 * 避免遥控发帧间隙被误当成松开。 */
/* 防断连：连续这么久没有合法 C1 帧才刹停；遥控发帧偶有 1s+ 间隙，
 * 放宽到 2s 避免误刹（PF3 掉线另有 App_Main 立即刹）。 */
#define REMOTE_FRAME_TIMEOUT_MS  2000U

static Bsp_Motor_Speed_t g_remote_speed = MOTOR_SPEED_MID;  /* 3 档速度，默认 2 档 70% */
static uint32_t          g_last_remote_frame = 0;           /* 最后一次合法帧时刻 */
static uint8_t           g_r1_was = 0, g_l1_was = 0;        /* 肩键边沿检测 */

/* 诊断计数（临时定位遥控接收问题，见 App_Main.c REMOTE_RX_DIAG） */
static volatile uint16_t s_frame_count = 0;   /* 全部合法 C1 帧 */
static volatile uint16_t s_key_count   = 0;   /* 至少有一个按键按下的帧 */
static volatile uint16_t s_key_mask    = 0;   /* 本窗口内出现过的按键位或（bit0..9）*/

void App_Mode_Remote_Enter(void)
{
    g_remote_speed = MOTOR_SPEED_MID;
    g_last_remote_frame = 0;
    g_r1_was = 0;
    g_l1_was = 0;
    s_frame_count = 0;
    s_key_count = 0;
    s_key_mask = 0;
}

void App_Mode_Remote_OnFrame(const uint8_t keys[REMOTE_KEY_COUNT])
{
    if (App_Mode_Get() != APP_MODE_REMOTE) return;

    /* 肩键调速（边沿触发，2026-09-16 需求交换）：L1=加速，R1=减速 */
    if (keys[REMOTE_KEY_L1] && !g_l1_was) {
        if (g_remote_speed < MOTOR_SPEED_HIGH) g_remote_speed++;
    }
    if (keys[REMOTE_KEY_R1] && !g_r1_was) {
        if (g_remote_speed > MOTOR_SPEED_LOW) g_remote_speed--;
    }
    g_r1_was = keys[REMOTE_KEY_R1];
    g_l1_was = keys[REMOTE_KEY_L1];

    uint8_t any_key = 0;
    for (uint8_t i = 0; i < REMOTE_KEY_COUNT; i++) {
        if (keys[i]) { any_key = 1; break; }
    }

    if (any_key) {
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
            /* 单电机：Y=L正转 A=L反转 X=R正转 B=R反转；未按的一侧保持原短刹语义 */
            if (keys[REMOTE_KEY_Y])      Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  g_remote_speed);
            else if (keys[REMOTE_KEY_A]) Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, g_remote_speed);
            else                         Bsp_Motor_Brake(MOTOR_LEFT);
            if (keys[REMOTE_KEY_X])      Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  g_remote_speed);
            else if (keys[REMOTE_KEY_B]) Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, g_remote_speed);
            else                         Bsp_Motor_Brake(MOTOR_RIGHT);
        }
    } else {
        /* 无键帧 = 松开：立即短刹（同一次停止期间重复调用不会延长刹车脉冲） */
        Bsp_Motor_BrakeAll();
    }

    g_last_remote_frame = Bsp_Tick_GetMs();

    s_frame_count++;
    uint16_t mask = 0;
    for (uint8_t i = 0; i < REMOTE_KEY_COUNT; i++) {
        if (keys[i]) mask |= (uint16_t)(1U << i);
    }
    if (mask) s_key_count++;
    s_key_mask |= mask;
}

uint16_t App_Mode_Remote_TakeKeyMask(void)
{
    uint16_t v = s_key_mask;
    s_key_mask = 0;
    return v;
}

uint16_t App_Mode_Remote_TakeFrames(void)
{
    uint16_t v = s_frame_count;
    s_frame_count = 0;
    return v;
}

uint16_t App_Mode_Remote_TakeKeyed(void)
{
    uint16_t v = s_key_count;
    s_key_count = 0;
    return v;
}

void App_Mode_Remote_Update(void)
{
    if (App_Mode_Get() != APP_MODE_REMOTE) return;

    uint32_t now = Bsp_Tick_GetMs();

    /* 防断连：长时间没有合法帧才刹停。整段无帧时车保持原速，
       不因“没消息”提前刹，避免遥控发帧间隙被误当成松开。 */
    if (g_last_remote_frame != 0 &&
        (now - g_last_remote_frame > REMOTE_FRAME_TIMEOUT_MS)) {
        Bsp_Motor_BrakeAll();
    }
}
