#include "test.h"
#include "host_stub.h"
#include "App_Mode.h"
#include "App_Mode_Remote.h"

/* 遥控模式：方向/单电机驱动、松开保持窗、超时刹停 */

#define KEY_UP   0U
#define KEY_Y    4U

static void enter_remote(uint32_t now)
{
    Host_Tick_Set(now);
    App_Mode_Switch(APP_MODE_REMOTE, 0);
    Host_Motor_Reset();
}

static void send_keys(const uint8_t *keys)
{
    App_Mode_Remote_OnFrame(keys);
}

static void test_remote_direction_drive(void)
{
    enter_remote(1000U);

    uint8_t keys[REMOTE_KEY_COUNT] = {0};
    keys[KEY_UP] = 1;
    send_keys(keys);

    CHECK(Host_Motor_Count(HOST_MOTOR_SET, 0) >= 1);   /* 左轮前进 */
    CHECK(Host_Motor_Count(HOST_MOTOR_SET, 1) >= 1);   /* 右轮前进 */
    CHECK_EQ(Host_Motor_Count(HOST_MOTOR_BRAKE_ALL, -1), 0);
}

static void test_remote_single_motor(void)
{
    enter_remote(1000U);

    uint8_t keys[REMOTE_KEY_COUNT] = {0};
    keys[KEY_Y] = 1;
    send_keys(keys);

    CHECK_EQ(Host_Motor_Count(HOST_MOTOR_SET, 0), 1);   /* 只驱动左轮 */
    CHECK_EQ(Host_Motor_Count(HOST_MOTOR_BRAKE, 1), 1); /* 右轮短刹（未按一侧） */
}

static void test_remote_release_stops_immediately(void)
{
    enter_remote(1000U);

    uint8_t drive[REMOTE_KEY_COUNT] = {0};
    drive[KEY_UP] = 1;
    uint8_t none[REMOTE_KEY_COUNT] = {0};

    send_keys(drive);                                  /* t=1000 带键帧 */
    Host_Motor_Reset();

    send_keys(none);                                   /* 松开：无键帧一帧就刹 */
    CHECK(Host_Motor_Count(HOST_MOTOR_BRAKE_ALL, -1) >= 1);

    /* 后续无键帧重复调用不额外计数语义由 Bsp_Motor 层保护；这里验证能被再次驱动 */
    send_keys(drive);
    Host_Motor_Reset();
    App_Mode_Remote_Update();
    CHECK_EQ(Host_Motor_Count(HOST_MOTOR_BRAKE_ALL, -1), 0);
}

static void test_remote_frame_timeout(void)
{
    enter_remote(2000U);

    uint8_t drive[REMOTE_KEY_COUNT] = {0};
    drive[KEY_UP] = 1;
    send_keys(drive);                                  /* t=2000，之后整段无帧 */

    /* 整段无帧不能按“松开”处理：1.5s 内保持原速 */
    Host_Tick_Advance(1500U);
    App_Mode_Remote_Update();
    CHECK_EQ(Host_Motor_Count(HOST_MOTOR_BRAKE_ALL, -1), 0);

    Host_Tick_Advance(700U);                           /* t=4200：距最后一帧 2.2s */
    App_Mode_Remote_Update();
    CHECK(Host_Motor_Count(HOST_MOTOR_BRAKE_ALL, -1) >= 1);   /* 防断连超时刹停 */
}

int test_remote(void)
{
    printf("[remote]\n");
    test_remote_direction_drive();
    test_remote_single_motor();
    test_remote_release_stops_immediately();
    test_remote_frame_timeout();
    return g_test_fail;
}
