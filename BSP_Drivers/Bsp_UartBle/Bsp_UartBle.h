#ifndef __BSP_UART_BLE_H
#define __BSP_UART_BLE_H
#include "py32f0xx_hal.h"

/*
 * USART1 (PB6=TX / PB7=RX, AF0) <-> ECB00CV2 BLE 芯片
 *   波特率 9600 8N1（ECB00 默认，datasheet 第 10 页）
 *   PF3 = STA 引脚，下拉输入（datasheet 第 4 页要求）
 *
 * ECB00 工作模式：默认就是从机透传，无需 AT 配置主从。
 *   本驱动只负责原始字节收发 + PF3 连接电平检测，不解析遥控帧；
 *   遥控帧协议（帧格式/按键枚举）见 Protocol/Proto_Remote.h。
 */

#define BLE_RX_BUF_SIZE   128U

/** 初始化 USART1 9600 8N1 + DMA 收 + IDLE，PF3 下拉输入 */
void Bsp_UartBle_Init(void);

/**
 * @brief 发 AT 命令配置 BLE 名称（如 "LBS_XIAOBAI"）。
 *        阻塞发送 AT+NAME=<name>\r\n，不等回显。
 *        建议 BLE 上电稳定后调用。
 * @param name 名称字符串（ASCII，<=22 字节，不含 \0）
 * @param len  名称长度
 */
void Bsp_UartBle_ConfigName(const char *name, uint8_t len);

/** 发送若干字节（阻塞，超时 20ms） */
void Bsp_UartBle_Send(const uint8_t *data, uint16_t len);

/**
 * @brief 读一段收到的原始数据（尽可能多），复制到 out_buf。
 *        本函数不做协议解析，调用方自己处理。
 * @return 实际拷贝的字节数（0 表示无数据）。
 */
uint16_t Bsp_UartBle_TryRecv(uint8_t *out_buf, uint16_t max_len);

/** PF3 BLE 连接状态：1 = 已连接（高电平），0 = 未连接（低电平） */
uint8_t  Bsp_UartBle_IsConnected(void);

void Bsp_UartBle_UART_IRQHandler(void);
void Bsp_UartBle_DMA_IRQHandler(void);

#endif
