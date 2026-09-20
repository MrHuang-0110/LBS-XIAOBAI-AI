#include "App_Display.h"
#include "App_Eye.h"
#include "Bsp.h"
#include "Eye_Data.h"

typedef enum {
    DISP_AUTO = 0,   /* 交回 App_Eye 自动动画（默认） */
    DISP_EYE  = 1,   /* 手动表情循环 */
    DISP_NUM  = 2,   /* 手动数字（静态） */
    DISP_OFF  = 3,   /* 关闭显示 */
} Disp_Mode_t;

static Disp_Mode_t s_mode = DISP_AUTO;
static uint8_t     s_eye = 1;         /* 1..10 */
static uint8_t     s_frame = 0;
static uint32_t    s_frame_ms = 0;

/* 3×5 数字 → 14 列缓冲：0–100 无前导零，居中（1 位起 5，2 位起 3，3 位起 1） */
static void Disp_DrawNumber(uint8_t *buf, uint8_t number)
{
    uint8_t digits[3];
    uint8_t count;

    if (number >= 100U) {
        digits[0] = 1; digits[1] = 0; digits[2] = 0; count = 3;
    } else if (number >= 10U) {
        digits[0] = (uint8_t)(number / 10U);
        digits[1] = (uint8_t)(number % 10U);
        count = 2;
    } else {
        digits[0] = number;
        count = 1;
    }

    uint8_t width = (uint8_t)(count * 3U + (count - 1U));
    uint8_t pos = (uint8_t)((EYE_FRAME_COLS - width) / 2U);

    for (uint8_t i = 0; i < count; i++) {
        for (uint8_t j = 0; j < 3U; j++) {
            buf[pos++] = g_digit_cols[digits[i]][j];
        }
        if ((uint8_t)(i + 1U) < count) pos++;   /* 1 列间隔 */
    }
}

void App_Display_Init(void)
{
    s_mode = DISP_AUTO;
    s_eye = 1;
    s_frame = 0;
    s_frame_ms = 0;
    App_Eye_SetManual(0);
}

void App_Display_ShowEye(uint8_t id)
{
    if (id < 1U || id > EYE_ANIM_COUNT) return;

    App_Eye_SetManual(1);
    s_mode = DISP_EYE;
    s_eye = id;
    s_frame = 0;
    s_frame_ms = Bsp_Tick_GetMs();
    Bsp_Tm1640_Refresh(g_eye_anims[id - 1U].frames[0].col);
}

void App_Display_ShowNumber(uint8_t number)
{
    if (number > 100U) return;

    uint8_t buf[EYE_FRAME_COLS] = {0};
    Disp_DrawNumber(buf, number);

    App_Eye_SetManual(1);
    s_mode = DISP_NUM;
    Bsp_Tm1640_Refresh(buf);
}

void App_Display_Off(void)
{
    App_Eye_SetManual(1);
    s_mode = DISP_OFF;
    Bsp_Tm1640_Clear();
}

void App_Display_Release(void)
{
    s_mode = DISP_AUTO;
    App_Eye_SetManual(0);
}

void App_Display_Update(void)
{
    if (s_mode != DISP_EYE) return;

    const Eye_Anim_t *anim = &g_eye_anims[s_eye - 1U];
    uint32_t now = Bsp_Tick_GetMs();

    if ((now - s_frame_ms) >= (uint32_t)anim->frames[s_frame].ms) {
        s_frame_ms = now;
        s_frame++;
        if (s_frame >= anim->count) s_frame = 0;
        Bsp_Tm1640_Refresh(anim->frames[s_frame].col);
    }
}
