#ifndef PROTO_BLE_H
#define PROTO_BLE_H
#include <stdint.h>

/* ===== BLE 协议 v2（编程模式）=====
 * 权威契约：docs/BLE协议v2-编程模式.md（黄金帧 docs/ble_v2_golden_frames.json）。
 * 帧外壳与原遥控协议完全一致（17 字节，5A SRC DST 0A TYPE DATA[10] CRC A5），
 * 在其上新增 C2 编程请求 / D2 响应 / D3 异步事件；C1 遥控帧原样兼容。
 *
 * 方向：App→设备 SRC=0x97 DST=0x98；设备→App 反向。
 * 校验：字节 0..14 累加和低 8 位。多字节整数 little-endian。
 *
 * 语义要点（勿回退到旧设计）：编号只用于关联指令与 DONE，不做幂等/去重，
 * 不返回 SEQ_CONFLICT；收到任意新动作指令都必须重新执行。 */

/* --- 帧外壳 --- */
#define PROTO_BLE_HEAD        0x5AU
#define PROTO_BLE_TAIL        0xA5U
#define PROTO_BLE_LEN         17U
#define PROTO_BLE_DATA_LEN    10U
#define PROTO_BLE_ADDR_APP    0x97U   /* App 源地址 / 设备回报目标地址 */
#define PROTO_BLE_ADDR_DEV    0x98U   /* 设备源地址 / App 请求目标地址 */

/* --- 帧类型 --- */
#define PROTO_BLE_TYPE_C1     0xC1U   /* 遥控 10 键位图 */
#define PROTO_BLE_TYPE_C2     0xC2U   /* 编程/会话请求 */
#define PROTO_BLE_TYPE_D2     0xD2U   /* 响应：查询/完成/错误 */
#define PROTO_BLE_TYPE_D3     0xD3U   /* 异步事件 */

/* --- C2 操作码 --- */
#define PROTO_OP_ENTER_PROGRAM  0x01U
#define PROTO_OP_ENTER_REMOTE   0x02U
#define PROTO_OP_HEARTBEAT      0x03U
#define PROTO_OP_QUERY_STATUS   0x04U
#define PROTO_OP_STOP_PROGRAM   0x05U
#define PROTO_OP_MOTOR_TIME     0x10U   /* args=[motor, dir, ms0..ms3] */
#define PROTO_OP_MOTOR_RUN      0x11U   /* args=[motor, dir] */
#define PROTO_OP_MOTOR_STOP     0x12U   /* args=[motor] */
#define PROTO_OP_MOTOR_POWER    0x13U   /* args=[level 1..3] */
#define PROTO_OP_MOVE_TIME      0x20U   /* args=[move, ms0..ms3] */
#define PROTO_OP_MOVE_RUN       0x21U   /* args=[move] */
#define PROTO_OP_MOVE_STOP      0x22U
#define PROTO_OP_MOVE_POWER     0x23U   /* args=[level 1..3] */
#define PROTO_OP_WAIT_IR        0x30U   /* args=[channel, cmp, threshold] */
#define PROTO_OP_READ_IR        0x31U
#define PROTO_OP_SHOW_EYE       0x40U   /* args=[eye 1..10] */
#define PROTO_OP_SHOW_NUM       0x41U   /* args=[num 0..100] */
#define PROTO_OP_SHOW_OFF       0x42U
#define PROTO_OP_PLAY_VOICE     0x50U   /* args=[item 1..10 = P01..P10] */
#define PROTO_OP_WAIT_VOICE     0x51U   /* args=[item 1..10 = ASR_01..ASR_10] */

/* --- 参数取值 --- */
#define PROTO_MOTOR_LEFT        0U
#define PROTO_MOTOR_RIGHT       1U
#define PROTO_DIR_FORWARD       1U
#define PROTO_DIR_BACKWARD      2U
#define PROTO_MOVE_FORWARD      1U
#define PROTO_MOVE_LEFT         2U
#define PROTO_MOVE_RIGHT        3U
#define PROTO_MOVE_BACKWARD     4U
#define PROTO_IR_LEFT           0U
#define PROTO_IR_CENTER         1U
#define PROTO_IR_RIGHT          2U
#define PROTO_IR_CMP_GT         0U      /* 严格大于阈值 */
#define PROTO_IR_CMP_LT         1U      /* 严格小于阈值 */
#define PROTO_POWER_LOW         1U

/* --- D2 result --- */
#define PROTO_RESULT_OK         0x00U
#define PROTO_RESULT_BAD_PARAM  0x01U
#define PROTO_RESULT_BAD_OPCODE 0x02U
#define PROTO_RESULT_BAD_MODE   0x03U

/* --- D3 event --- */
#define PROTO_EVT_MODE_CHANGE   0x01U
#define PROTO_EVT_PROGRAM_ABORT 0x02U
#define PROTO_EVT_IR_CHANGE     0x03U
#define PROTO_EVT_WAKE          0x04U
#define PROTO_EVT_SLEEP         0x05U
#define PROTO_EVT_LOW_BATTERY   0x06U
#define PROTO_EVT_PROTOCOL_ERR  0x07U

/* D3 PROGRAM_ABORT data0 原因 */
#define PROTO_ABORT_HEARTBEAT   1U
#define PROTO_ABORT_BLE_LOST    2U
#define PROTO_ABORT_KEY         3U
#define PROTO_ABORT_APP_MODE    4U

/* D3 PROTOCOL_ERR data0 错误码 */
#define PROTO_PERR_OVERFLOW     1U
#define PROTO_PERR_CHECKSUM     2U
#define PROTO_PERR_OPCODE       3U
#define PROTO_PERR_PARAM        4U

/* 查询状态 data6：高 4 位任务，低 4 位故障 */
#define PROTO_TASK_NONE         0U
#define PROTO_TASK_MOTOR_TIME   1U
#define PROTO_TASK_WAIT_IR      2U
#define PROTO_TASK_WAIT_VOICE   3U
#define PROTO_FAULT_HEARTBEAT   0x01U
#define PROTO_FAULT_BAD_OPCODE  0x02U
#define PROTO_FAULT_BAD_PARAM   0x04U

/* 等待红外执行参数（供执行器/测试引用，避免魔法数散落） */
#define PROTO_PROG_IR_SAMPLE_MS   20U
#define PROTO_PROG_IR_STABLE_CNT  3U

typedef struct {
    uint8_t type;                  /* PROTO_BLE_TYPE_* */
    uint8_t data[PROTO_BLE_DATA_LEN];
} Proto_Ble_Frame_t;

/** 清空接收流缓冲（开机调用一次） */
void Proto_Ble_Init(void);

/** 喂入新收字节（来自 Bsp_UartBle_TryRecv），内部滑窗分帧 */
void Proto_Ble_Feed(const uint8_t *buf, uint16_t len);

/**
 * @brief 取出一帧结构合法（头/地址/长度/校验/尾）的帧。
 * @return 1=取到；0=流里没有完整合法帧
 */
uint8_t Proto_Ble_GetFrame(Proto_Ble_Frame_t *out);

/** 组装并发送一帧（自动填地址/长度/校验/帧尾） */
void Proto_Ble_SendRaw(uint8_t type, const uint8_t data[PROTO_BLE_DATA_LEN]);

/** 发送 D2：[seq, opcode, result, data0..data6] */
void Proto_Ble_SendResponse(uint8_t seq, uint8_t opcode, uint8_t result,
                            const uint8_t data[7]);

/** 发送 D3：[event, counter(自增), data0..data7] */
void Proto_Ble_SendEvent(uint8_t event, const uint8_t data[8]);

/** little-endian 读取 */
uint32_t Proto_Ble_GetU32(const uint8_t *p);

/** 从 C1 帧取 10 键位图（copy，不做解释） */
void Proto_Ble_C1Keys(const Proto_Ble_Frame_t *frame, uint8_t keys[PROTO_BLE_DATA_LEN]);

#endif
