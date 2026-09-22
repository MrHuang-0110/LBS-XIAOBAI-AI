#ifndef BSP_UARTBLE_RX_H
#define BSP_UARTBLE_RX_H
#include <stdint.h>

/* 环形 DMA 增量拷贝计划（纯逻辑，便于宿主测试）。
 *
 * 把 [last_pos, cur) 的环形区间映射成至多两段连续内存拷贝，并受目标缓冲
 * 剩余空间 space 限制。take1 从 g_rx[last_pos] 起，take2 从 g_rx[0] 起。 */
typedef struct {
    uint16_t take1;   /* 从 last_pos 连续拷贝的字节数 */
    uint16_t take2;   /* 跨环后从 0 连续拷贝的字节数 */
    uint16_t next;    /* 处理完后的新 last_pos（始终等于 cur） */
    uint16_t total;   /* take1 + take2（受 space 截断后的实际拷贝量） */
} Bsp_UartBle_RxPlan_t;

static inline Bsp_UartBle_RxPlan_t Bsp_UartBle_RxPlan(uint16_t last_pos, uint16_t cur,
                                                      uint16_t buf_size, uint16_t space)
{
    Bsp_UartBle_RxPlan_t plan = {0, 0, cur, 0};
    if (cur == last_pos) return plan;

    uint16_t seg1, seg2;
    if (cur > last_pos) {
        seg1 = (uint16_t)(cur - last_pos);
        seg2 = 0;
    } else {
        seg1 = (uint16_t)(buf_size - last_pos);
        seg2 = cur;
    }

    uint16_t need = (uint16_t)(seg1 + seg2);
    if (need > space) need = space;              /* 目标缓冲不够：丢弃多余字节 */
    plan.take1 = (need > seg1) ? seg1 : need;
    plan.take2 = (uint16_t)(need - plan.take1);
    plan.total = need;
    return plan;
}

#endif
