#include "main.h"
#include "App_System.h"
#include "App_Vehicle.h"
#include "Bsp.h"
#include "Proto_Asr.h"
#include "Proto_Remote.h"
#include "App_Mode.h"
#include "App_Mode_Power.h"
#include "App_Mode_Sensor.h"
#include "App_Mode_Remote.h"

/* ===== TM1640 眼睛图案（8×14 点阵，左眼列0-6 / 右眼列7-13，各 7×8）=====
   椭圆形空心轮廓，眨眼=行3一条横线。 */
static const uint8_t eye_box[14] = {
    0x3C, 0x42, 0x81, 0x81, 0x81, 0x42, 0x3C,   /* 左眼 列0-6 椭圆轮廓 */
    0x3C, 0x42, 0x81, 0x81, 0x81, 0x42, 0x3C    /* 右眼 列7-13 椭圆轮廓 */
};
static const uint8_t eye_closed[14] = {
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,   /* 左眼闭 列0-6 行3 */
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08    /* 右眼闭 列7-13 行3 */
};
/* 未连接：双眨（睁2s → 闭100ms → 睁150ms → 闭100ms → 睁2s）*/
static const struct { uint16_t ms; uint8_t closed; } blink_idle[] = {
    {2000, 0}, {100, 1}, {150, 0}, {100, 1}, {2000, 0}
};
#define BLINK_IDLE_LEN (sizeof(blink_idle)/sizeof(blink_idle[0]))
/* 已连接：瞳孔移动（看左上 → 回中 → 看右上 → 回中）。
   瞳孔 3×3 实心方块：上方=行1-3(bit0x0E)，中=行3-5(bit0x38)，叠加 3 列宽。 */
static const uint8_t pupil_bit[3] = {0x0E, 0x38, 0x0E};
static const uint8_t pupil_lc[3]  = {1, 2, 4};       /* 左眼瞳孔起始列(0-6内) */
static const uint8_t pupil_rc[3]  = {8, 9, 11};      /* 右眼瞳孔起始列(7-13内) */
static const struct { uint16_t ms; uint8_t pos; } look_seq[] = {
    {500, 0}, {200, 1}, {500, 2}, {200, 1}
};
#define LOOK_LEN (sizeof(look_seq)/sizeof(look_seq[0]))

/* ===== 全局状态 ===== */
/* 呼吸灯状态（PA9，由 wake 唤醒启动，15s 超时自动关闭） */
static uint8_t  g_breathing = 0;       /* 1=呼吸中 0=关闭 */
static int16_t  g_breath_val = 0;
static uint8_t  g_breath_dir = 1;      /* 1=上升 0=下降 */
static uint32_t g_breath_t   = 0;      /* 呼吸动画步进时间戳 */
static uint32_t g_breath_start = 0;    /* 呼吸灯启动时刻，用于 15s 超时关灯 */

/* ===== 统一函数 ===== */

/* 低电量播报（带 5s 冷却，避免刷屏）。
   主循环周期检测：Bsp_Battery_IsLow() 为真时调这里，5s 播一次。 */
static void App_ReportLowBattery(void)
{
    static uint32_t last_report = 0;
    static uint8_t  reported = 0;
    uint32_t now = Bsp_Tick_GetMs();
    if (!reported || (now - last_report >= 5000)) {
        Bsp_UartAsr_SendPlay(ASR_VOICE_LOW_BATTERY);
        last_report = now;
        reported = 1;
    }
}

/* 完整关机流程：停电机 → 播关机语 → 关机动画 → 关 LED/TM1640 → 延时 1s → 断电。
   KEY1 长按和语音命令 cmd=1 共用。 */
static void PerformShutdown(void)
{
    Bsp_Motor_StopAll();
    Bsp_UartAsr_SendPlay(ASR_VOICE_SHUTDOWN);
    Bsp_Power_ShutdownAnimation();
    Bsp_LedPwm_Set(LEDPWM_1, 0);
    Bsp_LedPwm_Set(LEDPWM_2, 0);
    Bsp_Tm1640_Clear();
    Bsp_Tick_DelayMs(1000);
    Bsp_Power_ShutDown();
}

/* TM1640 眼睛动画：椭圆空心轮廓。
   未连接 → 双眨（灵动）
   已连接 → 瞳孔移动看左上/右上（AI 生命力）
   非阻塞，主循环周期调用。 */
static void Eye_Update(void)
{
    static uint32_t last_t = 0;
    static uint8_t  frame = 0;
    static uint8_t  was_connected = 0xFF;
    uint8_t connected = Bsp_UartBle_IsConnected();
    uint32_t now = Bsp_Tick_GetMs();

    /* 连接状态切换时重置，立刻显示第一帧 */
    if (connected != was_connected) {
        last_t = now;
        frame = 0;
        was_connected = connected;
        if (connected) {
            uint8_t buf[14];
            for (int i = 0; i < 14; i++) buf[i] = eye_box[i];
            for (int c = 0; c < 3; c++) {
                buf[pupil_lc[0] + c] |= pupil_bit[0];
                buf[pupil_rc[0] + c] |= pupil_bit[0];
            }
            Bsp_Tm1640_Refresh(buf);
        } else {
            Bsp_Tm1640_Refresh(eye_box);
        }
        return;
    }

    if (connected) {
        /* 瞳孔移动：看左上 → 回中 → 看右上 → 回中 */
        if (now - last_t >= look_seq[frame].ms) {
            last_t = now;
            frame = (uint8_t)((frame + 1) % LOOK_LEN);
            uint8_t buf[14];
            for (int i = 0; i < 14; i++) buf[i] = eye_box[i];
            uint8_t p = look_seq[frame].pos;
            for (int c = 0; c < 3; c++) {
                buf[pupil_lc[p] + c] |= pupil_bit[p];
                buf[pupil_rc[p] + c] |= pupil_bit[p];
            }
            Bsp_Tm1640_Refresh(buf);
        }
    } else {
        /* 双眨 */
        if (now - last_t >= blink_idle[frame].ms) {
            last_t = now;
            frame = (uint8_t)((frame + 1) % BLINK_IDLE_LEN);
            if (blink_idle[frame].closed) Bsp_Tm1640_Refresh(eye_closed);
            else                          Bsp_Tm1640_Refresh(eye_box);
        }
    }
}

int main(void)
{
    HAL_Init();
    App_System_Init();
    BSP_Init();

    /* 应用层时序：等 ASRPRO 启动 + 默认进入语音模式 */
    Bsp_Tick_DelayMs(1500);
    /* v0.7 ID 17 = "你好呀我是小白进入语音模式"，本身就是合并句，
       单独播；App_Mode_Switch 用 play_voice=0 静默切避免重复 */
    Bsp_UartAsr_SendPlay(ASR_VOICE_BOOT);
    App_Mode_Switch(APP_MODE_VOICE, 0);

    /* PA9 呼吸灯默认关闭，由 wake 唤醒启动 / 15s 超时自动关 */
    Bsp_LedPwm_Set(LEDPWM_1, 0);
    Bsp_LedPwm_Set(LEDPWM_2, 0);

    /* BLE 配名（BLE 上电后留 500ms） */
    Bsp_Tick_DelayMs(500);
    Bsp_UartBle_ConfigName("Spark_AI", 11);

    /* PF3 连接状态边沿检测 */
    uint8_t ble_was_connected = Bsp_UartBle_IsConnected();

    /* 遥控帧流缓冲已迁入协议层（Proto_Remote_Init 清空其内部缓冲） */
    Proto_Remote_Init();

    /* 动作命令防抖已移除：v0.7 下 ASRPRO 侧不误连发，每条 cmd 都执行。
       依赖 ASRPRO 侧命令冗余抑制；如未来发现误识别，在此处重新加防抖窗。 */

    while (1) {
        /* --- 按键扫描（4 键各管一个模式，KEY1 长按关机） --- */
        {
            Bsp_Key_Id_t kid;
            Bsp_Key_Evt_t ke = Bsp_Key_Poll(&kid);
            if (ke == KEY_EVT_SHORT) {
                switch (kid) {
                case KEY_ID_1: App_Mode_Switch(APP_MODE_VOICE, 1);  break;  /* LED1 */
                case KEY_ID_2:  /* LED2 感应模式：已在感应模式则切玩法 */
                    if (App_Mode_Get() == APP_MODE_SENSOR) {
                        App_Mode_Sensor_OnKey();
                    } else {
                        App_Mode_Switch(APP_MODE_SENSOR, 1);
                    }
                    break;
                case KEY_ID_3: App_Mode_Switch(APP_MODE_REMOTE, 1); break;  /* LED3 */
                case KEY_ID_4:  /* LED4 动力模式：已在动力模式则切动作 */
                    if (App_Mode_Get() == APP_MODE_POWER) {
                        App_Mode_Power_OnKey();
                    } else {
                        App_Mode_Switch(APP_MODE_POWER, 1);
                    }
                    break;
                default: break;
                }
            }
            else if (ke == KEY_EVT_LONG && kid == KEY_ID_1) {
                /* === 关机流程 === */
                PerformShutdown();
            }
        }

        /* --- ASRPRO 语音命令 --- */
        {
            Bsp_UartAsr_Event_t e;
            if (Bsp_UartAsr_TryRecv(&e)) {
                if (e.type == ASR_EVT_CMD) {
                    /* 段1: 任何模式响应（关机） */
                    if (e.arg == ASR_CMD_SHUTDOWN) {
                        PerformShutdown();
                    }
                    /* 段2: 任何模式响应（切模式 4 选 1） */
                    else if (e.arg >= ASR_CMD_ENTER_POWER && e.arg <= ASR_CMD_ENTER_VOICE) {
                        switch (e.arg) {
                        case ASR_CMD_ENTER_POWER:  App_Mode_Switch(APP_MODE_POWER, 1);  break;
                        case ASR_CMD_ENTER_SENSOR: App_Mode_Switch(APP_MODE_SENSOR, 1); break;
                        case ASR_CMD_ENTER_REMOTE: App_Mode_Switch(APP_MODE_REMOTE, 1); break;
                        case ASR_CMD_ENTER_VOICE:  App_Mode_Switch(APP_MODE_VOICE, 1);  break;
                        }
                    }
                    /* 段3: 仅语音模式响应（动作命令 11 条） */
                    else if (App_Mode_Get() == APP_MODE_VOICE &&
                             e.arg >= ASR_CMD_FORWARD && e.arg <= ASR_CMD_R_STOP) {
                        /* 统一回播规则：MCU 实际动作 → 对应播报语 */
                        Bsp_UartAsr_SendPlay(Proto_Asr_CmdToVoice(e.arg));
                        switch (e.arg) {
                        case ASR_CMD_FORWARD:  Vehicle_Drive(VEHICLE_DIR_FORWARD,  MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_BACKWARD: Vehicle_Drive(VEHICLE_DIR_BACKWARD, MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_LEFT:     Vehicle_Drive(VEHICLE_DIR_LEFT,     MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_RIGHT:    Vehicle_Drive(VEHICLE_DIR_RIGHT,    MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_STOP:     Vehicle_Drive(VEHICLE_DIR_STOP,     MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_L_FWD:  Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_L_REV:  Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_L_STOP: Vehicle_DriveSingle(MOTOR_LEFT,  MOTOR_DIR_STOP,     MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_R_FWD:  Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_R_REV:  Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, MOTOR_SPEED_HIGH); break;
                        case ASR_CMD_R_STOP: Vehicle_DriveSingle(MOTOR_RIGHT, MOTOR_DIR_STOP,     MOTOR_SPEED_HIGH); break;
                        default: break;
                        }
                    }
                }
                else if (e.type == ASR_EVT_WAKE) {
                    /* 唤醒：启动呼吸灯（15s 后主循环超时自动关）。
                       不再发 play=10，唤醒应答由 ASRPRO 本地回复词"我在"承担，响应更快 */
                    g_breathing = 1;
                    g_breath_val = 0;
                    g_breath_dir = 1;
                    g_breath_t = Bsp_Tick_GetMs();
                    g_breath_start = g_breath_t;
                }
                else if (e.type == ASR_EVT_DONE) {
                    /* done=NN：播报完成回执，仅供参考，无需动作 */
                }
            }
        }

        /* --- BLE 连接状态边沿 + 语音播报 --- */
        {
            uint8_t ble_now = Bsp_UartBle_IsConnected();
            if (ble_now && !ble_was_connected) {
                Bsp_UartAsr_SendPlay(ASR_VOICE_BLE_CONNECTED);
            } else if (!ble_now && ble_was_connected) {
                Bsp_UartAsr_SendPlay(ASR_VOICE_BLE_LOST);
            }
            ble_was_connected = ble_now;
        }

        /* --- BLE 遥控帧解析（协议层校验，应用层消费） --- */
        {
            uint8_t buf[REMOTE_FRAME_LEN * 2];
            uint16_t n = Bsp_UartBle_TryRecv(buf, sizeof(buf));
            Proto_Remote_Feed(buf, n);
            uint8_t keys[REMOTE_KEY_COUNT];
            while (Proto_Remote_GetFrame(keys)) {
                App_Mode_Remote_OnFrame(keys);
            }
        }

        /* --- TM1640 眼睛动画（未连接慢闪 / 连接后转动）--- */
        Eye_Update();

        /* --- PA9 呼吸灯：wake 唤醒启动，15s 超时自动关 --- */
        if (g_breathing && (Bsp_Tick_GetMs() - g_breath_start >= 15000)) {
            g_breathing = 0;
            g_breath_val = 0;
            g_breath_dir = 1;
            Bsp_LedPwm_Set(LEDPWM_2, 0);
        }
        if (g_breathing && (Bsp_Tick_GetMs() - g_breath_t >= 10)) {
            g_breath_t = Bsp_Tick_GetMs();
            if (g_breath_dir) {
                g_breath_val++;
                if (g_breath_val >= 100) { g_breath_val = 100; g_breath_dir = 0; }
            } else {
                g_breath_val--;
                if (g_breath_val <= 0) { g_breath_val = 0; g_breath_dir = 1; }
            }
            Bsp_LedPwm_Set(LEDPWM_2, (uint8_t)g_breath_val);
        }

        /* --- 动力模式电机驱动（主循环持续驱动，跟感应模式架构一致）--- */
        App_Mode_Power_Update();

        /* --- 感应模式执行（文档 §9）--- */
        App_Mode_Sensor_Update();

        /* --- 遥控模式超时停机（1s 没收到帧才停，防断连电机狂转；
               正常遥控器持续发帧间隔远小于 1s，不会误触发）--- */
        App_Mode_Remote_Update();

        /* --- 电池采样：10ms 一次入滤波窗口 --- */
        {
            static uint32_t last_batt = 0;
            if (Bsp_Tick_GetMs() - last_batt >= 10) {
                last_batt = Bsp_Tick_GetMs();
                Bsp_Battery_Poll();
            }
        }

        /* --- 低电量周期播报：滤波+迟滞后仍处于低电量态，由 App_ReportLowBattery 的
               5s 冷却控制播报频率；电压恢复到非低电量态后，reported 保持 1，等 5s
               冷却期过后若再进入低电量能立刻播（可接受，只在真的抖回来才响）。 */
        if (Bsp_Battery_IsLow()) {
            App_ReportLowBattery();
        }

        Bsp_Tick_DelayMs(5);
    }
}

