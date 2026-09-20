#include "host_stub.h"
#include "Bsp.h"
#include "Proto_Ble.h"
#include <string.h>

/* ===== 可控状态 ===== */
uint32_t g_tick_ms = 0;

uint8_t g_tx[HOST_TX_MAX][17];
int     g_tx_count = 0;

HostMotorEv_t g_motor_ev[HOST_MOTOR_MAX];
int           g_motor_ev_count = 0;

uint8_t g_play_ids[HOST_PLAY_MAX];
int     g_play_count = 0;

uint16_t g_ir_raw[3] = {4095U, 4095U, 4095U};   /* 默认无反射 */
uint16_t g_batt_mv = 3900U;
uint8_t  g_batt_low = 0;

uint8_t g_tm_last[14];
int     g_tm_refresh_count = 0;
int     g_tm_clear_count = 0;

uint8_t g_led_state[4] = {0};

/* ===== 时钟 ===== */
void Host_Tick_Set(uint32_t ms) { g_tick_ms = ms; }
void Host_Tick_Advance(uint32_t ms) { g_tick_ms += ms; }

uint32_t Bsp_Tick_GetMs(void) { return g_tick_ms; }
void     Bsp_Tick_DelayMs(uint32_t ms) { g_tick_ms += ms; }

/* ===== BLE 发送 ===== */
void Bsp_UartBle_Send(const uint8_t *data, uint16_t len)
{
    if (len != PROTO_BLE_LEN) return;
    if (g_tx_count >= HOST_TX_MAX) return;
    memcpy(g_tx[g_tx_count], data, PROTO_BLE_LEN);
    g_tx_count++;
}

uint16_t Bsp_UartBle_TryRecv(uint8_t *out_buf, uint16_t max_len)
{
    (void)out_buf; (void)max_len;
    return 0;
}

uint8_t Bsp_UartBle_IsConnected(void) { return 1; }

/* ===== 电机 ===== */
static void Motor_Record(HostMotorOp_t op, uint8_t id, uint8_t dir, uint8_t speed)
{
    if (g_motor_ev_count >= HOST_MOTOR_MAX) return;
    g_motor_ev[g_motor_ev_count].op = op;
    g_motor_ev[g_motor_ev_count].id = id;
    g_motor_ev[g_motor_ev_count].dir = dir;
    g_motor_ev[g_motor_ev_count].speed = speed;
    g_motor_ev_count++;
}

void Bsp_Motor_Set(Bsp_Motor_Id_t id, Bsp_Motor_Dir_t dir, Bsp_Motor_Speed_t speed)
{
    Motor_Record(HOST_MOTOR_SET, (uint8_t)id, (uint8_t)dir, (uint8_t)speed);
}

void Bsp_Motor_StopAll(void)
{
    Motor_Record(HOST_MOTOR_STOPALL, 0xFFU, 0U, 0U);
}

void Bsp_Motor_Brake(Bsp_Motor_Id_t id)
{
    Motor_Record(HOST_MOTOR_BRAKE, (uint8_t)id, 0U, 0U);
}

void Bsp_Motor_BrakeAll(void)
{
    Motor_Record(HOST_MOTOR_BRAKE_ALL, 0xFFU, 0U, 0U);
}

void Bsp_Motor_Update(void) { }

/* ===== LED ===== */
void Bsp_Led_Init(void) { }
void Bsp_Led_On(Bsp_Led_Id_t id) { if (id < 4) g_led_state[id] = 1; }
void Bsp_Led_Off(Bsp_Led_Id_t id) { if (id < 4) g_led_state[id] = 0; }
void Bsp_Led_Toggle(Bsp_Led_Id_t id) { if (id < 4) g_led_state[id] ^= 1U; }
void Bsp_Led_AllOff(void) { memset(g_led_state, 0, sizeof(g_led_state)); }

/* ===== ASR ===== */
void Bsp_UartAsr_SendPlay(uint8_t voice_id)
{
    if (g_play_count >= HOST_PLAY_MAX) return;
    g_play_ids[g_play_count++] = voice_id;
}

/* ===== 红外 / 电池 ===== */
uint16_t Bsp_IR_ReadCh1(void) { return g_ir_raw[0]; }
uint16_t Bsp_IR_ReadCh2(void) { return g_ir_raw[1]; }
uint16_t Bsp_IR_ReadCh3(void) { return g_ir_raw[2]; }

uint16_t Bsp_Battery_GetVoltage(void) { return g_batt_mv; }
uint8_t  Bsp_Battery_IsLow(void) { return g_batt_low; }

/* ===== TM1640 ===== */
void Bsp_Tm1640_Refresh(const uint8_t data[TM1640_COLS])
{
    memcpy(g_tm_last, data, TM1640_COLS);
    g_tm_refresh_count++;
}

void Bsp_Tm1640_Clear(void)
{
    memset(g_tm_last, 0, sizeof(g_tm_last));
    g_tm_clear_count++;
}

void Bsp_Tm1640_SetBrightness(uint8_t level) { (void)level; }

/* ===== 测试辅助 ===== */
void Host_Tx_Reset(void) { g_tx_count = 0; }

int Host_Tx_Count(void) { return g_tx_count; }

int Host_Tx_FindType(uint8_t type, int nth)
{
    int seen = 0;
    for (int i = 0; i < g_tx_count; i++) {
        if (g_tx[i][4] != type) continue;
        if (seen++ == nth) return i;
    }
    return -1;
}

int Host_Tx_FindData(uint8_t type, uint8_t d0, uint8_t d1, int nth)
{
    int seen = 0;
    for (int i = 0; i < g_tx_count; i++) {
        if (g_tx[i][4] != type) continue;
        if (g_tx[i][5] != d0) continue;
        if (d1 != 0xFFU && g_tx[i][6] != d1) continue;   /* 0xFF = 通配 */
        if (seen++ == nth) return i;
    }
    return -1;
}

void Host_Motor_Reset(void) { g_motor_ev_count = 0; }

int Host_Motor_Count(HostMotorOp_t op, int id)
{
    int n = 0;
    for (int i = 0; i < g_motor_ev_count; i++) {
        if (g_motor_ev[i].op != op) continue;
        if (id >= 0 && g_motor_ev[i].id != (uint8_t)id) continue;
        n++;
    }
    return n;
}

void Host_Play_Reset(void) { g_play_count = 0; }

int Host_Play_CountOf(uint8_t id)
{
    int n = 0;
    for (int i = 0; i < g_play_count; i++) {
        if (g_play_ids[i] == id) n++;
    }
    return n;
}

void Host_Ir_SetAll(uint16_t raw1, uint16_t raw2, uint16_t raw3)
{
    g_ir_raw[0] = raw1; g_ir_raw[1] = raw2; g_ir_raw[2] = raw3;
}

void Host_Tm_Reset(void)
{
    memset(g_tm_last, 0, sizeof(g_tm_last));
    g_tm_refresh_count = 0;
    g_tm_clear_count = 0;
}

void Host_Led_Reset(void) { memset(g_led_state, 0, sizeof(g_led_state)); }

/* ===== 测试专用：App_Mode 依赖的进入钩子（真 App_Mode.c 编译时链接） ===== */
void App_Mode_Remote_Enter(void) { }
void App_Mode_Sensor_Enter(void) { }
