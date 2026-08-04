#ifndef __BSP_UART_ASR_H
#define __BSP_UART_ASR_H
#include "py32f0xx_hal.h"

/* 语音芯片交互协议 v0.7（ASCII 文本；两方向均带帧尾：MCU 发帧尾 `\n`，收帧尾 `\r\n`；
   tag 用 `=` 分隔十进制数值）。
   ★ 协议 ID 宏（ASR_VOICE_* / ASR_CMD_*）与 cmd_to_voice 映射已上移到 Protocol/Proto_Asr.h，
   本驱动只负责 ASCII 帧解析 / DMA 收发 / 事件队列，不持有协议 ID。 */

typedef enum {
    ASR_EVT_NONE = 0,
    ASR_EVT_CMD  = 1,   /* ASRPRO 识别到语音命令，arg = 命令 ID (ASR_CMD_*) */
    ASR_EVT_WAKE = 2,   /* ASRPRO 检测到唤醒词，arg 无意义 */
    ASR_EVT_DONE = 3,   /* ASRPRO 播报完成，arg = 上次的语音 ID */
} Bsp_UartAsr_EvtType_t;

typedef struct {
    Bsp_UartAsr_EvtType_t type;
    uint8_t               arg;
} Bsp_UartAsr_Event_t;

/** 初始化 USART2 (PF0/PF1, 9600 8N1) + DMA 收 + IDLE 中断 */
void Bsp_UartAsr_Init(void);

/** 发送 "play=NN\n" 请求播报指定语音 ID（十进制） */
void Bsp_UartAsr_SendPlay(uint8_t voice_id);

/** 发送 "stop\n" */
void Bsp_UartAsr_SendStop(void);

/** 发送 "ping\n" */
void Bsp_UartAsr_SendPing(void);

/**
 * @brief 发送原始字节（调试用，绕过协议格式化）。
 *        直接 HAL_UART_Transmit，不加任何 tag/分隔符/帧尾。
 *        供调试时把其它来源的数据透传到 ASRPRO 串口观察。
 */
void Bsp_UartAsr_SendRaw(const uint8_t *data, uint16_t len);

/**
 * @brief 主循环轮询：取出一个已解析事件。
 *        无事件返回 0；有事件返回 1 并填充 out。
 */
uint8_t Bsp_UartAsr_TryRecv(Bsp_UartAsr_Event_t *out);

/* 中断入口（由 py32f0xx_it.c 转发） */
void Bsp_UartAsr_UART_IRQHandler(void);
void Bsp_UartAsr_DMA_IRQHandler(void);

#endif
