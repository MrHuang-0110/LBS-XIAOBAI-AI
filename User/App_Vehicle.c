#include "App_Vehicle.h"

void Vehicle_Drive(App_Vehicle_Dir_t dir, Bsp_Motor_Speed_t speed)
{
    switch (dir) {
    case VEHICLE_DIR_FORWARD:
        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  speed);
        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  speed);
        break;
    case VEHICLE_DIR_BACKWARD:
        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, speed);
        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, speed);
        break;
    case VEHICLE_DIR_LEFT:
        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_BACKWARD, speed);
        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_FORWARD,  speed);
        break;
    case VEHICLE_DIR_RIGHT:
        Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_FORWARD,  speed);
        Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_BACKWARD, speed);
        break;
    case VEHICLE_DIR_STOP:
    default:
        Bsp_Motor_StopAll();
        break;
    }
}

void Vehicle_DriveSingle(Bsp_Motor_Id_t motor, Bsp_Motor_Dir_t dir, Bsp_Motor_Speed_t speed)
{
    Bsp_Motor_Set(motor, dir, speed);
}
