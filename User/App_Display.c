#include "App_Display.h"
#include "Bsp.h"
#include "Eye_Data.h"

typedef enum {
    DISP_EYE  = 0,   /* 表情循环（默认 EYE_01 待机） */
    DISP_NUM  = 1,   /* 手动数字（静态） */
    DISP_OFF  = 2,   /* 关闭显示 */
} Disp_Mode_t;

static Disp_Mode_t s_mode = DISP_EYE;
static uint8_t     s_eye = 1;         /* 1..10 */
static uint8_t     s_frame = 0;
static uint32_t    s_frame_ms = 0;

/* 把一位数字写到指定起始列（超出点阵宽度的部分忽略，防越界） */
static void Disp_PutDigit(uint8_t *buf, uint8_t pos, uint8_t digit)
{
    for (uint8_t j = 0; j < 3U; j++) {
        if ((uint16_t)(pos + j) < EYE_FRAME_COLS) {
            buf[pos + j] = g_digit_cols[digit][j];
        }
    }
}

/* 3×5 数字 → 14 列缓冲（点阵分左右眼各 7 列）：
 *   0–99：补零成两位——十位在左眼居中（列 2..4），个位在右眼居中（列 9..11）；
 *         个位数显示为 00–09，不单独跨中间（一位数跨中会看起来像被切开）。
 *   100 ：整体居中（列 1..11）。 */
static void Disp_DrawNumber(uint8_t *buf, uint8_t number)
{
    if (number >= 100U) {
        Disp_PutDigit(buf, 1U, 1U);
        Disp_PutDigit(buf, 5U, 0U);
        Disp_PutDigit(buf, 9U, 0U);
    } else {
        Disp_PutDigit(buf, 2U, (uint8_t)(number / 10U));
        Disp_PutDigit(buf, 9U, (uint8_t)(number % 10U));
    }
}

void App_Display_ShowEye(uint8_t id)
{
    if (id < 1U || id > EYE_ANIM_COUNT) return;

    s_mode = DISP_EYE;
    s_eye = id;
    s_frame = 0;
    s_frame_ms = Bsp_Tick_GetMs();
    Bsp_Tm1640_Refresh(g_eye_anims[id - 1U].frames[0].col);
}

void App_Display_Init(void)
{
    App_Display_ShowEye(1U);   /* 默认：EYE_01 待机表情循环 */
}

void App_Display_ShowNumber(uint8_t number)
{
    if (number > 100U) return;

    uint8_t buf[EYE_FRAME_COLS] = {0};
    Disp_DrawNumber(buf, number);

    s_mode = DISP_NUM;
    Bsp_Tm1640_Refresh(buf);
}

void App_Display_Off(void)
{
    s_mode = DISP_OFF;
    Bsp_Tm1640_Clear();
}

void App_Display_ShowKeyMap(uint16_t mask, uint8_t activity)
{
    uint8_t buf[EYE_FRAME_COLS] = {0};
    for (uint8_t i = 0; i < 10U; i++) {
        if (mask & (uint16_t)(1U << i)) buf[i] = 0x7FU;   /* 行 0..6 全高 */
    }
    if (activity) buf[10] = 0x7FU;   /* 第 11 列：窗口内收到过任意 C1 帧 */

    s_mode = DISP_NUM;      /* 静态显示：不被表情动画帧覆盖 */
    Bsp_Tm1640_Refresh(buf);
}

void App_Display_Release(void)
{
    App_Display_ShowEye(1U);   /* 退出编程模式/诊断：回到默认待机表情 */
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
