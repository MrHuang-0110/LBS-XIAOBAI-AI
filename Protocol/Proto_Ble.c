#include "Proto_Ble.h"
#include "Bsp.h"        /* Bsp_UartBle_Send / Bsp_Tick_GetMs */
#include <string.h>

/* 接收流缓冲：4 帧待处理窗口。溢出时丢最旧字节保同步（不丢新数据）。 */
#define STREAM_SIZE  (PROTO_BLE_LEN * 4U)

static uint8_t  s_stream[STREAM_SIZE];
static uint16_t s_len = 0;

/* D3 事件计数器 + 错误事件限流（1 秒最多一次，防坏帧风暴刷屏） */
static uint8_t  s_evt_counter = 0;
static uint8_t  s_pending_err = 0;   /* PROTO_PERR_*，0=无 */
static uint32_t s_err_ms = 0;

void Proto_Ble_Init(void)
{
    s_len = 0;
    s_evt_counter = 0;
    s_pending_err = 0;
    s_err_ms = 0;
}

void Proto_Ble_Feed(const uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        if (s_len >= STREAM_SIZE) {
            /* 缓冲满：丢最旧 1 字节并记溢出错误 */
            memmove(s_stream, &s_stream[1], STREAM_SIZE - 1U);
            s_len = STREAM_SIZE - 1U;
            if (s_pending_err == 0) s_pending_err = PROTO_PERR_OVERFLOW;
        }
        s_stream[s_len++] = buf[i];
    }
}

static void Proto_Ble_ReportPendingError(void)
{
    if (s_pending_err == 0) return;
    uint32_t now = Bsp_Tick_GetMs();
    if ((now - s_err_ms) < 1000U && s_err_ms != 0) return;
    uint8_t d[8] = {0};
    d[0] = s_pending_err;
    Proto_Ble_SendEvent(PROTO_EVT_PROTOCOL_ERR, d);
    s_pending_err = 0;
    s_err_ms = now;
}

uint8_t Proto_Ble_GetFrame(Proto_Ble_Frame_t *out)
{
    uint16_t i = 0;

    Proto_Ble_ReportPendingError();

    while ((uint16_t)(i + PROTO_BLE_LEN) <= s_len) {
        const uint8_t *p = &s_stream[i];

        if (p[0] != PROTO_BLE_HEAD) { i++; continue; }
        if (p[1] != PROTO_BLE_ADDR_APP || p[2] != PROTO_BLE_ADDR_DEV ||
            p[3] != PROTO_BLE_DATA_LEN) { i++; continue; }
        if (p[4] != PROTO_BLE_TYPE_C1 && p[4] != PROTO_BLE_TYPE_C2 &&
            p[4] != PROTO_BLE_TYPE_D2 && p[4] != PROTO_BLE_TYPE_D3) { i++; continue; }
        if (p[16] != PROTO_BLE_TAIL) { i++; continue; }

        uint8_t crc = 0;
        for (uint8_t j = 0; j < (PROTO_BLE_LEN - 2U); j++) crc += p[j];
        if (crc != p[15]) {
            if (s_pending_err == 0) s_pending_err = PROTO_PERR_CHECKSUM;
            i++;                       /* 坏帧：前进 1 字节重新同步 */
            continue;
        }

        out->type = p[4];
        memcpy(out->data, &p[5], PROTO_BLE_DATA_LEN);

        /* 消费本帧，剩余左移 */
        uint16_t remain = (uint16_t)(s_len - (i + PROTO_BLE_LEN));
        if (remain > 0) memmove(s_stream, &s_stream[i + PROTO_BLE_LEN], remain);
        s_len = remain;
        return 1;
    }

    /* 没有完整帧：丢弃已扫描过的无效前缀，避免缓冲无限膨胀 */
    if (i > 0) {
        uint16_t remain = (uint16_t)(s_len - i);
        if (remain > 0) memmove(s_stream, &s_stream[i], remain);
        s_len = remain;
    }
    return 0;
}

void Proto_Ble_SendRaw(uint8_t type, const uint8_t data[PROTO_BLE_DATA_LEN])
{
    uint8_t frame[PROTO_BLE_LEN];
    frame[0] = PROTO_BLE_HEAD;
    frame[1] = PROTO_BLE_ADDR_DEV;
    frame[2] = PROTO_BLE_ADDR_APP;
    frame[3] = PROTO_BLE_DATA_LEN;
    frame[4] = type;
    memcpy(&frame[5], data, PROTO_BLE_DATA_LEN);

    uint8_t crc = 0;
    for (uint8_t i = 0; i < (PROTO_BLE_LEN - 2U); i++) crc += frame[i];
    frame[15] = crc;
    frame[16] = PROTO_BLE_TAIL;

    Bsp_UartBle_Send(frame, PROTO_BLE_LEN);
}

void Proto_Ble_SendResponse(uint8_t seq, uint8_t opcode, uint8_t result,
                            const uint8_t data[7])
{
    uint8_t d[PROTO_BLE_DATA_LEN] = {0};
    d[0] = seq;
    d[1] = opcode;
    d[2] = result;
    for (uint8_t i = 0; i < 7U; i++) d[3 + i] = data[i];
    Proto_Ble_SendRaw(PROTO_BLE_TYPE_D2, d);
}

void Proto_Ble_SendEvent(uint8_t event, const uint8_t data[8])
{
    uint8_t d[PROTO_BLE_DATA_LEN] = {0};
    s_evt_counter++;
    if (s_evt_counter == 0) s_evt_counter = 1;   /* 1..255 循环 */
    d[0] = event;
    d[1] = s_evt_counter;
    for (uint8_t i = 0; i < 8U; i++) d[2 + i] = data[i];
    Proto_Ble_SendRaw(PROTO_BLE_TYPE_D3, d);
}

uint32_t Proto_Ble_GetU32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void Proto_Ble_C1Keys(const Proto_Ble_Frame_t *frame, uint8_t keys[PROTO_BLE_DATA_LEN])
{
    for (uint8_t i = 0; i < PROTO_BLE_DATA_LEN; i++) keys[i] = frame->data[i];
}
