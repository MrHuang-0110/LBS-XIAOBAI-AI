#include "Proto_Remote.h"

/* 帧流缓冲：最多容纳 4 帧待处理数据（原 main.c 的 stream[REMOTE_FRAME_LEN*4]） */
static uint8_t  s_stream[REMOTE_FRAME_LEN * 4];
static uint16_t s_len = 0;

void Proto_Remote_Init(void)
{
    s_len = 0;
}

void Proto_Remote_Feed(const uint8_t *buf, uint16_t len)
{
    for (uint16_t k = 0; k < len; k++) {
        if (s_len < sizeof(s_stream)) s_stream[s_len++] = buf[k];
    }
}

uint8_t Proto_Remote_GetFrame(uint8_t keys_out[REMOTE_KEY_COUNT])
{
    uint16_t i = 0;
    while (i + REMOTE_FRAME_LEN <= s_len) {
        if (s_stream[i] != REMOTE_FRAME_HEAD) { i++; continue; }
        if (s_stream[i+1] != 0x97 || s_stream[i+2] != 0x98 ||
            s_stream[i+3] != 0x0A || s_stream[i+4] != 0xC1) { i++; continue; }
        if (s_stream[i + REMOTE_FRAME_LEN - 1] != REMOTE_FRAME_TAIL) { i++; continue; }
        uint8_t crc = 0;
        for (uint16_t j = 0; j < REMOTE_FRAME_LEN - 2; j++) crc += s_stream[i + j];
        if (crc != s_stream[i + REMOTE_FRAME_LEN - 2]) { i++; continue; }
        /* 帧有效：拷出按键位图（帧内偏移 5..14） */
        for (uint8_t k = 0; k < REMOTE_KEY_COUNT; k++) {
            keys_out[k] = s_stream[i + 5 + k];
        }
        /* 消费本帧，剩余字节左移（原 main.c 的搬移逻辑） */
        uint16_t remain = s_len - (i + REMOTE_FRAME_LEN);
        for (uint16_t k = 0; k < remain; k++) s_stream[k] = s_stream[i + REMOTE_FRAME_LEN + k];
        s_len = remain;
        return 1;
    }
    /* 没有完整帧：丢弃已扫描过的无效前缀（i>0 时左移），保持缓冲不膨胀 */
    if (i > 0) {
        uint16_t remain = s_len - i;
        for (uint16_t k = 0; k < remain; k++) s_stream[k] = s_stream[i + k];
        s_len = remain;
    }
    return 0;
}
