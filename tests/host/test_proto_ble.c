#include "test.h"
#include "test_frames.h"
#include "golden_frames.h"
#include "host_stub.h"
#include "Proto_Ble.h"

static void test_golden_parse(void)
{
    for (int k = 0; k < (int)GOLDEN_FRAME_COUNT; k++) {
        const uint8_t *g = k_golden_frames[k].bytes;
        /* 设备侧解析器只接收 App→设备帧（D2/D3 为设备→App，由编码用例覆盖） */
        if (g[1] != PROTO_BLE_ADDR_APP) continue;
        Proto_Ble_Init();
        Host_Tx_Reset();
        Proto_Ble_Frame_t f;
        CHECK_EQ(feed_one(g, &f), 1);
        CHECK_EQ(f.type, g[4]);
        for (int i = 0; i < 10; i++) {
            CHECK_EQ(f.data[i], g[5 + i]);
        }
    }
}

static void test_golden_parse_bytewise(void)
{
    Proto_Ble_Init();
    Proto_Ble_Frame_t f;
    const uint8_t *g = k_golden_frames[1].bytes;   /* C2 MOTOR_TIME */
    for (int i = 0; i < 16; i++) {
        Proto_Ble_Feed(&g[i], 1);
        CHECK_EQ(Proto_Ble_GetFrame(&f), 0);
    }
    Proto_Ble_Feed(&g[16], 1);
    CHECK_EQ(Proto_Ble_GetFrame(&f), 1);
    CHECK_EQ(f.type, PROTO_BLE_TYPE_C2);
    CHECK_EQ(f.data[0], 1);
    CHECK_EQ(f.data[1], PROTO_OP_MOTOR_TIME);
}

static void test_encoder_matches_golden(void)
{
    /* D2 DONE seq=1 op=0x10 */
    Proto_Ble_Init();
    Host_Tx_Reset();
    uint8_t zeros[7] = {0};
    Proto_Ble_SendResponse(1, PROTO_OP_MOTOR_TIME, PROTO_RESULT_OK, zeros);
    CHECK_EQ(Host_Tx_Count(), 1);
    CHECK(memcmp(g_tx[0], k_golden_frames[6].bytes, 17) == 0);

    /* D2 QUERY_STATUS 响应 */
    Host_Tx_Reset();
    uint8_t q[7] = {0, 10, 20, 30, 0x3C, 0x0F, 0x00};
    Proto_Ble_SendResponse(0, PROTO_OP_QUERY_STATUS, PROTO_RESULT_OK, q);
    CHECK_EQ(Host_Tx_Count(), 1);
    CHECK(memcmp(g_tx[0], k_golden_frames[7].bytes, 17) == 0);

    /* D3 MODE_CHANGE（首事件 counter=1） */
    Proto_Ble_Init();
    Host_Tx_Reset();
    uint8_t d[8] = {4, 0, 0, 0, 0, 0, 0, 0};
    Proto_Ble_SendEvent(PROTO_EVT_MODE_CHANGE, d);
    CHECK_EQ(Host_Tx_Count(), 1);
    CHECK(memcmp(g_tx[0], k_golden_frames[8].bytes, 17) == 0);

    /* D3 PROTOCOL_ERROR（首事件 counter=1） */
    Proto_Ble_Init();
    Host_Tx_Reset();
    uint8_t e[8] = {2, 0, 0, 0, 0, 0, 0, 0};
    Proto_Ble_SendEvent(PROTO_EVT_PROTOCOL_ERR, e);
    CHECK(memcmp(g_tx[0], k_golden_frames[9].bytes, 17) == 0);
}

static void test_bad_checksum_resync(void)
{
    Proto_Ble_Init();
    Proto_Ble_Frame_t f;
    uint8_t bad[17];
    memcpy(bad, k_golden_frames[1].bytes, 17);
    bad[15] ^= 0xFFU;
    CHECK_EQ(feed_one(bad, &f), 0);
    /* 坏帧后紧跟有效帧：必须重新同步并取到 */
    CHECK_EQ(feed_one(k_golden_frames[3].bytes, &f), 1);
    CHECK_EQ(f.type, PROTO_BLE_TYPE_C2);
    CHECK_EQ(f.data[1], PROTO_OP_WAIT_IR);
}

static void test_noise_prefix_and_glued(void)
{
    Proto_Ble_Init();
    Proto_Ble_Frame_t f;
    uint8_t noise[5] = {0x00, 0x5A, 0x97, 0x11, 0xA5};
    Proto_Ble_Feed(noise, sizeof(noise));
    Proto_Ble_Feed(k_golden_frames[0].bytes, 17);
    Proto_Ble_Feed(k_golden_frames[2].bytes, 17);   /* 粘包：两帧连续 */
    CHECK_EQ(Proto_Ble_GetFrame(&f), 1);
    CHECK_EQ(f.type, PROTO_BLE_TYPE_C1);
    CHECK_EQ(f.data[0], 1);
    CHECK_EQ(Proto_Ble_GetFrame(&f), 1);
    CHECK_EQ(f.type, PROTO_BLE_TYPE_C2);
    CHECK_EQ(f.data[1], PROTO_OP_ENTER_PROGRAM);
    CHECK_EQ(Proto_Ble_GetFrame(&f), 0);
}

static void test_unknown_type_ignored(void)
{
    Proto_Ble_Init();
    Proto_Ble_Frame_t f;
    uint8_t data[10] = {0};
    uint8_t frame[17];
    mk_frame(frame, PROTO_BLE_ADDR_APP, PROTO_BLE_ADDR_DEV, 0xEE, data);
    CHECK_EQ(feed_one(frame, &f), 0);
    /* 未知类型后仍能取到合法帧 */
    CHECK_EQ(feed_one(k_golden_frames[2].bytes, &f), 1);
}

static void test_overflow_recovery(void)
{
    Proto_Ble_Init();
    Proto_Ble_Frame_t f;
    uint8_t junk[200];
    memset(junk, 0x11, sizeof(junk));
    Proto_Ble_Feed(junk, sizeof(junk));             /* 溢出：丢最旧保同步 */
    Proto_Ble_Feed(k_golden_frames[1].bytes, 17);   /* 紧接有效帧 */
    CHECK_EQ(Proto_Ble_GetFrame(&f), 1);
    CHECK_EQ(f.type, PROTO_BLE_TYPE_C2);
    CHECK_EQ(f.data[0], 1);
}

static void test_c1_keys_and_u32(void)
{
    Proto_Ble_Init();
    Proto_Ble_Frame_t f;
    CHECK_EQ(feed_one(k_golden_frames[0].bytes, &f), 1);
    uint8_t keys[10];
    Proto_Ble_C1Keys(&f, keys);
    CHECK_EQ(keys[0], 1);      /* KeyUp */
    CHECK_EQ(keys[9], 0);

    uint8_t le[4] = {0xE8, 0x03, 0x01, 0x00};
    CHECK_EQ(Proto_Ble_GetU32(le), 0x000103E8U);
}

int test_proto_ble(void)
{
    printf("[proto_ble]\n");
    test_golden_parse();
    test_golden_parse_bytewise();
    test_encoder_matches_golden();
    test_bad_checksum_resync();
    test_noise_prefix_and_glued();
    test_unknown_type_ignored();
    test_overflow_recovery();
    test_c1_keys_and_u32();
    return g_test_fail;
}
