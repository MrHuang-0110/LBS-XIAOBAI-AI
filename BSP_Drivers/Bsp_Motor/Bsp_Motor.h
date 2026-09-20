#ifndef BSP_MOTOR_H
#define BSP_MOTOR_H
#include "py32f0xx_hal.h"

typedef enum {
    MOTOR_LEFT  = 0,
    MOTOR_RIGHT = 1,
} Bsp_Motor_Id_t;

typedef enum {
    MOTOR_DIR_STOP     = 0,   /* 双输入低：安全滑行 */
    MOTOR_DIR_FORWARD  = 1,
    MOTOR_DIR_BACKWARD = 2,
} Bsp_Motor_Dir_t;

/* 速度档位（占空比）：1=40% / 2=70% / 3=100%。
   文档 §11 遥控器 3 档速度对应这三档。 */
typedef enum {
    MOTOR_SPEED_LOW  = 1,   /* 40%  */
    MOTOR_SPEED_MID  = 2,   /* 70%  */
    MOTOR_SPEED_HIGH = 3,   /* 100% */
} Bsp_Motor_Speed_t;

/** 初始化 TIM3 4 通道 PWM 20kHz，全部滑行 */
void Bsp_Motor_Init(void);

/** 设置某电机方向 + 速度档位（STOP = 双输入低滑行） */
void Bsp_Motor_Set(Bsp_Motor_Id_t id, Bsp_Motor_Dir_t dir, Bsp_Motor_Speed_t speed);

/** 停两个电机（滑行） */
void Bsp_Motor_StopAll(void);

/**
 * @brief 主动短刹：双输入 100% 高电平（已确认驱动真值 = 刹车）。
 *        刹停脉冲 MOTOR_BRAKE_MS 后由 Bsp_Motor_Update 释放为滑行；
 *        同一次停止期间重复调用不会延长。再次驱动电机后可重新刹停。
 */
void Bsp_Motor_Brake(Bsp_Motor_Id_t id);

/** 短刹两个电机 */
void Bsp_Motor_BrakeAll(void);

/** 主循环调用：刹停脉冲计时到点后释放为滑行（非阻塞） */
void Bsp_Motor_Update(void);

#endif
