#include "Bsp_UartBle/Bsp_UartBle.h"
#include "Bsp_UartBle/Bsp_UartBle_Rx.h"
#include "Bsp_Tick/Bsp_Tick.h"
#include "py32f0xx_ll_system.h"   /* LL_SYSCFG_SetDMARemap_CH2 */
#include <string.h>

/*
 * USART1 (PB6=TX / PB7=RX, AF0) <-> BLE ECB02 模块
 *   9600 8N1，DMA1_Channel2 循环收 + UART IDLE 中断
 *
 * DMA 通道分配（与 Task 8 互补）：
 *   Task 8 USART2_RX -> DMA1_Channel1（HAL_SYSCFG_DMA_Req(0x08)，CH1 映射）
 *   Task 9 USART1_RX -> DMA1_Channel2（LL_SYSCFG_SetDMARemap_CH2，CH2 映射）
 *   request ID 0x06 = USART1_RX
 *
 * IDLE 处理：直接在 Bsp_UartBle_UART_IRQHandler 里检查/清 IDLE 标志，
 *   读 DMA 剩余计数得到 cur 位置，把 [last_pos, cur) 段累积到 g_out。
 *   不用 HAL_UART_IdleFrameDetectCpltCallback 弱回调——Bsp_UartAsr.c 已
 *   覆盖该回调（只处理 USART2），此处直接处理避免多重定义冲突。
 *
 * 接收路径（不定长）：
 *   DMA 循环写 g_rx[128]；IDLE 中断和主循环 TryRecv 都会把 DMA 增量段
 *   拷进 g_out[128] + g_out_len（关中断保护）。
 *   注意：不能只依赖 IDLE——遥控器高速连发时 ECB02 的 9600 输出是连续流，
 *   长时间无空闲间隔，只等 IDLE 会整段丢数据（表现为跑一下停一下）。
 *   Bsp_UartBle_TryRecv 取走 g_out 并清零 g_out_len（同一临界区内）。
 *   本驱动只做原始字节透传，不做协议解析。
 *
 * ECB02 收发保护：收到串口数据后至少留 30ms 再反向发送；连续发送帧之间
 * 至少留 10ms。发送先进入 4 帧队列，由主循环 Update 非阻塞调度，避免在
 * 收包回调中立即反向阻塞发送造成模块丢包。
 */

#define BLE_TX_QUEUE_SIZE       4U
#define BLE_TX_MAX_SIZE         17U
#define BLE_RX_TX_GUARD_MS      30U
#define BLE_TX_GAP_MS           10U
#define BLE_TX_TIMEOUT_MS       50U

static UART_HandleTypeDef huart;
static DMA_HandleTypeDef  hdma_rx;
static uint8_t g_rx[BLE_RX_BUF_SIZE];

/* 出队缓冲：IDLE 中断累积，TryRecv 按需取走。
 *   读指针 g_out_rd 支持“一次未取完不丢数据”：取空后复位，消费过半后压缩。 */
static uint8_t  g_out[BLE_RX_BUF_SIZE];
static volatile uint16_t g_out_len = 0;
static volatile uint16_t g_out_rd  = 0;

/* 上次处理到的 DMA 位置（IDLE 中断与主循环临界区内访问） */
static uint16_t g_last_pos = 0;

/* ECB02 发送队列：协议帧固定 17 字节；只在主循环访问。 */
typedef struct {
    uint8_t data[BLE_TX_MAX_SIZE];
    uint8_t len;
} Ble_Tx_Item_t;

static Ble_Tx_Item_t g_tx_queue[BLE_TX_QUEUE_SIZE];
static uint8_t g_tx_read = 0;
static uint8_t g_tx_write = 0;
static uint8_t g_tx_count = 0;
static volatile uint32_t g_last_rx_ms = 0;
static uint32_t g_last_tx_ms = 0;

static void Ble_GpioClkInit(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_DMA_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    /* PB6=TX1, PB7=RX1, AF0（datasheet V2.5 §3.2） */
    GPIO_InitTypeDef gi = {0};
    gi.Mode      = GPIO_MODE_AF_PP;
    gi.Pull      = GPIO_PULLUP;
    gi.Speed     = GPIO_SPEED_FREQ_HIGH;
    gi.Alternate = GPIO_AF0_USART1;
    gi.Pin       = GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOB, &gi);

    /* PF3 BLE_STA 输入，下拉（datasheet 第 4 页要求单片机配下拉输入） */
    gi.Mode = GPIO_MODE_INPUT;
    gi.Pull = GPIO_PULLDOWN;
    gi.Pin  = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOF, &gi);
}

void Bsp_UartBle_Init(void)
{
    Ble_GpioClkInit();

    huart.Instance          = USART1;
    huart.Init.BaudRate     = 9600;          /* 与当前 ECB02 模块配置一致 */
    huart.Init.WordLength   = UART_WORDLENGTH_8B;
    huart.Init.StopBits     = UART_STOPBITS_1;
    huart.Init.Parity       = UART_PARITY_NONE;
    huart.Init.Mode         = UART_MODE_TX_RX;
    huart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart);

    hdma_rx.Instance                 = DMA1_Channel2;
    hdma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_rx.Init.Mode                = DMA_CIRCULAR;
    hdma_rx.Init.Priority            = DMA_PRIORITY_MEDIUM;
    HAL_DMA_Init(&hdma_rx);

    // pi-lens-ignore: no-reserved-identifiers -- STM32 HAL 内建宏，不可重命名
    __HAL_LINKDMA(&huart, hdmarx, hdma_rx);

    /* SYSCFG DMA remap：USART1_RX(request 0x06) -> DMA1_Channel2
       LL_SYSCFG_SetDMARemap_CH2 用 MODIFY_REG 清 DMA2_MAP 域再写入，
       比 HAL_SYSCFG_DMA_Req(SET_BIT) 更安全（HAL 版只 OR 不清）。 */
    LL_SYSCFG_SetDMARemap_CH2(LL_SYSCFG_DMA_MAP_USART1_RX);

    HAL_NVIC_SetPriority(USART1_IRQn, 1, 1);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel2_3_IRQn, 1, 1);
    HAL_NVIC_EnableIRQ(DMA1_Channel2_3_IRQn);

    g_out_len = 0;
    g_out_rd = 0;
    g_last_pos = 0;
    g_tx_read = 0;
    g_tx_write = 0;
    g_tx_count = 0;
    g_last_rx_ms = Bsp_Tick_GetMs();
    g_last_tx_ms = g_last_rx_ms;

    HAL_UART_Receive_DMA(&huart, g_rx, BLE_RX_BUF_SIZE);
    __HAL_UART_ENABLE_IT(&huart, UART_IT_IDLE);
}

void Bsp_UartBle_Send(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0U || len > BLE_TX_MAX_SIZE) return;
    if (g_tx_count >= BLE_TX_QUEUE_SIZE) return;

    memcpy(g_tx_queue[g_tx_write].data, data, len);
    g_tx_queue[g_tx_write].len = (uint8_t)len;
    g_tx_write = (uint8_t)((g_tx_write + 1U) % BLE_TX_QUEUE_SIZE);
    g_tx_count++;
}

void Bsp_UartBle_Update(void)
{
    if (g_tx_count == 0U) return;

    uint32_t now = Bsp_Tick_GetMs();
    if ((now - g_last_rx_ms) < BLE_RX_TX_GUARD_MS) return;
    if ((now - g_last_tx_ms) < BLE_TX_GAP_MS) return;

    Ble_Tx_Item_t *item = &g_tx_queue[g_tx_read];
    if (HAL_UART_Transmit(&huart, item->data, item->len, BLE_TX_TIMEOUT_MS) != HAL_OK) return;

    g_tx_read = (uint8_t)((g_tx_read + 1U) % BLE_TX_QUEUE_SIZE);
    g_tx_count--;
    g_last_tx_ms = Bsp_Tick_GetMs();
}

void Bsp_UartBle_ConfigName(const char *name, uint8_t len)
{
    /* 发送 AT+NAME=<name>\r\n
       ECB02 收到 AT 开头的串就当 AT 命令处理。
       蓝牙名最长 22 字节，这里不做越界检查，调用方负责。 */
    if (len > 22U) len = 22U;
    uint8_t tx[32];
    uint16_t n = 0;
    tx[n++] = 'A'; tx[n++] = 'T'; tx[n++] = '+';
    tx[n++] = 'N'; tx[n++] = 'A'; tx[n++] = 'M'; tx[n++] = 'E'; tx[n++] = '=';
    for (uint8_t i = 0; i < len; i++) tx[n++] = (uint8_t)name[i];
    tx[n++] = '\r'; tx[n++] = '\n';
    HAL_UART_Transmit(&huart, tx, n, 50);
}

/* 把 DMA 环形缓冲自 g_last_pos 以来的新增字节追加到 g_out。
 * IDLE 中断与主循环 TryRecv 都会调用；调用方保证互斥：
 * 主循环在关中断临界区里调用，中断上下文天然互斥。 */
static void Ble_RxAppendIncrement(void)
{
    uint16_t cur = (uint16_t)(BLE_RX_BUF_SIZE - __HAL_DMA_GET_COUNTER(&hdma_rx));
    if (cur == g_last_pos) return;

    uint16_t space = (uint16_t)(BLE_RX_BUF_SIZE - g_out_len);
    Bsp_UartBle_RxPlan_t plan = Bsp_UartBle_RxPlan(g_last_pos, cur, BLE_RX_BUF_SIZE, space);
    if (plan.take1) {
        memcpy(&g_out[g_out_len], &g_rx[g_last_pos], plan.take1);
        g_out_len = (uint16_t)(g_out_len + plan.take1);
    }
    if (plan.take2) {
        memcpy(&g_out[g_out_len], &g_rx[0], plan.take2);
        g_out_len = (uint16_t)(g_out_len + plan.take2);
    }
    g_last_pos = plan.next;
    g_last_rx_ms = Bsp_Tick_GetMs();
}

uint16_t Bsp_UartBle_TryRecv(uint8_t *out_buf, uint16_t max_len)
{
    uint16_t copied = 0;
    // pi-lens-ignore: no-reserved-identifiers -- CMSIS 内核内建，不可重命名
    __disable_irq();
    /* 同步 DMA 增量：不依赖 IDLE，连续数据流也能持续取出 */
    Ble_RxAppendIncrement();
    uint16_t avail = (uint16_t)(g_out_len - g_out_rd);
    copied = (avail > max_len) ? max_len : avail;
    if (copied) {
        memcpy(out_buf, &g_out[g_out_rd], copied);
        g_out_rd = (uint16_t)(g_out_rd + copied);
    }
    if (g_out_rd >= g_out_len) {
        g_out_len = 0;          /* 取空：复位，IRQ 可继续追加 */
        g_out_rd  = 0;
    } else if (g_out_rd >= 64U) {
        /* 半消费：压缩剩余数据，保证 IRQ 侧始终有空间不丢帧 */
        uint16_t remain = (uint16_t)(g_out_len - g_out_rd);
        memmove(g_out, &g_out[g_out_rd], remain);
        g_out_len = remain;
        g_out_rd  = 0;
    }
    // pi-lens-ignore: no-reserved-identifiers -- CMSIS 内核内建，不可重命名
    __enable_irq();
    return copied;
}

uint8_t Bsp_UartBle_IsConnected(void)
{
    /* 假设 PF3 = 高 表示已连接。若极性相反，改成 == RESET。 */
    return HAL_GPIO_ReadPin(GPIOF, GPIO_PIN_3) == GPIO_PIN_SET ? 1 : 0;
}

/* IDLE 中断把 [last_pos..cur) 追加到 g_out。
   直接在 UART IRQ 里处理 IDLE（先清标志再调 HAL_UART_IRQHandler，
   这样 HAL 不会重复进 IDLE 分支、也不会调已被 Bsp_UartAsr 覆盖的弱回调）。 */
void Bsp_UartBle_UART_IRQHandler(void)
{
    if (__HAL_UART_GET_FLAG(&huart, UART_FLAG_IDLE)) {
        __HAL_UART_CLEAR_IDLEFLAG(&huart);
        Ble_RxAppendIncrement();
    }
    HAL_UART_IRQHandler(&huart);
}

void Bsp_UartBle_DMA_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_rx); }
