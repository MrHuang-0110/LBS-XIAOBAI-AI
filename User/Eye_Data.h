/* 本文件由 tools/gen_eye_data.py 自动生成，请勿手改。
 * 来源：docs/眼睛点阵动画V02/.../V0.2动画帧.json（48 帧，10 组表情）。
 * 每列 1 字节：bit0..bit6 = 行 0..6，bit7 恒 0；列 0..6 左眼，列 7..13 右眼。 */
#ifndef EYE_DATA_H
#define EYE_DATA_H

#include <stdint.h>

#define EYE_ANIM_COUNT  10U
#define EYE_FRAME_COLS  14U

typedef struct {
    uint16_t ms;                   /* 该帧持续毫秒（来自 JSON duration_ms） */
    uint8_t  col[EYE_FRAME_COLS];  /* TM1640 列数据 */
} Eye_Frame_t;

typedef struct {
    const Eye_Frame_t *frames;
    uint8_t            count;
} Eye_Anim_t;

/* 索引 0..9 = EYE_01..EYE_10 */
extern const Eye_Anim_t g_eye_anims[EYE_ANIM_COUNT];

/* 3×5 数字字库：每数字 3 列，bit0..bit4 = 行 0..4 */
extern const uint8_t g_digit_cols[10][3];

#endif
