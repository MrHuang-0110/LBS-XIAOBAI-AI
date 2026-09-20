#include "test.h"
#include "test_frames.h"
#include "host_stub.h"
#include "Proto_Ble.h"
#include "Proto_Asr.h"
#include "App_Program.h"
#include "App_Mode.h"
#include "App_Display.h"

/* 发送一条 C2 帧并全部交给执行器 */
static void send_c2(uint8_t seq, uint8_t opcode, const uint8_t args[8])
{
    uint8_t frame[17];
    mk_c2(frame, seq, opcode, args);
    Proto_Ble_Feed(frame, PROTO_BLE_LEN);
    Proto_Ble_Frame_t f;
    while (Proto_Ble_GetFrame(&f)) {
        App_Program_HandleFrame(&f);
    }
}

/* 以主循环周期 5ms 推进时间 + 持续 Update；每 300ms 模拟上位机心跳 */
static void send_c2(uint8_t seq, uint8_t opcode, const uint8_t args[8]);

static void advance_ms(uint32_t ms)
{
    static uint32_t since_hb = 0;
    for (uint32_t t = 0; t < ms; t += 5U) {
        Host_Tick_Advance(5U);
        App_Program_Update();
        since_hb += 5U;
        if (since_hb >= 300U) {
            since_hb = 0;
            uint8_t noargs[8] = {0};
            send_c2(0, PROTO_OP_HEARTBEAT, noargs);
        }
    }
}

/* 无心跳推进：仅心跳超时用例使用 */
static void advance_no_hb(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 5U) {
        Host_Tick_Advance(5U);
        App_Program_Update();
    }
}

static void reset_world(void)
{
    Host_Tick_Set(0);
    Proto_Ble_Init();
    App_Program_Init();
    App_Display_Release();
    App_Mode_Switch(APP_MODE_VOICE, 0);
    Host_Tx_Reset();
    Host_Motor_Reset();
    Host_Play_Reset();
    Host_Tm_Reset();
    Host_Led_Reset();
    Host_Ir_SetAll(4095U, 4095U, 4095U);
    g_batt_mv = 3900U;
    g_batt_low = 0;
}

static void enter_program(void)
{
    uint8_t args[8] = {0};
    send_c2(0, PROTO_OP_ENTER_PROGRAM, args);
}

static void test_enter_program(void)
{
    reset_world();
    enter_program();

    CHECK_EQ(App_Mode_Get(), APP_MODE_PROGRAM);
    CHECK_EQ(Host_Play_CountOf(52), 1);
    CHECK(Host_Motor_Count(HOST_MOTOR_BRAKE_ALL, -1) >= 1);
    CHECK_EQ(g_led_state[0], 1);              /* 跑马第一灯 LED1 */
    int ev = Host_Tx_FindData(PROTO_BLE_TYPE_D3, PROTO_EVT_MODE_CHANGE, 0xFFU, 0);
    CHECK(ev >= 0);
    if (ev >= 0) CHECK_EQ(g_tx[ev][7], APP_MODE_PROGRAM);   /* data0=新模式 */

    /* 幂等：再次进入不重复播报/不重复初始化 */
    int play_before = Host_Play_CountOf(52);
    int motor_before = g_motor_ev_count;
    enter_program();
    CHECK_EQ(App_Mode_Get(), APP_MODE_PROGRAM);
    CHECK_EQ(Host_Play_CountOf(52), play_before);
    CHECK_EQ(g_motor_ev_count, motor_before);
}

static void test_motor_timed_done_and_repeat(void)
{
    reset_world();
    enter_program();
    Host_Tx_Reset();
    Host_Motor_Reset();

    /* 左电机正转 100ms，编号 1 */
    uint8_t args[8] = {PROTO_MOTOR_LEFT, PROTO_DIR_FORWARD, 100, 0, 0, 0, 0, 0};
    send_c2(1, PROTO_OP_MOTOR_TIME, args);
    CHECK_EQ(g_motor_ev_count, 1);
    CHECK_EQ(g_motor_ev[0].op, HOST_MOTOR_SET);
    CHECK_EQ(g_motor_ev[0].id, 0);
    CHECK_EQ(g_motor_ev[0].dir, 1);
    CHECK_EQ(g_motor_ev[0].speed, 3);          /* 默认 3 档 100% */

    advance_ms(95U);
    CHECK_EQ(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 1, PROTO_OP_MOTOR_TIME, 0), -1);
    advance_ms(10U);
    CHECK(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 1, PROTO_OP_MOTOR_TIME, 0) >= 0);
    CHECK_EQ(Host_Motor_Count(HOST_MOTOR_BRAKE, 0), 1);   /* 只刹左电机 */
    CHECK_EQ(Host_Motor_Count(HOST_MOTOR_BRAKE, 1), 0);

    /* 200ms 后重报 */
    advance_ms(200U);
    CHECK(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 1, PROTO_OP_MOTOR_TIME, 1) >= 0);

    /* 下一条动作（显示关闭，无需回报）立即停止旧 DONE 重报 */
    int d2_before = 0;
    for (int i = 0; i < Host_Tx_Count(); i++) {
        if (g_tx[i][4] == PROTO_BLE_TYPE_D2 && g_tx[i][5] == 1) d2_before++;
    }
    uint8_t off[8] = {0};
    send_c2(2, PROTO_OP_SHOW_OFF, off);
    advance_ms(500U);
    int d2_after = 0;
    for (int i = 0; i < Host_Tx_Count(); i++) {
        if (g_tx[i][4] == PROTO_BLE_TYPE_D2 && g_tx[i][5] == 1) d2_after++;
    }
    CHECK_EQ(d2_after, d2_before);
}

static void test_same_seq_reexecutes(void)
{
    reset_world();
    enter_program();
    Host_Tx_Reset();
    Host_Motor_Reset();

    uint8_t args[8] = {PROTO_MOTOR_LEFT, PROTO_DIR_FORWARD, 100, 0, 0, 0, 0, 0};
    send_c2(1, PROTO_OP_MOTOR_TIME, args);
    advance_ms(100U);
    CHECK(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 1, PROTO_OP_MOTOR_TIME, 0) >= 0);

    /* 同编号同内容再次下发：必须重新执行（无幂等/无 SEQ_CONFLICT） */
    send_c2(1, PROTO_OP_MOTOR_TIME, args);
    CHECK_EQ(Host_Motor_Count(HOST_MOTOR_SET, 0), 2);
    CHECK_EQ(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 1, PROTO_OP_MOTOR_TIME, 1), -1); /* 回归报已停 */
    advance_ms(105U);
    CHECK(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 1, PROTO_OP_MOTOR_TIME, 1) >= 0);
    /* 全程无非 0 result（不存在 SEQ_CONFLICT） */
    for (int i = 0; i < Host_Tx_Count(); i++) {
        if (g_tx[i][4] == PROTO_BLE_TYPE_D2) CHECK_EQ(g_tx[i][7], PROTO_RESULT_OK);
    }
}

static void test_preemption_and_brake(void)
{
    reset_world();
    enter_program();
    Host_Tx_Reset();
    Host_Motor_Reset();

    /* 组合定时 1000ms，编号 3 */
    uint8_t mv[8] = {PROTO_MOVE_FORWARD, 0xE8, 0x03, 0, 0, 0, 0, 0};
    send_c2(3, PROTO_OP_MOVE_TIME, mv);
    advance_ms(100U);

    /* 新指令抢占：取消旧任务并刹停其控制的双电机（逐电机刹停或 BrakeAll 都算） */
    uint8_t eye[8] = {2, 0, 0, 0, 0, 0, 0, 0};
    send_c2(4, PROTO_OP_SHOW_EYE, eye);
    int both_braked = Host_Motor_Count(HOST_MOTOR_BRAKE_ALL, -1)
                    + Host_Motor_Count(HOST_MOTOR_BRAKE, 0)
                    + Host_Motor_Count(HOST_MOTOR_BRAKE, 1);
    CHECK(both_braked >= 2);

    advance_ms(1200U);
    CHECK_EQ(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 3, PROTO_OP_MOVE_TIME, 0), -1);
}

static void test_invalid_param_and_unknown_opcode(void)
{
    reset_world();
    enter_program();
    Host_Tx_Reset();
    Host_Motor_Reset();

    /* 非法方向：一次错误响应，不驱动电机 */
    uint8_t bad[8] = {PROTO_MOTOR_LEFT, 7, 100, 0, 0, 0, 0, 0};
    send_c2(1, PROTO_OP_MOTOR_TIME, bad);
    int idx = Host_Tx_FindData(PROTO_BLE_TYPE_D2, 1, PROTO_OP_MOTOR_TIME, 0);
    CHECK(idx >= 0);
    if (idx >= 0) CHECK_EQ(g_tx[idx][7], PROTO_RESULT_BAD_PARAM);
    CHECK_EQ(Host_Motor_Count(HOST_MOTOR_SET, -1), 0);

    /* 时长为 0 也非法 */
    uint8_t zero[8] = {PROTO_MOTOR_LEFT, PROTO_DIR_FORWARD, 0, 0, 0, 0, 0, 0};
    Host_Tx_Reset();
    send_c2(2, PROTO_OP_MOTOR_TIME, zero);
    CHECK(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 2, PROTO_OP_MOTOR_TIME, 0) >= 0);

    /* 未知操作码：回错误且不打断当前任务 */
    uint8_t run[8] = {PROTO_MOTOR_LEFT, PROTO_DIR_FORWARD, 150, 0, 0, 0, 0, 0};
    send_c2(3, PROTO_OP_MOTOR_TIME, run);
    uint8_t unknown[8] = {0};
    Host_Tx_Reset();
    send_c2(4, 0x99, unknown);
    idx = Host_Tx_FindData(PROTO_BLE_TYPE_D2, 4, 0x99, 0);
    CHECK(idx >= 0);
    if (idx >= 0) CHECK_EQ(g_tx[idx][7], PROTO_RESULT_BAD_OPCODE);

    advance_ms(160U);
    CHECK(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 3, PROTO_OP_MOTOR_TIME, 0) >= 0);
}

static void test_wait_ir(void)
{
    reset_world();
    enter_program();
    Host_Tx_Reset();
    Host_Motor_Reset();

    /* 中通道原始 1000 → 归一化 75；阈值 50，严格大于 */
    Host_Ir_SetAll(4095U, 1000U, 4095U);
    uint8_t args[8] = {PROTO_IR_CENTER, PROTO_IR_CMP_GT, 50, 0, 0, 0, 0, 0};
    send_c2(5, PROTO_OP_WAIT_IR, args);

    /* 连续 3 次满足才完成：20ms 采样 */
    advance_ms(20U);
    CHECK_EQ(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 5, PROTO_OP_WAIT_IR, 0), -1);
    advance_ms(20U);
    CHECK_EQ(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 5, PROTO_OP_WAIT_IR, 0), -1);
    advance_ms(20U);
    CHECK(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 5, PROTO_OP_WAIT_IR, 0) >= 0);

    /* 防抖：中间一次不满足则计数清零 */
    Host_Tx_Reset();
    Host_Ir_SetAll(4095U, 1000U, 4095U);
    send_c2(6, PROTO_OP_WAIT_IR, args);
    advance_ms(20U);                                  /* hit 1 */
    Host_Ir_SetAll(4095U, 4095U, 4095U);              /* 不满足 */
    advance_ms(20U);                                  /* reset */
    Host_Ir_SetAll(4095U, 1000U, 4095U);
    advance_ms(20U);                                  /* hit 1 */
    advance_ms(20U);                                  /* hit 2 */
    CHECK_EQ(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 6, PROTO_OP_WAIT_IR, 0), -1);
    advance_ms(20U);                                  /* hit 3 */
    CHECK(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 6, PROTO_OP_WAIT_IR, 0) >= 0);

    /* 边界：>/< 均为严格比较 */
    reset_world();
    enter_program();
    Host_Tx_Reset();
    Host_Ir_SetAll(4095U, 4095U, 4095U);              /* 中通道值 0 */
    uint8_t lt0[8] = {PROTO_IR_CENTER, PROTO_IR_CMP_LT, 0, 0, 0, 0, 0, 0};
    send_c2(7, PROTO_OP_WAIT_IR, lt0);
    advance_ms(200U);
    CHECK_EQ(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 7, PROTO_OP_WAIT_IR, 0), -1);

    Host_Ir_SetAll(4095U, 0U, 4095U);
    /* raw 0 → 100 */
    CHECK_EQ(App_Program_NormalizeIr(0U), 100);
    uint8_t gt100[8] = {PROTO_IR_CENTER, PROTO_IR_CMP_GT, 100, 0, 0, 0, 0, 0};
    Host_Tx_Reset();
    send_c2(8, PROTO_OP_WAIT_IR, gt100);
    advance_ms(200U);
    CHECK_EQ(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 8, PROTO_OP_WAIT_IR, 0), -1);

    /* 归一化：4095→0，0→100，2047→50，越界钳位 */
    CHECK_EQ(App_Program_NormalizeIr(4095U), 0);
    CHECK_EQ(App_Program_NormalizeIr(2047U), 50);
    CHECK_EQ(App_Program_NormalizeIr(5000U), 0);
}

static void test_wait_voice(void)
{
    reset_world();
    enter_program();
    Host_Tx_Reset();

    uint8_t args[8] = {1, 0, 0, 0, 0, 0, 0, 0};     /* 等 ASR_01 */
    send_c2(9, PROTO_OP_WAIT_VOICE, args);

    App_Program_OnAsrCmd(43);                        /* ASR_02：不匹配 */
    CHECK_EQ(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 9, PROTO_OP_WAIT_VOICE, 0), -1);
    App_Program_OnAsrCmd(42);                        /* ASR_01：匹配 */
    CHECK(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 9, PROTO_OP_WAIT_VOICE, 0) >= 0);

    /* 同名词条兼容：ASR_09/ASR_10 与旧命令 6/7 同为前进/后退 */
    Host_Tx_Reset();
    uint8_t w9[8] = {9, 0, 0, 0, 0, 0, 0, 0};        /* 等 ASR_09（前进） */
    send_c2(10, PROTO_OP_WAIT_VOICE, w9);
    App_Program_OnAsrCmd(ASR_CMD_FORWARD);           /* cmd=6 前进 */
    CHECK(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 10, PROTO_OP_WAIT_VOICE, 0) >= 0);
}

static void test_play_voice_mapping(void)
{
    reset_world();
    enter_program();
    Host_Play_Reset();
    Host_Tx_Reset();

    /* P01 → 播报 ID 54；P10 → 63 */
    uint8_t p01[8] = {1, 0, 0, 0, 0, 0, 0, 0};
    send_c2(11, PROTO_OP_PLAY_VOICE, p01);
    CHECK_EQ(Host_Play_CountOf(54), 1);

    uint8_t p10[8] = {10, 0, 0, 0, 0, 0, 0, 0};
    send_c2(12, PROTO_OP_PLAY_VOICE, p10);
    CHECK_EQ(Host_Play_CountOf(63), 1);

    /* 非法词条号：回一次错误响应，不播报 */
    uint8_t bad[8] = {11, 0, 0, 0, 0, 0, 0, 0};
    Host_Tx_Reset();
    send_c2(13, PROTO_OP_PLAY_VOICE, bad);
    CHECK(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 13, PROTO_OP_PLAY_VOICE, 0) >= 0);
    CHECK_EQ(Host_Play_CountOf(53), 0);
}

static void test_stop_program(void)
{
    reset_world();
    enter_program();
    Host_Tx_Reset();
    Host_Motor_Reset();

    uint8_t run[8] = {PROTO_MOTOR_LEFT, PROTO_DIR_FORWARD, 0xF4, 0x01, 0, 0, 0, 0};   /* 500ms */
    send_c2(1, PROTO_OP_MOTOR_TIME, run);
    CHECK_EQ(App_Program_TaskCode(), PROTO_TASK_MOTOR_TIME);

    uint8_t noargs[8] = {0};
    send_c2(0, PROTO_OP_STOP_PROGRAM, noargs);
    CHECK_EQ(App_Program_TaskCode(), PROTO_TASK_NONE);
    CHECK(Host_Motor_Count(HOST_MOTOR_BRAKE_ALL, -1) >= 1);
    CHECK_EQ(App_Mode_Get(), APP_MODE_PROGRAM);      /* 仍留在编程模式 */

    advance_ms(600U);
    CHECK_EQ(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 1, PROTO_OP_MOTOR_TIME, 0), -1);
}

static void test_heartbeat_timeout_and_keepalive(void)
{
    reset_world();
    enter_program();
    Host_Tx_Reset();
    Host_Play_Reset();
    Host_Motor_Reset();

    advance_no_hb(900U);                            /* 900ms 无心跳：尚未超时 */
    CHECK_EQ(App_Mode_Get(), APP_MODE_PROGRAM);

    uint8_t noargs[8] = {0};
    send_c2(0, PROTO_OP_HEARTBEAT, noargs);
    advance_no_hb(900U);
    CHECK_EQ(App_Mode_Get(), APP_MODE_PROGRAM);

    advance_no_hb(200U);                            /* 距上次心跳 >1000ms */
    CHECK_EQ(App_Mode_Get(), APP_MODE_VOICE);
    CHECK_EQ(Host_Play_CountOf(53), 1);
    CHECK(Host_Motor_Count(HOST_MOTOR_BRAKE_ALL, -1) >= 1);
    int ev = Host_Tx_FindData(PROTO_BLE_TYPE_D3, PROTO_EVT_PROGRAM_ABORT, 0xFFU, 0);
    CHECK(ev >= 0);
    if (ev >= 0) CHECK_EQ(g_tx[ev][7], PROTO_ABORT_HEARTBEAT);
}

static void test_query_status_and_read_ir(void)
{
    reset_world();
    enter_program();
    Host_Tx_Reset();

    Host_Ir_SetAll(0U, 2047U, 4095U);                 /* 100 / 50 / 0 */
    g_batt_mv = 3900U;
    uint8_t noargs[8] = {0};
    send_c2(7, PROTO_OP_QUERY_STATUS, noargs);

    int idx = Host_Tx_FindData(PROTO_BLE_TYPE_D2, 7, PROTO_OP_QUERY_STATUS, 0);
    CHECK(idx >= 0);
    if (idx >= 0) {
        CHECK_EQ(g_tx[idx][7], PROTO_RESULT_OK);
        CHECK_EQ(g_tx[idx][8], APP_MODE_PROGRAM);
        CHECK_EQ(g_tx[idx][9], 100);                  /* 左 */
        CHECK_EQ(g_tx[idx][10], 50);                  /* 中 */
        CHECK_EQ(g_tx[idx][11], 0);                   /* 右 */
        CHECK_EQ(g_tx[idx][12], 0x3C);                /* 3900mV 低字节 */
        CHECK_EQ(g_tx[idx][13], 0x0F);
        CHECK_EQ(g_tx[idx][14], 0);                   /* 无任务/无故障 */
    }

    /* READ_IR 是查询：不影响正在运行的任务 */
    uint8_t run[8] = {PROTO_MOTOR_LEFT, PROTO_DIR_FORWARD, 200, 0, 0, 0, 0, 0};
    send_c2(8, PROTO_OP_MOTOR_TIME, run);
    Host_Tx_Reset();
    send_c2(9, PROTO_OP_READ_IR, noargs);
    CHECK(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 9, PROTO_OP_READ_IR, 0) >= 0);
    advance_ms(210U);
    CHECK(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 8, PROTO_OP_MOTOR_TIME, 0) >= 0);

    /* 心跳/查询不停止 DONE 重报 */
    int d2 = Host_Tx_FindData(PROTO_BLE_TYPE_D2, 8, PROTO_OP_MOTOR_TIME, 0);
    CHECK(d2 >= 0);
    send_c2(0, PROTO_OP_HEARTBEAT, noargs);
    send_c2(10, PROTO_OP_QUERY_STATUS, noargs);
    advance_ms(210U);
    CHECK(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 8, PROTO_OP_MOTOR_TIME, 1) >= 0);
}

static void test_tick_wrap(void)
{
    reset_world();
    Host_Tick_Set(0xFFFFFF00U);
    enter_program();
    Host_Tx_Reset();
    Host_Motor_Reset();

    uint8_t run[8] = {PROTO_MOTOR_LEFT, PROTO_DIR_FORWARD, 0x2C, 0x01, 0, 0, 0, 0};   /* 300ms */
    send_c2(1, PROTO_OP_MOTOR_TIME, run);
    /* 越过 32 位回绕点 */
    for (uint32_t i = 0; i < 80U; i++) {
        Host_Tick_Advance(5U);
        App_Program_Update();
    }
    CHECK(Host_Tx_FindData(PROTO_BLE_TYPE_D2, 1, PROTO_OP_MOTOR_TIME, 0) >= 0);
}

static void test_enter_remote_and_key_exit(void)
{
    reset_world();
    enter_program();
    Host_Tx_Reset();
    Host_Play_Reset();

    uint8_t noargs[8] = {0};
    send_c2(0, PROTO_OP_ENTER_REMOTE, noargs);
    CHECK_EQ(App_Mode_Get(), APP_MODE_REMOTE);
    CHECK(Host_Play_CountOf(21) >= 1);                /* 播“遥控模式” */
    int ev = Host_Tx_FindData(PROTO_BLE_TYPE_D3, PROTO_EVT_PROGRAM_ABORT, 0xFFU, 0);
    CHECK(ev >= 0);

    /* 实体键退出：只播 ID 53，不追加目标模式播报 */
    reset_world();
    enter_program();
    Host_Play_Reset();
    App_Program_ExitTo(APP_MODE_SENSOR, PROTO_ABORT_KEY);
    CHECK_EQ(App_Mode_Get(), APP_MODE_SENSOR);
    CHECK_EQ(Host_Play_CountOf(53), 1);
    CHECK_EQ(Host_Play_CountOf(ASR_VOICE_ENTER_SENSOR), 0);
}

int test_program(void)
{
    printf("[program]\n");
    test_enter_program();
    test_motor_timed_done_and_repeat();
    test_same_seq_reexecutes();
    test_preemption_and_brake();
    test_invalid_param_and_unknown_opcode();
    test_wait_ir();
    test_wait_voice();
    test_play_voice_mapping();
    test_stop_program();
    test_heartbeat_timeout_and_keepalive();
    test_query_status_and_read_ir();
    test_tick_wrap();
    test_enter_remote_and_key_exit();
    return g_test_fail;
}
