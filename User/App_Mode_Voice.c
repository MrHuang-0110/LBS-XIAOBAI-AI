#include "App_Mode_Voice.h"
#include "App_Mode.h"
#include "Bsp.h"
#include "Proto_Asr.h"
#include "App_Vehicle.h"

void App_Mode_Voice_OnCmd(uint8_t cmd)
{
    if (App_Mode_Get() != APP_MODE_VOICE) return;
    if (cmd < ASR_CMD_FORWARD || cmd > ASR_CMD_R_STOP) return;

    /* 统一回播规则：MCU 实际动作 → 对应播报语（协议层查表） */
    Bsp_UartAsr_SendPlay(Proto_Asr_CmdToVoice(cmd));
    switch (cmd) {
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
