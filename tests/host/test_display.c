#include "test.h"
#include "host_stub.h"
#include "Bsp.h"
#include "App_Display.h"
#include "Eye_Data.h"

static void test_anim_table(void)
{
    static const uint8_t expect_frames[10] = {9, 5, 4, 5, 4, 5, 4, 4, 4, 4};

    CHECK_EQ(EYE_ANIM_COUNT, 10);
    for (int a = 0; a < 10; a++) {
        CHECK_EQ(g_eye_anims[a].count, expect_frames[a]);
        const Eye_Anim_t *anim = &g_eye_anims[a];
        for (int f = 0; f < anim->count; f++) {
            CHECK(anim->frames[f].ms > 0);
            for (unsigned c = 0; c < EYE_FRAME_COLS; c++) {
                CHECK_EQ(anim->frames[f].col[c] & 0x80U, 0);   /* bit7 恒 0 */
            }
        }
    }
}

static void test_eye01_frame0_mapping(void)
{
    /* JSON EYE_01 首帧（左右相同，rows "0000000"/"0011100"...）→ 列数据 */
    static const uint8_t expect[EYE_FRAME_COLS] = {
        0x00, 0x1C, 0x3E, 0x3E, 0x3E, 0x1C, 0x00,
        0x00, 0x1C, 0x3E, 0x3E, 0x3E, 0x1C, 0x00
    };
    for (unsigned c = 0; c < EYE_FRAME_COLS; c++) {
        CHECK_EQ(g_eye_anims[0].frames[0].col[c], expect[c]);
    }
    CHECK_EQ(g_eye_anims[0].frames[0].ms, 900);
}

static void test_default_idle_and_release(void)
{
    /* 开机默认 EYE_01 待机表情 */
    Host_Tm_Reset();
    App_Display_Init();
    for (unsigned c = 0; c < EYE_FRAME_COLS; c++) {
        CHECK_EQ(g_tm_last[c], g_eye_anims[0].frames[0].col[c]);
    }
    CHECK_EQ(g_tm_refresh_count, 1);

    /* 帧步进：EYE_01 首帧 900ms，到点切下一帧 */
    Host_Tick_Set(0);
    App_Display_ShowEye(1);
    Host_Tm_Reset();
    Host_Tick_Advance(900U);
    App_Display_Update();
    CHECK_EQ(g_tm_refresh_count, 1);
    CHECK_EQ(g_tm_last[2], g_eye_anims[0].frames[1].col[2]);

    /* 退出编程模式/手动显示后回到 EYE_01 */
    App_Display_ShowNumber(5);
    Host_Tm_Reset();
    App_Display_Release();
    for (unsigned c = 0; c < EYE_FRAME_COLS; c++) {
        CHECK_EQ(g_tm_last[c], g_eye_anims[0].frames[0].col[c]);
    }
}

static void test_number_layout(void)
{
    /* 0–9 补零成两位：0 → 左眼"0"(列 2..4) + 右眼"0"(列 9..11) */
    Host_Tm_Reset();
    App_Display_ShowNumber(0);
    for (unsigned c = 0; c < EYE_FRAME_COLS; c++) {
        uint8_t want = 0;
        if (c >= 2 && c <= 4) want = g_digit_cols[0][c - 2];
        else if (c >= 9 && c <= 11) want = g_digit_cols[0][c - 9];
        CHECK_EQ(g_tm_last[c], want);
    }

    /* 5 → "05"：左眼"0"，右眼"5" */
    Host_Tm_Reset();
    App_Display_ShowNumber(5);
    for (unsigned c = 0; c < EYE_FRAME_COLS; c++) {
        uint8_t want = 0;
        if (c >= 2 && c <= 4) want = g_digit_cols[0][c - 2];
        else if (c >= 9 && c <= 11) want = g_digit_cols[5][c - 9];
        CHECK_EQ(g_tm_last[c], want);
    }

    /* 两位数 57：十位在左眼居中（列 2..4），个位在右眼居中（列 9..11） */
    Host_Tm_Reset();
    App_Display_ShowNumber(57);
    for (unsigned c = 0; c < EYE_FRAME_COLS; c++) {
        uint8_t want = 0;
        if (c >= 2 && c <= 4) want = g_digit_cols[5][c - 2];
        else if (c >= 9 && c <= 11) want = g_digit_cols[7][c - 9];
        CHECK_EQ(g_tm_last[c], want);
    }

    /* 100：列 1..3 + 5..7 + 9..11，11 列完整放入 14 列 */
    Host_Tm_Reset();
    App_Display_ShowNumber(100);
    CHECK_EQ(g_tm_last[0], 0);
    CHECK_EQ(g_tm_last[1], g_digit_cols[1][0]);
    CHECK_EQ(g_tm_last[3], g_digit_cols[1][2]);
    CHECK_EQ(g_tm_last[4], 0);
    CHECK_EQ(g_tm_last[5], g_digit_cols[0][0]);
    CHECK_EQ(g_tm_last[8], 0);
    CHECK_EQ(g_tm_last[9], g_digit_cols[0][0]);
    CHECK_EQ(g_tm_last[11], g_digit_cols[0][2]);
    CHECK_EQ(g_tm_last[12], 0);
    CHECK_EQ(g_tm_last[13], 0);

    /* 关闭显示 */
    Host_Tm_Reset();
    App_Display_Off();
    CHECK_EQ(g_tm_clear_count, 1);
    for (unsigned c = 0; c < EYE_FRAME_COLS; c++) CHECK_EQ(g_tm_last[c], 0);
}

static void test_diag_key_map(void)
{
    /* 诊断键位图：列 0..9 = 键位 0..9，列 10 = 帧活动灯；置位整列点亮（行 0..6） */
    Host_Tm_Reset();
    App_Display_ShowKeyMap((uint16_t)((1U << 0) | (1U << 3) | (1U << 9)), 1U);
    for (unsigned c = 0; c < EYE_FRAME_COLS; c++) {
        uint8_t want = (c == 0 || c == 3 || c == 9 || c == 10) ? 0x7FU : 0x00U;
        CHECK_EQ(g_tm_last[c], want);
    }

    /* 无按键但有帧：只有活动灯亮 */
    Host_Tm_Reset();
    App_Display_ShowKeyMap(0, 1U);
    for (unsigned c = 0; c < EYE_FRAME_COLS; c++) {
        CHECK_EQ(g_tm_last[c], (c == 10) ? 0x7FU : 0x00U);
    }

    Host_Tm_Reset();
    App_Display_ShowKeyMap(0, 0);
    for (unsigned c = 0; c < EYE_FRAME_COLS; c++) CHECK_EQ(g_tm_last[c], 0);
}

static void test_digit_font(void)
{
    /* 5 行字模下移 1 行，在 7 行点阵（bit0..bit6）里垂直居中 */
    CHECK_EQ(g_digit_cols[0][0], 0x1F << 1);
    CHECK_EQ(g_digit_cols[0][1], 0x11 << 1);
    CHECK_EQ(g_digit_cols[0][2], 0x1F << 1);
    CHECK_EQ(g_digit_cols[8][1], 0x15 << 1);
    for (int d = 0; d < 10; d++) {
        uint8_t all = 0;
        for (int c = 0; c < 3; c++) {
            CHECK_EQ(g_digit_cols[d][c] & 0x81U, 0);   /* 只用 bit1..5（居中 5 行） */
            all |= g_digit_cols[d][c];
        }
        CHECK(all & 0x20U);                            /* 字模最下行落在 bit5 */
    }
}

int test_display(void)
{
    printf("[display]\n");
    test_anim_table();
    test_eye01_frame0_mapping();
    test_default_idle_and_release();
    test_number_layout();
    test_diag_key_map();
    test_digit_font();
    return g_test_fail;
}
