#include "App_Eye.h"
#include "Bsp.h"

/* TM1640 眼睛图案（8×14 点阵，左眼列0-6 / 右眼列7-13，各 7×8）
   椭圆形空心轮廓，眨眼=行3一条横线。 */
static const uint8_t eye_box[14] = {
    0x3C, 0x42, 0x81, 0x81, 0x81, 0x42, 0x3C,   /* 左眼 列0-6 椭圆轮廓 */
    0x3C, 0x42, 0x81, 0x81, 0x81, 0x42, 0x3C    /* 右眼 列7-13 椭圆轮廓 */
};
static const uint8_t eye_closed[14] = {
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,   /* 左眼闭 列0-6 行3 */
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08    /* 右眼闭 列7-13 行3 */
};
/* 未连接：双眨（睁2s → 闭100ms → 睁150ms → 闭100ms → 睁2s）*/
static const struct { uint16_t ms; uint8_t closed; } blink_idle[] = {
    {2000, 0}, {100, 1}, {150, 0}, {100, 1}, {2000, 0}
};
#define BLINK_IDLE_LEN (sizeof(blink_idle)/sizeof(blink_idle[0]))
/* 已连接：瞳孔移动（看左上 → 回中 → 看右上 → 回中）。
   瞳孔 3×3 实心方块：上方=行1-3(bit0x0E)，中=行3-5(bit0x38)，叠加 3 列宽。 */
static const uint8_t pupil_bit[3] = {0x0E, 0x38, 0x0E};
static const uint8_t pupil_lc[3]  = {1, 2, 4};       /* 左眼瞳孔起始列(0-6内) */
static const uint8_t pupil_rc[3]  = {8, 9, 11};      /* 右眼瞳孔起始列(7-13内) */
static const struct { uint16_t ms; uint8_t pos; } look_seq[] = {
    {500, 0}, {200, 1}, {500, 2}, {200, 1}
};
#define LOOK_LEN (sizeof(look_seq)/sizeof(look_seq[0]))

void App_Eye_Update(void)
{
    static uint32_t last_t = 0;
    static uint8_t  frame = 0;
    static uint8_t  was_connected = 0xFF;
    uint8_t connected = Bsp_UartBle_IsConnected();
    uint32_t now = Bsp_Tick_GetMs();

    /* 连接状态切换时重置，立刻显示第一帧 */
    if (connected != was_connected) {
        last_t = now;
        frame = 0;
        was_connected = connected;
        if (connected) {
            uint8_t buf[14];
            for (int i = 0; i < 14; i++) buf[i] = eye_box[i];
            for (int c = 0; c < 3; c++) {
                buf[pupil_lc[0] + c] |= pupil_bit[0];
                buf[pupil_rc[0] + c] |= pupil_bit[0];
            }
            Bsp_Tm1640_Refresh(buf);
        } else {
            Bsp_Tm1640_Refresh(eye_box);
        }
        return;
    }

    if (connected) {
        /* 瞳孔移动：看左上 → 回中 → 看右上 → 回中 */
        if (now - last_t >= look_seq[frame].ms) {
            last_t = now;
            frame = (uint8_t)((frame + 1) % LOOK_LEN);
            uint8_t buf[14];
            for (int i = 0; i < 14; i++) buf[i] = eye_box[i];
            uint8_t p = look_seq[frame].pos;
            for (int c = 0; c < 3; c++) {
                buf[pupil_lc[p] + c] |= pupil_bit[p];
                buf[pupil_rc[p] + c] |= pupil_bit[p];
            }
            Bsp_Tm1640_Refresh(buf);
        }
    } else {
        /* 双眨 */
        if (now - last_t >= blink_idle[frame].ms) {
            last_t = now;
            frame = (uint8_t)((frame + 1) % BLINK_IDLE_LEN);
            if (blink_idle[frame].closed) Bsp_Tm1640_Refresh(eye_closed);
            else                          Bsp_Tm1640_Refresh(eye_box);
        }
    }
}
