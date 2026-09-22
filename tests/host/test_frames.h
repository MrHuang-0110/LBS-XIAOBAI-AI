#ifndef TEST_FRAMES_H
#define TEST_FRAMES_H
#include <stdint.h>
#include <string.h>
#include "Proto_Ble.h"

/* 组一帧 17 字节（与 C 端编码器独立实现，用于喂给解析器） */
static inline void mk_frame(uint8_t *f, uint8_t src, uint8_t dst,
                            uint8_t type, const uint8_t data[10])
{
    f[0] = 0x5AU;
    f[1] = src;
    f[2] = dst;
    f[3] = 0x0AU;
    f[4] = type;
    memcpy(&f[5], data, 10);
    uint8_t crc = 0;
    for (int i = 0; i < 15; i++) crc = (uint8_t)(crc + f[i]);
    f[15] = crc;
    f[16] = 0xA5U;
}

/* App→设备 C2：[seq, opcode, args0..7] */
static inline void mk_c2(uint8_t *f, uint8_t seq, uint8_t opcode, const uint8_t args[8])
{
    uint8_t d[10];
    d[0] = seq;
    d[1] = opcode;
    memcpy(&d[2], args, 8);
    mk_frame(f, PROTO_BLE_ADDR_APP, PROTO_BLE_ADDR_DEV, PROTO_BLE_TYPE_C2, d);
}

/* 把一帧喂进协议层并取出（返回取到的帧数） */
static inline int feed_one(const uint8_t *f, Proto_Ble_Frame_t *out)
{
    Proto_Ble_Feed(f, PROTO_BLE_LEN);
    return Proto_Ble_GetFrame(out);
}

#endif
