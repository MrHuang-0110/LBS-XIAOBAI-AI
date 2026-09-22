#include "test.h"
#include "Bsp_UartBle/Bsp_UartBle_Rx.h"

/* DMA 环形增量拷贝计划：前进/绕环/目标缓冲截断/无数据 四种边界 */

static void test_no_delta(void)
{
    Bsp_UartBle_RxPlan_t p = Bsp_UartBle_RxPlan(10U, 10U, 128U, 100U);
    CHECK_EQ(p.take1, 0);
    CHECK_EQ(p.take2, 0);
    CHECK_EQ(p.total, 0);
    CHECK_EQ(p.next, 10);
}

static void test_forward_span(void)
{
    Bsp_UartBle_RxPlan_t p = Bsp_UartBle_RxPlan(10U, 30U, 128U, 100U);
    CHECK_EQ(p.take1, 20);
    CHECK_EQ(p.take2, 0);
    CHECK_EQ(p.total, 20);
    CHECK_EQ(p.next, 30);
}

static void test_wrap_around(void)
{
    Bsp_UartBle_RxPlan_t p = Bsp_UartBle_RxPlan(120U, 5U, 128U, 100U);
    CHECK_EQ(p.take1, 8);            /* 120..127 */
    CHECK_EQ(p.take2, 5);            /* 0..4 */
    CHECK_EQ(p.total, 13);
    CHECK_EQ(p.next, 5);
}

static void test_space_clamps_inside_first_segment(void)
{
    Bsp_UartBle_RxPlan_t p = Bsp_UartBle_RxPlan(10U, 90U, 128U, 20U);
    CHECK_EQ(p.take1, 20);
    CHECK_EQ(p.take2, 0);
    CHECK_EQ(p.total, 20);
    CHECK_EQ(p.next, 90);
}

static void test_space_zero_drops_all_but_keeps_position(void)
{
    Bsp_UartBle_RxPlan_t p = Bsp_UartBle_RxPlan(120U, 5U, 128U, 0U);
    CHECK_EQ(p.total, 0);
    CHECK_EQ(p.next, 5);             /* 位置仍前移：接受丢字节，不重复消费 */
}

int test_uartble_rx(void)
{
    printf("[uartble_rx]\n");
    test_no_delta();
    test_forward_span();
    test_wrap_around();
    test_space_clamps_inside_first_segment();
    test_space_zero_drops_all_but_keeps_position();
    return g_test_fail;
}
