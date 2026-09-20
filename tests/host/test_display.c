#include "test.h"
#include "host_stub.h"
#include "Bsp.h"
#include "App_Display.h"
#include "App_Eye.h"
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

static void test_show_eye_owns_display(void)
{
    Host_Tm_Reset();
    App_Display_ShowEye(2);
    for (unsigned c = 0; c < EYE_FRAME_COLS; c++) {
        CHECK_EQ(g_tm_last[c], g_eye_anims[1].frames[0].col[c]);
    }
    CHECK_EQ(g_tm_refresh_count, 1);

    /* 手动显示期间自动动画不再覆盖 */
    Host_Tm_Reset();
    Host_Tick_Advance(5000U);
    App_Eye_Update();
    CHECK_EQ(g_tm_refresh_count, 0);

    /* 释放后自动动画恢复（立即重画一帧） */
    App_Display_Release();
    Host_Tm_Reset();
    App_Eye_Update();
    CHECK_EQ(g_tm_refresh_count, 1);
}

static void test_number_layout(void)
{
    /* 单数字 0：居中在列 5..7 */
    Host_Tm_Reset();
    App_Display_ShowNumber(0);
    for (unsigned c = 0; c < EYE_FRAME_COLS; c++) {
        uint8_t want = 0;
        if (c >= 5 && c <= 7) want = g_digit_cols[0][c - 5];
        CHECK_EQ(g_tm_last[c], want);
    }

    /* 两位数 57：列 3..5 + 7..9 */
    Host_Tm_Reset();
    App_Display_ShowNumber(57);
    CHECK_EQ(g_tm_last[3], g_digit_cols[5][0]);
    CHECK_EQ(g_tm_last[4], g_digit_cols[5][1]);
    CHECK_EQ(g_tm_last[5], g_digit_cols[5][2]);
    CHECK_EQ(g_tm_last[6], 0);
    CHECK_EQ(g_tm_last[7], g_digit_cols[7][0]);
    CHECK_EQ(g_tm_last[9], g_digit_cols[7][2]);

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

static void test_digit_font(void)
{
    CHECK_EQ(g_digit_cols[0][0], 0x1F);
    CHECK_EQ(g_digit_cols[0][1], 0x11);
    CHECK_EQ(g_digit_cols[0][2], 0x1F);
    CHECK_EQ(g_digit_cols[8][1], 0x15);
    for (int d = 0; d < 10; d++) {
        for (int c = 0; c < 3; c++) {
            CHECK_EQ(g_digit_cols[d][c] & 0xE0U, 0);   /* 只用 bit0..4（5 行） */
        }
    }
}

int test_display(void)
{
    printf("[display]\n");
    test_anim_table();
    test_eye01_frame0_mapping();
    test_show_eye_owns_display();
    test_number_layout();
    test_digit_font();
    return g_test_fail;
}
