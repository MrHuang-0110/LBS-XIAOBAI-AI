#ifndef HOST_STUB_H
#define HOST_STUB_H
#include <stdint.h>

/* ===== 宿主测试替身：记录 BSP/驱动调用，提供可控时钟 ===== */

/* 时钟 */
extern uint32_t g_tick_ms;
void Host_Tick_Set(uint32_t ms);
void Host_Tick_Advance(uint32_t ms);

/* BLE 发送捕获（D2/D3 帧） */
#define HOST_TX_MAX 64
extern uint8_t g_tx[HOST_TX_MAX][17];
extern int     g_tx_count;
void    Host_Tx_Reset(void);
int     Host_Tx_Count(void);
/* 找第 n（从 0 起）个指定类型帧，返回下标；-1 = 无 */
int     Host_Tx_FindType(uint8_t type, int nth);
/* 找第 n 个 D2/D3 中满足 data[0]==a 且 data[1]==b 的帧下标；-1 = 无 */
int     Host_Tx_FindData(uint8_t type, uint8_t d0, uint8_t d1, int nth);

/* 电机事件 */
typedef enum {
    HOST_MOTOR_SET = 0,
    HOST_MOTOR_BRAKE = 1,
    HOST_MOTOR_BRAKE_ALL = 2,
    HOST_MOTOR_STOPALL = 3,
} HostMotorOp_t;

typedef struct {
    HostMotorOp_t op;
    uint8_t id;      /* 0=左 1=右 */
    uint8_t dir;     /* Bsp_Motor_Dir_t */
    uint8_t speed;   /* Bsp_Motor_Speed_t */
} HostMotorEv_t;

#define HOST_MOTOR_MAX 256
extern HostMotorEv_t g_motor_ev[HOST_MOTOR_MAX];
extern int           g_motor_ev_count;
void Host_Motor_Reset(void);
int  Host_Motor_Count(HostMotorOp_t op, int id /* -1=任意 */);

/* ASR 播报捕获 */
#define HOST_PLAY_MAX 64
extern uint8_t g_play_ids[HOST_PLAY_MAX];
extern int     g_play_count;
void    Host_Play_Reset(void);
int     Host_Play_CountOf(uint8_t id);

/* 红外原始值 / 电池 / TM1640 */
extern uint16_t g_ir_raw[3];
extern uint16_t g_batt_mv;
extern uint8_t  g_batt_low;
extern uint8_t  g_tm_last[14];
extern int      g_tm_refresh_count;
extern int      g_tm_clear_count;
extern uint8_t  g_led_state[4];
void Host_Ir_SetAll(uint16_t raw1, uint16_t raw2, uint16_t raw3);
void Host_Tm_Reset(void);
void Host_Led_Reset(void);

#endif
