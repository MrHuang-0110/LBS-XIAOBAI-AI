#ifndef __APP_VEHICLE_H
#define __APP_VEHICLE_H
#include "Bsp.h"

/* 车辆双电机统一动作（消除动力/感应/遥控三处重复的 Bsp_Motor_Set 组合）。
 * LEFT/RIGHT 为坦克转向：LEFT=左反右正，RIGHT=左正右反。 */
typedef enum {
    VEHICLE_DIR_STOP    = 0,
    VEHICLE_DIR_FORWARD = 1,
    VEHICLE_DIR_BACKWARD = 2,
    VEHICLE_DIR_LEFT    = 3,
    VEHICLE_DIR_RIGHT   = 4,
} App_Vehicle_Dir_t;

/** 双电机统一驱动（含 STOP）。LEFT/RIGHT = 坦克转向。 */
void Vehicle_Drive(App_Vehicle_Dir_t dir, Bsp_Motor_Speed_t speed);

/** 单电机驱动（遥控模式单电机键用）。 */
void Vehicle_DriveSingle(Bsp_Motor_Id_t motor, Bsp_Motor_Dir_t dir, Bsp_Motor_Speed_t speed);

#endif
