#include "App_Main.h"
#include "Bsp.h"
#include "Proto_Asr.h"
#include "Proto_Ble.h"
#include "Proto_Remote.h"
#include "App_Mode.h"
#include "App_Mode_Power.h"
#include "App_Mode_Sensor.h"
#include "App_Mode_Remote.h"
#include "App_Mode_Voice.h"
#include "App_Display.h"
#include "App_Program.h"
#include "App_Breath.h"
#include "App_Shutdown.h"
#include "App_Battery.h"

/* 临时诊断（2026-09-22）：遥控模式把 C1 键位/帧活动画到眼睛屏，用于定位
   长按顿挫。问题已定位，改回 0；需要再查遥控接收时改成 1。 */
#define REMOTE_RX_DIAG  0

/* 实体按键 → 目标模式（2026-09-16：KEY1 语音 / KEY2 动力 / KEY3 遥控 / KEY4 感应） */
static App_Mode_t Key_TargetMode(Bsp_Key_Id_t kid)
{
    switch (kid) {
    case KEY_ID_2: return APP_MODE_POWER;
    case KEY_ID_3: return APP_MODE_REMOTE;
    case KEY_ID_4: return APP_MODE_SENSOR;
    case KEY_ID_1:
    default:       return APP_MODE_VOICE;
    }
}

void App_Init(void)
{
    Proto_Ble_Init();
    App_Program_Init();

    /* 应用层时序：等 ASRPRO 启动 + 默认进入语音模式 */
    Bsp_Tick_DelayMs(1500);
    /* v0.7 ID 17 = "你好呀我是小白进入语音模式"，本身就是合并句，
       单独播；App_Mode_Switch 用 play_voice=0 静默切避免重复 */
    Bsp_UartAsr_SendPlay(ASR_VOICE_BOOT);
    App_Mode_Switch(APP_MODE_VOICE, 0);

    /* PA9 呼吸灯默认关闭：wake 启动 / sleep 立即熄灭（ASRPRO 事件驱动，无本地超时） */
    App_Breath_Init();

    /* 显示默认 EYE_01 待机表情（App_Display 按帧时长循环） */
    App_Display_Init();

    /* BLE 配名（BLE 上电后留 500ms） */
    Bsp_Tick_DelayMs(500);
    Bsp_UartBle_ConfigName("Spark_AI", 11);
}

/* 处理一条 ASRPRO 事件 */
static void App_HandleAsr(const Bsp_UartAsr_Event_t *e)
{
    if (e->type == ASR_EVT_CMD) {
        /* 全局安全命令：任何模式响应关机 */
        if (e->arg == ASR_CMD_SHUTDOWN) {
            App_Shutdown_Execute();
        }
        /* 编程模式：旧模式/动作语音不抢占程序；只把 ASR_01-ASR_10 交给等待词条任务 */
        else if (App_Mode_Get() == APP_MODE_PROGRAM) {
            App_Program_OnAsrCmd(e->arg);
        }
        /* 语音切模式（动力/感应/遥控/语音） */
        else if (e->arg >= ASR_CMD_ENTER_POWER && e->arg <= ASR_CMD_ENTER_VOICE) {
            switch (e->arg) {
            case ASR_CMD_ENTER_POWER:  App_Mode_Switch(APP_MODE_POWER, 1);  break;
            case ASR_CMD_ENTER_SENSOR: App_Mode_Switch(APP_MODE_SENSOR, 1); break;
            case ASR_CMD_ENTER_REMOTE: App_Mode_Switch(APP_MODE_REMOTE, 1); break;
            case ASR_CMD_ENTER_VOICE:  App_Mode_Switch(APP_MODE_VOICE, 1);  break;
            default: break;
            }
        }
        /* 仅语音模式响应动作命令（11 条，App_Mode_Voice 内部判模式） */
        else if (e->arg >= ASR_CMD_FORWARD && e->arg <= ASR_CMD_R_STOP) {
            App_Mode_Voice_OnCmd(e->arg);
        }
    }
    else if (e->type == ASR_EVT_WAKE) {
        /* 唤醒：启动呼吸灯，并上报 D3 WAKE（不再发 play，唤醒应答由 ASRPRO 本地完成） */
        App_Breath_Start();
        uint8_t d[8] = {0};
        Proto_Ble_SendEvent(PROTO_EVT_WAKE, d);
    }
    else if (e->type == ASR_EVT_SLEEP) {
        /* 休眠：立即熄灭呼吸灯，并上报 D3 SLEEP */
        App_Breath_Stop();
        uint8_t d[8] = {0};
        Proto_Ble_SendEvent(PROTO_EVT_SLEEP, d);
    }
    else if (e->type == ASR_EVT_DONE) {
        /* done=NN：播报完成回执，无需动作 */
    }
}

/* 处理实体按键 */
static void App_HandleKey(Bsp_Key_Id_t kid)
{
    if (App_Mode_Get() == APP_MODE_PROGRAM) {
        /* 编程模式任一模式键：取消程序并刹停，切到对应模式，只播“退出编程模式” */
        App_Program_ExitTo(Key_TargetMode(kid), PROTO_ABORT_KEY);
        return;
    }

    switch (kid) {
    case KEY_ID_1: App_Mode_Switch(APP_MODE_VOICE, 1);  break;
    case KEY_ID_2:  /* 动力：已在动力模式则切动作 */
        if (App_Mode_Get() == APP_MODE_POWER) App_Mode_Power_OnKey();
        else                                   App_Mode_Switch(APP_MODE_POWER, 1);
        break;
    case KEY_ID_3: App_Mode_Switch(APP_MODE_REMOTE, 1); break;
    case KEY_ID_4:  /* 感应：已在感应模式则切玩法 */
        if (App_Mode_Get() == APP_MODE_SENSOR) App_Mode_Sensor_OnKey();
        else                                    App_Mode_Switch(APP_MODE_SENSOR, 1);
        break;
    default: break;
    }
}

/* 处理 BLE 字节流：C1 → 遥控模式；C2 → 编程/会话；D2/D3 为设备→App 方向，忽略 */
static void App_HandleBle(void)
{
    for (;;) {
        uint8_t buf[64];
        uint16_t n = Bsp_UartBle_TryRecv(buf, sizeof(buf));
        if (n == 0) break;
        Proto_Ble_Feed(buf, n);
    }

    Proto_Ble_Frame_t frame;
    while (Proto_Ble_GetFrame(&frame)) {
        if (frame.type == PROTO_BLE_TYPE_C1) {
            App_Mode_Remote_OnFrame(frame.data);   /* 内部判模式：仅遥控模式生效 */
        } else if (frame.type == PROTO_BLE_TYPE_C2) {
            App_Program_HandleFrame(&frame);
        }
    }
}

void App_Loop(void)
{
    /* PF3 连接状态边沿检测 */
    uint8_t ble_was_connected = Bsp_UartBle_IsConnected();

    while (1) {
        /* --- 按键扫描（4 键各管一个模式，KEY1 长按关机） --- */
        {
            Bsp_Key_Id_t kid;
            Bsp_Key_Evt_t ke = Bsp_Key_Poll(&kid);
            if (ke == KEY_EVT_SHORT) {
                App_HandleKey(kid);
            }
            else if (ke == KEY_EVT_LONG && kid == KEY_ID_1) {
                App_Shutdown_Execute();
            }
        }

        /* --- ASRPRO 语音事件 --- */
        {
            Bsp_UartAsr_Event_t e;
            if (Bsp_UartAsr_TryRecv(&e)) {
                App_HandleAsr(&e);
            }
        }

        /* --- BLE 连接状态边沿 + 协议帧分发 --- */
        {
            uint8_t ble_now = Bsp_UartBle_IsConnected();
            if (ble_now && !ble_was_connected) {
                Bsp_UartAsr_SendPlay(ASR_VOICE_BLE_CONNECTED);
            } else if (!ble_now && ble_was_connected) {
                Bsp_UartAsr_SendPlay(ASR_VOICE_BLE_LOST);
                /* 遥控模式断连：立即刹停（不等 1s 帧超时，防遥控器断电/出范围 */
                if (App_Mode_Get() == APP_MODE_REMOTE) {
                    Bsp_Motor_BrakeAll();
                }
                /* 编程模式断连：刹停、取消任务、退出回语音模式 */
                if (App_Mode_Get() == APP_MODE_PROGRAM) {
                    App_Program_ExitTo(APP_MODE_VOICE, PROTO_ABORT_BLE_LOST);
                }
            }
            ble_was_connected = ble_now;

            App_HandleBle();
        }

        /* --- 各模式驱动 --- */
        App_Mode_Power_Update();
        App_Mode_Sensor_Update();
        App_Mode_Remote_Update();

        /* --- 编程模式：跑马/心跳/任务/DONE 重报 --- */
        App_Program_Update();

        /* --- 电机刹停脉冲计时释放 --- */
        Bsp_Motor_Update();

        /* --- 显示：待机/手动表情帧步进 --- */
        App_Display_Update();

#if REMOTE_RX_DIAG
        /* 遥控诊断：每 200ms 画“本窗口收到的 C1 按键位”到点阵：
             列 1..10 = 键位 上/下/左/右/Y/A/X/B/R1/L1
             列 11    = 帧活动灯（本窗口收到任意 C1 帧）
           按键列闪、活动列稳 = 帧在到但键位断续（抖动）；两列同时灭 = 整帧断流。 */
        {
            static uint32_t diag_last = 0;
            static uint8_t  diag_was_remote = 0;
            uint32_t dnow = Bsp_Tick_GetMs();
            if ((dnow - diag_last) >= 200U) {
                diag_last = dnow;
                if (App_Mode_Get() == APP_MODE_REMOTE) {
                    uint16_t keys = App_Mode_Remote_TakeKeyMask();
                    uint16_t frames = App_Mode_Remote_TakeFrames();
                    App_Display_ShowKeyMap(keys, (frames != 0U) ? 1U : 0U);
                    diag_was_remote = 1;
                } else if (diag_was_remote) {
                    diag_was_remote = 0;
                    App_Display_Release();
                }
            }
        }
#endif

        /* --- PA9 呼吸灯：wake 启动 / sleep 熄灭 --- */
        App_Breath_Update();

        /* --- 电池采样 + 低电量播报 + D3 事件 --- */
        App_Battery_Update();

        /* --- ECB02 发送队列：避开刚收到的数据，按收发保护间隔逐帧发送 --- */
        Bsp_UartBle_Update();

        Bsp_Tick_DelayMs(5);
    }
}
