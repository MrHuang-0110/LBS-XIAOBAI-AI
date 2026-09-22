#ifndef APP_PROGRAM_H
#define APP_PROGRAM_H
#include <stdint.h>
#include "Proto_Ble.h"

/* ===== 编程模式执行器 =====
 * 职责：
 *  - 进入/退出编程模式（进入幂等；四灯 200ms 跑马；播报 ID 52/53）
 *  - 处理 C2 动作指令（校验 → 抢占旧任务 → 执行）
 *  - 唯一的等待型任务状态机（定时电机 / 等待红外 / 等待词条）
 *  - 需要回报的任务完成后立即发 DONE，之后每 200ms 重发，直到下一条指令
 *  - 编程会话心跳（300ms 周期，1000ms 超时退出）；编程模式下任何结构+语义
 *    有效的 C2 指令也视为会话活跃，避免上位机为前台指令额外插入冗余心跳
 *  - QUERY_STATUS / READ_IR 响应、D3 红外变化事件
 *
 * 语义（权威契约 docs/BLE协议v2-编程模式.md）：编号不做幂等/去重，
 * 不产生 SEQ_CONFLICT；收到任意新动作指令都重新执行。 */

/** 开机初始化（清任务/回报/故障状态） */
void App_Program_Init(void);

/** 主循环周期调用：跑马灯、心跳超时、任务状态机、DONE 重报、红外事件 */
void App_Program_Update(void);

/** 消费一帧（仅 C2 生效；C1 由遥控模式消费） */
void App_Program_HandleFrame(const Proto_Ble_Frame_t *frame);

/** 编程模式下收到 ASR_01..ASR_10（cmd 42..51）时匹配等待词条任务 */
void App_Program_OnAsrCmd(uint8_t cmd);

/**
 * @brief 退出编程模式：取消任务、停回报、刹停、切到 mode、只播“退出编程模式”。
 * @param mode   目标模式（APP_MODE_*）
 * @param reason PROTO_ABORT_* 原因码（上报 D3 PROGRAM_ABORT）
 */
void App_Program_ExitTo(uint8_t mode, uint8_t reason);

/** 当前是否有等待型任务（供 QUERY_STATUS/测试观察） */
uint8_t App_Program_TaskCode(void);

/** 当前是否处于编程模式 */
uint8_t App_Program_IsActive(void);

/** 红外原始值 → 0..100（越近/反射越强数值越大） */
uint8_t App_Program_NormalizeIr(uint16_t raw);

/** 读指定通道归一化红外值：0=左 PA1 / 1=中 PA2 / 2=右 PA3 */
uint8_t App_Program_ReadIr(uint8_t channel);

#endif
