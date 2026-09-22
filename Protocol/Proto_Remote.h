#ifndef PROTO_REMOTE_H
#define PROTO_REMOTE_H
#include <stdint.h>
#include "Proto_Ble.h"

/* ===== 遥控器 C1 键位定义（协议见 docs/BLE协议v2-编程模式.md §3）=====
 * 帧解析/校验已统一到 Protocol/Proto_Ble.c（多类型 17 字节解析器），
 * 本头文件只保留键位枚举与帧常量，供遥控模式消费 C1 帧。 */

#define REMOTE_FRAME_HEAD   PROTO_BLE_HEAD
#define REMOTE_FRAME_TAIL   PROTO_BLE_TAIL
#define REMOTE_FRAME_LEN    PROTO_BLE_LEN
#define REMOTE_KEY_COUNT    PROTO_BLE_DATA_LEN

/* 按键枚举（与遥控协议.md 的 enum 顺序一致，对应 DATA[0..9]） */
typedef enum {
    REMOTE_KEY_UP    = 0,   /* KeyUp    方向上 */
    REMOTE_KEY_DOWN  = 1,   /* KeyDown  方向下 */
    REMOTE_KEY_LEFT  = 2,   /* KeyLeft  方向左 */
    REMOTE_KEY_RIGHT = 3,   /* KeyRight 方向右 */
    REMOTE_KEY_Y     = 4,
    REMOTE_KEY_A     = 5,
    REMOTE_KEY_X     = 6,
    REMOTE_KEY_B     = 7,
    REMOTE_KEY_R1    = 8,   /* R1Key（减速） */
    REMOTE_KEY_L1    = 9,   /* L1Key（加速） */
} Bsp_RemoteKey_t;

#endif
