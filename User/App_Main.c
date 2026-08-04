#include "App_Main.h"
#include "Bsp.h"
#include "Proto_Asr.h"
#include "Proto_Remote.h"
#include "App_Mode.h"
#include "App_Mode_Power.h"
#include "App_Mode_Sensor.h"
#include "App_Mode_Remote.h"
#include "App_Mode_Voice.h"
#include "App_Eye.h"
#include "App_Breath.h"
#include "App_Shutdown.h"
#include "App_Battery.h"

void App_Init(void)
{
    /* 应用层时序：等 ASRPRO 启动 + 默认进入语音模式 */
    Bsp_Tick_DelayMs(1500);
    /* v0.7 ID 17 = "你好呀我是小白进入语音模式"，本身就是合并句，
       单独播；App_Mode_Switch 用 play_voice=0 静默切避免重复 */
    Bsp_UartAsr_SendPlay(ASR_VOICE_BOOT);
    App_Mode_Switch(APP_MODE_VOICE, 0);

    /* PA9 呼吸灯默认关闭，由 wake 唤醒启动 / 15s 超时自动关 */
    App_Breath_Init();

    /* BLE 配名（BLE 上电后留 500ms） */
    Bsp_Tick_DelayMs(500);
    Bsp_UartBle_ConfigName("Spark_AI", 11);

    Proto_Remote_Init();
}

void App_Loop(void)
{
    /* PF3 连接状态边沿检测 */
    uint8_t ble_was_connected = Bsp_UartBle_IsConnected();

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
                App_Shutdown_Execute();
            }
        }

        /* --- ASRPRO 语音命令 --- */
        {
            Bsp_UartAsr_Event_t e;
            if (Bsp_UartAsr_TryRecv(&e)) {
                if (e.type == ASR_EVT_CMD) {
                    /* 段1: 任何模式响应（关机） */
                    if (e.arg == ASR_CMD_SHUTDOWN) {
                        App_Shutdown_Execute();
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
                    /* 段3: 仅语音模式响应（动作命令 11 条，App_Mode_Voice 内部判模式） */
                    else if (e.arg >= ASR_CMD_FORWARD && e.arg <= ASR_CMD_R_STOP) {
                        App_Mode_Voice_OnCmd(e.arg);
                    }
                }
                else if (e.type == ASR_EVT_WAKE) {
                    /* 唤醒：启动呼吸灯（15s 后主循环超时自动关）。
                       不再发 play=10，唤醒应答由 ASRPRO 本地回复词"我在"承担，响应更快 */
                    App_Breath_Start();
                }
                else if (e.type == ASR_EVT_DONE) {
                    /* done=NN：播报完成回执，仅供参考，无需动作 */
                }
            }
        }

        /* --- BLE 连接状态边沿 + 语音播报 + 遥控帧喂协议层 --- */
        {
            uint8_t ble_now = Bsp_UartBle_IsConnected();
            if (ble_now && !ble_was_connected) {
                Bsp_UartAsr_SendPlay(ASR_VOICE_BLE_CONNECTED);
            } else if (!ble_now && ble_was_connected) {
                Bsp_UartAsr_SendPlay(ASR_VOICE_BLE_LOST);
            }
            ble_was_connected = ble_now;

            uint8_t buf[REMOTE_FRAME_LEN * 2];
            uint16_t n = Bsp_UartBle_TryRecv(buf, sizeof(buf));
            Proto_Remote_Feed(buf, n);
            uint8_t keys[REMOTE_KEY_COUNT];
            while (Proto_Remote_GetFrame(keys)) {
                App_Mode_Remote_OnFrame(keys);
            }
        }

        /* --- 各模式驱动 --- */
        App_Mode_Power_Update();
        App_Mode_Sensor_Update();
        App_Mode_Remote_Update();

        /* --- TM1640 眼睛动画（未连接慢闪 / 连接后转动）--- */
        App_Eye_Update();

        /* --- PA9 呼吸灯：wake 唤醒启动，15s 超时自动关 --- */
        App_Breath_Update();

        /* --- 电池采样 + 低电量播报 --- */
        App_Battery_Update();

        Bsp_Tick_DelayMs(5);
    }
}
