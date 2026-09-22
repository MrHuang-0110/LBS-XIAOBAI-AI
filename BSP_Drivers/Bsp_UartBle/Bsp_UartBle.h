#ifndef __BSP_UART_BLE_H
#define __BSP_UART_BLE_H
#include "py32f0xx_hal.h"

/*
 * USART1 (PB6=TX / PB7=RX, AF0) <-> ECB02 BLE 芯片
 *   当前硬件配置为 9600 8N1。
 *   PF3 = STA 引脚，下拉输入（datasheet 第 4 页要求）
 *
 * ECB02 工作模式：从机透传；收发切换需要保护间隔。
 *   本驱动只负责原始字节收发 + PF3 连接电平检测，不解析遥控帧；
 *   遥控帧协议（帧格式/按键枚举）见 Protocol/Proto_Remote.h。
 */

#define BLE_RX_BUF_SIZE   128U

/** 初始化 USART1 9600 8N1 + DMA 循环收 + IDLE，PF3 下拉输入
 *  接收不依赖 IDLE：TryRecv 每次都会同步 DMA 增量，遥控器高速连发
 *  （ECB02 输出无空闲间隔的连续流）也不会丢帧。 */
void Bsp_UartBle_Init(void);

/**
 * @brief 发 AT 命令配置 BLE 名称（如 "LBS_XIAOBAI"）。
 *        阻塞发送 AT+NAME=<name>\r\n，不等回显。
 *        建议 BLE 上电稳定后调用。
 * @param name 名称字符串（ASCII，<=22 字节，不含 \0）
 * @param len  名称长度
 */
void Bsp_UartBle_ConfigName(const char *name, uint8_t len);

/** 将待发送数据放入非阻塞发送队列；队列由 Bsp_UartBle_Update() 按保护间隔发送。 */
void Bsp_UartBle_Send(const uint8_t *data, uint16_t len);

/** 主循环周期调用：等待 ECB02 RX→TX 保护间隔后发送一帧。 */
void Bsp_UartBle_Update(void);

/**
 * @brief 读一段收到的原始数据（尽可能多），复制到 out_buf。
 *        内部先同步 DMA 环形缓冲增量（关中断，不依赖 IDLE），再取走数据；
 *        本函数不做协议解析，调用方自己处理。
 * @return 实际拷贝的字节数（0 表示无数据）。
 */
uint16_t Bsp_UartBle_TryRecv(uint8_t *out_buf, uint16_t max_len);

/** PF3 BLE 连接状态：1 = 已连接（高电平），0 = 未连接（低电平） */
uint8_t  Bsp_UartBle_IsConnected(void);

void Bsp_UartBle_UART_IRQHandler(void);
void Bsp_UartBle_DMA_IRQHandler(void);

#endif
