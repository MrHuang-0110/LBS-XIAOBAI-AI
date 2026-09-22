#include "App_Program.h"
#include "App_Mode.h"
#include "App_Display.h"
#include "App_Vehicle.h"
#include "Bsp.h"
#include "Proto_Asr.h"

/* --- 时序常量 --- */
#define PROG_DONE_REPEAT_MS   200U    /* DONE 重报周期 */
#define PROG_HB_TIMEOUT_MS    1000U   /* 心跳超时（上位机 300ms 一发） */
#define PROG_MARQUEE_MS       200U    /* 四灯跑马步进 */
#define PROG_IR_POLL_MS       100U    /* D3 红外事件采样周期 */
#define PROG_IR_EVT_TH        50U     /* 触发掩码阈值（≥ 视为触发） */

/* --- 任务类型（与 QUERY_STATUS data6 高 4 位一致） --- */
typedef enum {
    PROG_TASK_NONE = PROTO_TASK_NONE,
    PROG_TASK_MOTOR_TIME = PROTO_TASK_MOTOR_TIME,
    PROG_TASK_WAIT_IR = PROTO_TASK_WAIT_IR,
    PROG_TASK_WAIT_VOICE = PROTO_TASK_WAIT_VOICE,
} Prog_Task_t;

static struct {
    Prog_Task_t kind;
    uint8_t  seq;
    uint8_t  opcode;
    uint32_t start_ms;        /* 定时任务：启动时刻 */
    uint32_t duration_ms;     /* 定时任务：时长 */
    uint8_t  motor_mask;      /* bit0 左 / bit1 右：任务结束后要刹停的电机 */
    uint8_t  ir_ch;           /* 0 左 / 1 中 / 2 右 */
    uint8_t  ir_cmp;          /* PROTO_IR_CMP_* */
    uint8_t  ir_th;           /* 0..100 */
    uint8_t  ir_cnt;          /* 连续满足次数（防抖） */
    uint32_t ir_next_ms;      /* 下次采样时刻 */
    uint8_t  voice_item;      /* 1..10 = ASR_01..ASR_10 */
} s_task;

/* 单电机功率与组合功率分别维护，默认 3 档（100%） */
static uint8_t s_motor_power = 3;
static uint8_t s_move_power  = 3;

/* 编程会话心跳 */
static uint32_t s_last_hb = 0;
static uint8_t  s_hb_seen = 0;
static uint8_t  s_fault = 0;      /* PROTO_FAULT_* 位或 */

/* 四灯跑马 */
static uint32_t s_marquee_ms = 0;
static uint8_t  s_marquee_idx = 0;

/* DONE 重报：完成后立即发一次，之后每 200ms 重发，直到新指令/模式切换 */
static uint8_t  s_done_active = 0;
static uint8_t  s_done_seq = 0;
static uint8_t  s_done_op = 0;
static uint32_t s_done_next = 0;

/* 红外变化事件 */
static uint8_t  s_ir_last_mask = 0xFF;   /* 0xFF = 未初始化，首轮不误报 */
static uint32_t s_ir_last_ms = 0;

/* ---------- 小工具 ---------- */

static uint32_t Prog_Now(void) { return Bsp_Tick_GetMs(); }

/* 无符号 tick 差值：a 是否已到达/超过 b（兼容 SysTick 回绕） */
static uint8_t Prog_Due(uint32_t now, uint32_t target)
{
    return (uint8_t)((now - target) < 0x80000000U);
}

uint8_t App_Program_NormalizeIr(uint16_t raw)
{
    if (raw > 4095U) raw = 4095U;
    /* 反射越强（raw 越小）数值越大：value = (4095-raw)*100/4095 */
    return (uint8_t)(((4095U - (uint32_t)raw) * 100U) / 4095U);
}

uint8_t App_Program_ReadIr(uint8_t channel)
{
    switch (channel) {
    case PROTO_IR_LEFT:   return App_Program_NormalizeIr(Bsp_IR_ReadCh1());
    case PROTO_IR_CENTER: return App_Program_NormalizeIr(Bsp_IR_ReadCh2());
    case PROTO_IR_RIGHT:  return App_Program_NormalizeIr(Bsp_IR_ReadCh3());
    default:              return 0;
    }
}

static Bsp_Motor_Id_t Prog_MotorId(uint8_t motor)
{
    return (motor == PROTO_MOTOR_RIGHT) ? MOTOR_RIGHT : MOTOR_LEFT;
}

static Bsp_Motor_Dir_t Prog_MotorDir(uint8_t dir)
{
    return (dir == PROTO_DIR_BACKWARD) ? MOTOR_DIR_BACKWARD : MOTOR_DIR_FORWARD;
}

static Bsp_Motor_Speed_t Prog_PowerLevel(uint8_t level)
{
    if (level < 1U) level = 1U;
    if (level > 3U) level = 3U;
    return (Bsp_Motor_Speed_t)level;
}

static void Prog_ApplyMove(uint8_t move, Bsp_Motor_Speed_t speed)
{
    switch (move) {
    case PROTO_MOVE_FORWARD:  Vehicle_Drive(VEHICLE_DIR_FORWARD,  speed); break;
    case PROTO_MOVE_LEFT:     Vehicle_Drive(VEHICLE_DIR_LEFT,     speed); break;
    case PROTO_MOVE_RIGHT:    Vehicle_Drive(VEHICLE_DIR_RIGHT,    speed); break;
    case PROTO_MOVE_BACKWARD: Vehicle_Drive(VEHICLE_DIR_BACKWARD, speed); break;
    default: break;
    }
}

static void Prog_BrakeMask(uint8_t mask)
{
    if (mask & 0x01U) Bsp_Motor_Brake(MOTOR_LEFT);
    if (mask & 0x02U) Bsp_Motor_Brake(MOTOR_RIGHT);
}

/* ---------- 任务与回报 ---------- */

static void Prog_CancelTask(void)
{
    if (s_task.kind == PROG_TASK_MOTOR_TIME) {
        Prog_BrakeMask(s_task.motor_mask);   /* 旧任务在驱动电机：先刹停其控制的电机 */
    }
    s_task.kind = PROG_TASK_NONE;
}

static void Prog_StopDone(void)
{
    s_done_active = 0;
}

static void Prog_SendDone(uint8_t seq, uint8_t opcode)
{
    uint8_t data[7] = {0};
    Proto_Ble_SendResponse(seq, opcode, PROTO_RESULT_OK, data);
    s_done_seq = seq;
    s_done_op = opcode;
    s_done_active = 1;
    s_done_next = Prog_Now() + PROG_DONE_REPEAT_MS;
}

static void Prog_CompleteTask(void)
{
    uint8_t seq = s_task.seq;
    uint8_t op = s_task.opcode;

    if (s_task.kind == PROG_TASK_MOTOR_TIME) {
        Prog_BrakeMask(s_task.motor_mask);   /* 定时结束只刹任务控制的电机 */
    }
    s_task.kind = PROG_TASK_NONE;
    Prog_SendDone(seq, op);
}

/* ---------- 参数校验 ---------- */

static uint8_t Prog_Validate(uint8_t opcode, const uint8_t *a)
{
    switch (opcode) {
    case PROTO_OP_MOTOR_TIME:
        if (a[0] > PROTO_MOTOR_RIGHT) return PROTO_RESULT_BAD_PARAM;
        if (a[1] != PROTO_DIR_FORWARD && a[1] != PROTO_DIR_BACKWARD) return PROTO_RESULT_BAD_PARAM;
        if (Proto_Ble_GetU32(&a[2]) == 0U) return PROTO_RESULT_BAD_PARAM;
        return PROTO_RESULT_OK;
    case PROTO_OP_MOTOR_RUN:
        if (a[0] > PROTO_MOTOR_RIGHT) return PROTO_RESULT_BAD_PARAM;
        if (a[1] != PROTO_DIR_FORWARD && a[1] != PROTO_DIR_BACKWARD) return PROTO_RESULT_BAD_PARAM;
        return PROTO_RESULT_OK;
    case PROTO_OP_MOTOR_STOP:
        if (a[0] > PROTO_MOTOR_RIGHT) return PROTO_RESULT_BAD_PARAM;
        return PROTO_RESULT_OK;
    case PROTO_OP_MOTOR_POWER:
    case PROTO_OP_MOVE_POWER:
        if (a[0] < 1U || a[0] > 3U) return PROTO_RESULT_BAD_PARAM;
        return PROTO_RESULT_OK;
    case PROTO_OP_MOVE_TIME:
        if (a[0] < PROTO_MOVE_FORWARD || a[0] > PROTO_MOVE_BACKWARD) return PROTO_RESULT_BAD_PARAM;
        if (Proto_Ble_GetU32(&a[1]) == 0U) return PROTO_RESULT_BAD_PARAM;
        return PROTO_RESULT_OK;
    case PROTO_OP_MOVE_RUN:
        if (a[0] < PROTO_MOVE_FORWARD || a[0] > PROTO_MOVE_BACKWARD) return PROTO_RESULT_BAD_PARAM;
        return PROTO_RESULT_OK;
    case PROTO_OP_MOVE_STOP:
        return PROTO_RESULT_OK;
    case PROTO_OP_WAIT_IR:
        if (a[0] > PROTO_IR_RIGHT) return PROTO_RESULT_BAD_PARAM;
        if (a[1] != PROTO_IR_CMP_GT && a[1] != PROTO_IR_CMP_LT) return PROTO_RESULT_BAD_PARAM;
        if (a[2] > 100U) return PROTO_RESULT_BAD_PARAM;
        return PROTO_RESULT_OK;
    case PROTO_OP_SHOW_EYE:
        if (a[0] < 1U || a[0] > 10U) return PROTO_RESULT_BAD_PARAM;
        return PROTO_RESULT_OK;
    case PROTO_OP_SHOW_NUM:
        if (a[0] > 100U) return PROTO_RESULT_BAD_PARAM;
        return PROTO_RESULT_OK;
    case PROTO_OP_SHOW_OFF:
        return PROTO_RESULT_OK;
    case PROTO_OP_PLAY_VOICE:
    case PROTO_OP_WAIT_VOICE:
        if (a[0] < 1U || a[0] > 10U) return PROTO_RESULT_BAD_PARAM;
        return PROTO_RESULT_OK;
    default:
        return PROTO_RESULT_BAD_OPCODE;
    }
}

/* ---------- 执行 ---------- */

/* 编程模式下收到结构+语义有效的 C2 视为会话活跃：新上位机会用前台指令夹带
 * 心跳（省掉冗余心跳写），旧上位机的显式 HEARTBEAT 行为保持不变。
 * 坏帧/非 C2/语义非法不能续期，避免损坏流量掩盖真实链损。 */
static void Prog_KeepAlive(uint32_t now)
{
    s_last_hb = now;
    s_hb_seen = 1;
    s_fault &= (uint8_t)~PROTO_FAULT_HEARTBEAT;
}

static void Prog_SendError(uint8_t seq, uint8_t opcode, uint8_t result)
{
    uint8_t data[7] = {0};
    if (result == PROTO_RESULT_BAD_OPCODE) s_fault |= PROTO_FAULT_BAD_OPCODE;
    else                                  s_fault |= PROTO_FAULT_BAD_PARAM;
    Proto_Ble_SendResponse(seq, opcode, result, data);
}

static void Prog_Execute(uint8_t seq, uint8_t opcode, const uint8_t *a)
{
    uint32_t now = Prog_Now();

    switch (opcode) {
    case PROTO_OP_MOTOR_TIME:
        Bsp_Motor_Set(Prog_MotorId(a[0]), Prog_MotorDir(a[1]), Prog_PowerLevel(s_motor_power));
        s_task.kind = PROG_TASK_MOTOR_TIME;
        s_task.seq = seq;
        s_task.opcode = opcode;
        s_task.start_ms = now;
        s_task.duration_ms = Proto_Ble_GetU32(&a[2]);
        s_task.motor_mask = (a[0] == PROTO_MOTOR_LEFT) ? 0x01U : 0x02U;
        break;

    case PROTO_OP_MOTOR_RUN:
        Bsp_Motor_Set(Prog_MotorId(a[0]), Prog_MotorDir(a[1]), Prog_PowerLevel(s_motor_power));
        break;

    case PROTO_OP_MOTOR_STOP:
        Bsp_Motor_Brake(Prog_MotorId(a[0]));
        break;

    case PROTO_OP_MOTOR_POWER:
        s_motor_power = a[0];
        break;

    case PROTO_OP_MOVE_TIME:
        Prog_ApplyMove(a[0], Prog_PowerLevel(s_move_power));
        s_task.kind = PROG_TASK_MOTOR_TIME;
        s_task.seq = seq;
        s_task.opcode = opcode;
        s_task.start_ms = now;
        s_task.duration_ms = Proto_Ble_GetU32(&a[1]);
        s_task.motor_mask = 0x03U;
        break;

    case PROTO_OP_MOVE_RUN:
        Prog_ApplyMove(a[0], Prog_PowerLevel(s_move_power));
        break;

    case PROTO_OP_MOVE_STOP:
        Bsp_Motor_BrakeAll();
        break;

    case PROTO_OP_MOVE_POWER:
        s_move_power = a[0];
        break;

    case PROTO_OP_WAIT_IR:
        s_task.kind = PROG_TASK_WAIT_IR;
        s_task.seq = seq;
        s_task.opcode = opcode;
        s_task.ir_ch = a[0];
        s_task.ir_cmp = a[1];
        s_task.ir_th = a[2];
        s_task.ir_cnt = 0;
        s_task.ir_next_ms = now;
        break;

    case PROTO_OP_SHOW_EYE:
        App_Display_ShowEye(a[0]);
        break;

    case PROTO_OP_SHOW_NUM:
        App_Display_ShowNumber(a[0]);
        break;

    case PROTO_OP_SHOW_OFF:
        App_Display_Off();
        break;

    case PROTO_OP_PLAY_VOICE:
        /* item 1..10 → P01..P10 播报 ID 54..63 */
        Bsp_UartAsr_SendPlay((uint8_t)(ASR_VOICE_P01 + (a[0] - 1U)));
        break;

    case PROTO_OP_WAIT_VOICE:
        s_task.kind = PROG_TASK_WAIT_VOICE;
        s_task.seq = seq;
        s_task.opcode = opcode;
        s_task.voice_item = a[0];
        break;

    default:
        break;
    }
}

/* ---------- 模式进出 ---------- */

static void Prog_Enter(void)
{
    if (App_Mode_Get() == APP_MODE_PROGRAM) return;   /* 幂等：不重复初始化/播报 */

    Prog_CancelTask();
    Prog_StopDone();
    Bsp_Motor_BrakeAll();

    App_Mode_Switch(APP_MODE_PROGRAM, 0);
    Bsp_Led_AllOff();

    s_marquee_idx = 0;
    s_marquee_ms = Prog_Now();
    Bsp_Led_On(LED_1);

    s_hb_seen = 1;                 /* 进入即给 1s 心跳宽限，避免首帧前误退出 */
    s_last_hb = Prog_Now();
    s_fault = 0;

    Bsp_UartAsr_SendPlay(ASR_VOICE_ENTER_PROGRAM);
}

void App_Program_ExitTo(uint8_t mode, uint8_t reason)
{
    if (App_Mode_Get() != APP_MODE_PROGRAM) return;

    Prog_CancelTask();
    Prog_StopDone();
    Bsp_Motor_BrakeAll();
    App_Display_Release();

    App_Mode_Switch((App_Mode_t)mode, 0);            /* 不追加目标模式播报 */
    Bsp_UartAsr_SendPlay(ASR_VOICE_EXIT_PROGRAM);    /* 只播“退出编程模式” */

    uint8_t d[8] = {0};
    d[0] = reason;
    d[1] = mode;
    Proto_Ble_SendEvent(PROTO_EVT_PROGRAM_ABORT, d);
}

/* 进入遥控：在编程模式时先清理并上报中止，其它模式直接切换（幂等） */
static void Prog_EnterRemote(void)
{
    uint8_t mode = App_Mode_Get();
    if (mode == APP_MODE_PROGRAM) {
        Prog_CancelTask();
        Prog_StopDone();
        Bsp_Motor_BrakeAll();
        App_Display_Release();
        App_Mode_Switch(APP_MODE_REMOTE, 1);         /* 播“遥控模式” */
        uint8_t d[8] = {0};
        d[0] = PROTO_ABORT_APP_MODE;
        d[1] = APP_MODE_REMOTE;
        Proto_Ble_SendEvent(PROTO_EVT_PROGRAM_ABORT, d);
    } else if (mode != APP_MODE_REMOTE) {
        App_Mode_Switch(APP_MODE_REMOTE, 1);
    }
}

/* ---------- 查询与事件 ---------- */

static void Prog_SendStatus(uint8_t seq)
{
    uint8_t d[7];
    uint16_t mv = Bsp_Battery_GetVoltage();

    d[0] = (uint8_t)App_Mode_Get();
    d[1] = App_Program_ReadIr(PROTO_IR_LEFT);
    d[2] = App_Program_ReadIr(PROTO_IR_CENTER);
    d[3] = App_Program_ReadIr(PROTO_IR_RIGHT);
    d[4] = (uint8_t)(mv & 0xFFU);
    d[5] = (uint8_t)(mv >> 8);
    d[6] = (uint8_t)((App_Program_TaskCode() << 4) | (s_fault & 0x0FU));

    Proto_Ble_SendResponse(seq, PROTO_OP_QUERY_STATUS, PROTO_RESULT_OK, d);
}

static void Prog_PollIrEvent(uint32_t now)
{
    if (!Prog_Due(now, s_ir_last_ms + PROG_IR_POLL_MS)) return;
    s_ir_last_ms = now;

    uint8_t l = App_Program_ReadIr(PROTO_IR_LEFT);
    uint8_t c = App_Program_ReadIr(PROTO_IR_CENTER);
    uint8_t r = App_Program_ReadIr(PROTO_IR_RIGHT);
    uint8_t mask = (uint8_t)(((l >= PROG_IR_EVT_TH) ? 0x01U : 0U) |
                             ((c >= PROG_IR_EVT_TH) ? 0x02U : 0U) |
                             ((r >= PROG_IR_EVT_TH) ? 0x04U : 0U));

    if (s_ir_last_mask == 0xFFU) {   /* 首轮只记基线，不上报 */
        s_ir_last_mask = mask;
        return;
    }
    if (mask != s_ir_last_mask) {
        s_ir_last_mask = mask;
        /* D3 红外事件只服务编程上位机；遥控/语音等模式下没必要上行，
           否则会占用 ECB02 的收发通道，抢遥控 C1 帧（2026-09-22 定位）。 */
        if (App_Mode_Get() != APP_MODE_PROGRAM) return;
        uint8_t d[8] = {0};
        d[0] = l;
        d[1] = c;
        d[2] = r;
        d[3] = mask;
        Proto_Ble_SendEvent(PROTO_EVT_IR_CHANGE, d);
    }
}

/* ---------- 对外接口 ---------- */

void App_Program_Init(void)
{
    s_task.kind = PROG_TASK_NONE;
    s_motor_power = 3;
    s_move_power = 3;
    s_hb_seen = 0;
    s_fault = 0;
    s_done_active = 0;
    s_marquee_idx = 0;
    s_marquee_ms = 0;
    s_ir_last_mask = 0xFF;
    s_ir_last_ms = 0;
}

uint8_t App_Program_TaskCode(void)
{
    return (uint8_t)s_task.kind;
}

uint8_t App_Program_IsActive(void)
{
    return (App_Mode_Get() == APP_MODE_PROGRAM) ? 1U : 0U;
}

void App_Program_HandleFrame(const Proto_Ble_Frame_t *frame)
{
    if (frame->type != PROTO_BLE_TYPE_C2) return;

    uint8_t seq = frame->data[0];
    uint8_t opcode = frame->data[1];
    const uint8_t *args = &frame->data[2];
    uint32_t now = Prog_Now();

    /* 全局指令：任何模式都处理 */
    switch (opcode) {
    case PROTO_OP_ENTER_PROGRAM:
        /* 幂等进入时 Prog_Enter 会提前返回，这里单独续期，保证语义统一 */
        if (App_Mode_Get() == APP_MODE_PROGRAM) Prog_KeepAlive(now);
        Prog_Enter();
        return;
    case PROTO_OP_ENTER_REMOTE:
        Prog_EnterRemote();
        return;
    case PROTO_OP_HEARTBEAT:
        Prog_KeepAlive(now);
        return;
    case PROTO_OP_QUERY_STATUS:
        if (App_Mode_Get() == APP_MODE_PROGRAM) Prog_KeepAlive(now);
        Prog_SendStatus(seq);
        return;
    default:
        break;
    }

    /* 非编程模式：静默忽略动作类 C2（仅上面 4 个全局指令有效） */
    if (App_Mode_Get() != APP_MODE_PROGRAM) return;

    if (opcode == PROTO_OP_STOP_PROGRAM) {
        /* 停止程序：取消任务/回报、刹停双电机，留在编程模式，不回报 */
        Prog_KeepAlive(now);
        Prog_CancelTask();
        Prog_StopDone();
        Bsp_Motor_BrakeAll();
        return;
    }

    if (opcode == PROTO_OP_READ_IR) {
        /* 查询类：不影响任务与 DONE 重报；语义恒定有效，可续期 */
        Prog_KeepAlive(now);
        uint8_t d[7] = {0};
        d[0] = App_Program_ReadIr(PROTO_IR_LEFT);
        d[1] = App_Program_ReadIr(PROTO_IR_CENTER);
        d[2] = App_Program_ReadIr(PROTO_IR_RIGHT);
        Proto_Ble_SendResponse(seq, opcode, PROTO_RESULT_OK, d);
        return;
    }

    /* 未知操作码：回错误，不打断当前任务 */
    uint8_t result = Prog_Validate(opcode, args);
    if (result == PROTO_RESULT_BAD_OPCODE) {
        Prog_SendError(seq, opcode, result);
        uint8_t d[8] = {0};
        d[0] = PROTO_PERR_OPCODE;
        Proto_Ble_SendEvent(PROTO_EVT_PROTOCOL_ERR, d);
        return;
    }

    /* 已知动作指令：先停止旧 DONE 重报、取消旧任务（必要时刹停其电机），再校验执行 */
    Prog_StopDone();
    Prog_CancelTask();

    if (result != PROTO_RESULT_OK) {
        Prog_SendError(seq, opcode, result);
        return;
    }

    Prog_KeepAlive(now);   /* 仅语义有效的动作指令可作为会话活跃凭据 */
    Prog_Execute(seq, opcode, args);
}

void App_Program_OnAsrCmd(uint8_t cmd)
{
    if (App_Mode_Get() != APP_MODE_PROGRAM) return;

    /* 识别词 → ASR_01..ASR_10 序号。
     * 兼容性：ASR_09/ASR_10 与旧动作命令 6/7 词条同名（前进/后退），而 ASRPRO 同一
     * 词条只能绑一个 ID。若模型把“前进/后退”绑到 cmd=6/7，等待 ASR_09/ASR_10 的
     * Blockly 流程也应被满足，否则等待会永远不返回。 */
    uint8_t item;
    if (cmd >= ASR_CMD_ASR_01 && cmd <= ASR_CMD_ASR_10) {
        item = (uint8_t)(cmd - ASR_CMD_ASR_01 + 1U);
    } else if (cmd == ASR_CMD_FORWARD) {
        item = 9U;    /* 前进 = ASR_09 */
    } else if (cmd == ASR_CMD_BACKWARD) {
        item = 10U;   /* 后退 = ASR_10 */
    } else {
        return;
    }

    if (s_task.kind == PROG_TASK_WAIT_VOICE && s_task.voice_item == item) {
        Prog_CompleteTask();
    }
}

void App_Program_Update(void)
{
    uint32_t now = Prog_Now();

    /* 红外变化事件：所有模式都推送（App 可用于状态显示） */
    Prog_PollIrEvent(now);

    if (App_Mode_Get() != APP_MODE_PROGRAM) return;

    /* 心跳超时：连接异常，刹停并退出编程模式 */
    if (s_hb_seen && Prog_Due(now, s_last_hb + PROG_HB_TIMEOUT_MS)) {
        s_fault |= PROTO_FAULT_HEARTBEAT;
        App_Program_ExitTo(APP_MODE_VOICE, PROTO_ABORT_HEARTBEAT);
        return;
    }

    /* 四灯跑马（每 200ms 移动一灯） */
    if (Prog_Due(now, s_marquee_ms + PROG_MARQUEE_MS)) {
        s_marquee_ms = now;
        s_marquee_idx = (uint8_t)((s_marquee_idx + 1U) & 0x03U);
        Bsp_Led_AllOff();
        Bsp_Led_On((Bsp_Led_Id_t)(LED_1 + s_marquee_idx));
    }

    /* 任务状态机（同一时刻最多一个等待型任务） */
    switch (s_task.kind) {
    case PROG_TASK_MOTOR_TIME:
        if (Prog_Due(now, s_task.start_ms + s_task.duration_ms)) {
            Prog_CompleteTask();
        }
        break;

    case PROG_TASK_WAIT_IR:
        if (Prog_Due(now, s_task.ir_next_ms)) {
            s_task.ir_next_ms = now + PROTO_PROG_IR_SAMPLE_MS;
            uint8_t v = App_Program_ReadIr(s_task.ir_ch);
            uint8_t hit = (s_task.ir_cmp == PROTO_IR_CMP_GT) ? (v > s_task.ir_th)
                                                             : (v < s_task.ir_th);
            if (hit) {
                s_task.ir_cnt++;
                if (s_task.ir_cnt >= PROTO_PROG_IR_STABLE_CNT) Prog_CompleteTask();
            } else {
                s_task.ir_cnt = 0;
            }
        }
        break;

    case PROG_TASK_WAIT_VOICE:
    case PROG_TASK_NONE:
    default:
        break;
    }

    /* DONE 重报：完成后每 200ms 重发，直到新指令/模式切换（心跳/查询不打断） */
    if (s_done_active && Prog_Due(now, s_done_next)) {
        uint8_t data[7] = {0};
        Proto_Ble_SendResponse(s_done_seq, s_done_op, PROTO_RESULT_OK, data);
        s_done_next = now + PROG_DONE_REPEAT_MS;
    }
}
