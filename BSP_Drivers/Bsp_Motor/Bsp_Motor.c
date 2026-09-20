#include "Bsp_Motor/Bsp_Motor.h"
#include "Bsp_Tick/Bsp_Tick.h"

/*
 * HAL 库版本，TIM3 4 通道 PWM，20 kHz，方向 + 3 档速度。
 *
 * 引脚（Datasheet V2.5 §3.1/§3.2）：
 *   PA6=CH1 (AF1) L_A   PA7=CH2 (AF1) L_B
 *   PB0=CH3 (AF1) R_A   PB1=CH4 (AF1) R_B
 *
 * 关键坑：HAL_TIM_PWM_ConfigChannel 默认打开 OCxPE (CCR preload)，之后 HAL_TIM_PWM_Start
 *   只翻 CEN 位、不生成 UG。CCMR/CCER 里的位改动没落地到工作寄存器 → PWM 永远不出。
 *   修复方案：Init 末尾主动补一次 EGR.UG=1，把配置从 preload 传到工作寄存器。
 *
 * 刹停（2026-09-16 需求）：驱动真值为 IN_A=高 + IN_B=高 = 刹车。
 *   用两路 100% 高电平短刹 MOTOR_BRAKE_MS，之后自动回到双输入低滑行；
 *   同一次停止期间重复 Brake 不延长脉冲；再次驱动电机后可重新刹停。
 */

#define MOTOR_TIM_PERIOD  2399U    /* 48MHz / (2400) = 20 kHz */
/* 3 档速度占空比（CCR 值）：40% / 70% / 100% */
#define MOTOR_DUTY_40     960U     /* 2400 * 40% = 960  */
#define MOTOR_DUTY_70     1680U    /* 2400 * 70% = 1680 */
#define MOTOR_DUTY_100    2399U    /* 2400 * 100% = 2400，clamp 到 PERIOD */

#define MOTOR_BRAKE_MS    150U     /* 短刹脉冲时长，之后释放为滑行 */

static TIM_HandleTypeDef htim3;

typedef struct {
    Bsp_Motor_Dir_t dir;          /* 最近一次驱动方向（STOP=滑行） */
    uint8_t  braked;              /* 1=刹车脉冲进行中 */
    uint8_t  brake_done;          /* 1=本次停止已完成刹停（避免重复延长） */
    uint32_t brake_start;         /* 刹车脉冲起始 tick */
} Motor_State_t;

static Motor_State_t g_state[2] = {
    { MOTOR_DIR_STOP, 0, 1, 0 },   /* 上电双输入低：安全滑行 */
    { MOTOR_DIR_STOP, 0, 1, 0 },
};

/* HAL_TIM_PWM_Init 会调用此回调（SDK 例程 Bsp_Timer1_PWM.c 的标准做法）*/
void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3) {
        // pi-lens-ignore: no-reserved-identifiers -- 普冉 HAL 供应商宏，不可重命名
        __HAL_RCC_TIM3_CLK_ENABLE();
        // pi-lens-ignore: no-reserved-identifiers
        __HAL_RCC_GPIOA_CLK_ENABLE();
        // pi-lens-ignore: no-reserved-identifiers
        __HAL_RCC_GPIOB_CLK_ENABLE();

        GPIO_InitTypeDef gi = {0};
        gi.Mode      = GPIO_MODE_AF_PP;
        gi.Pull      = GPIO_NOPULL;
        gi.Speed     = GPIO_SPEED_FREQ_HIGH;
        gi.Alternate = GPIO_AF1_TIM3;   /* 4 路 TIM3 都是 AF1（datasheet 亲测）*/

        gi.Pin = GPIO_PIN_6 | GPIO_PIN_7;
        HAL_GPIO_Init(GPIOA, &gi);

        gi.Pin = GPIO_PIN_0 | GPIO_PIN_1;
        HAL_GPIO_Init(GPIOB, &gi);
    }
}

void Bsp_Motor_Init(void)
{
    htim3.Instance               = TIM3;
    htim3.Init.Prescaler         = 19;   /* 48MHz/20=2.4MHz 计数，/2400=1kHz */
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = MOTOR_TIM_PERIOD;
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_PWM_Init(&htim3);

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = 0;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;

    HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_2);
    HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_3);
    HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_4);

    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);

    /* 关键补丁：让 preload 中的 CCMR/CCER 值传到工作寄存器 */
    // pi-lens-ignore: no-reserved-identifiers -- 普冉 HAL 供应商宏，不可重命名
    __HAL_TIM_SET_COUNTER(&htim3, 0);
    htim3.Instance->EGR = TIM_EGR_UG;

    /* 上电安全滑行：双输入低 */
    Bsp_Motor_StopAll();
}

static uint32_t Motor_DutyOf(Bsp_Motor_Speed_t speed)
{
    static const uint32_t duty_tbl[4] = {
        0,              /* 占位，speed 从 1 开始 */
        MOTOR_DUTY_40,  /* MOTOR_SPEED_LOW  = 1 */
        MOTOR_DUTY_70,  /* MOTOR_SPEED_MID  = 2 */
        MOTOR_DUTY_100, /* MOTOR_SPEED_HIGH = 3 */
    };
    uint8_t s = (uint8_t)speed;
    if (s < 1U) s = 1U;
    if (s > 3U) s = 3U;
    return duty_tbl[s];
}

static void Motor_Write(uint32_t ch_a, uint32_t ch_b, uint32_t duty_a, uint32_t duty_b)
{
    // pi-lens-ignore: no-reserved-identifiers -- 普冉 HAL 供应商宏，不可重命名
    __HAL_TIM_SET_COMPARE(&htim3, ch_a, duty_a);
    // pi-lens-ignore: no-reserved-identifiers
    __HAL_TIM_SET_COMPARE(&htim3, ch_b, duty_b);
}

static void Motor_Channels(Bsp_Motor_Id_t id, uint32_t *ch_a, uint32_t *ch_b)
{
    if (id == MOTOR_LEFT) { *ch_a = TIM_CHANNEL_1; *ch_b = TIM_CHANNEL_2; }
    else                  { *ch_a = TIM_CHANNEL_3; *ch_b = TIM_CHANNEL_4; }
}

void Bsp_Motor_Set(Bsp_Motor_Id_t id, Bsp_Motor_Dir_t dir, Bsp_Motor_Speed_t speed)
{
    if (id > MOTOR_RIGHT) return;

    uint32_t ch_a, ch_b, duty = Motor_DutyOf(speed);
    Motor_Channels(id, &ch_a, &ch_b);

    switch (dir) {
    case MOTOR_DIR_FORWARD:
        Motor_Write(ch_a, ch_b, duty, 0);
        break;
    case MOTOR_DIR_BACKWARD:
        Motor_Write(ch_a, ch_b, 0, duty);
        break;
    case MOTOR_DIR_STOP:
    default:
        Motor_Write(ch_a, ch_b, 0, 0);
        dir = MOTOR_DIR_STOP;
        break;
    }

    g_state[id].dir = dir;
    if (dir != MOTOR_DIR_STOP) {
        g_state[id].braked = 0;
        g_state[id].brake_done = 0;   /* 再次驱动后允许重新刹停 */
    } else {
        g_state[id].braked = 0;
        g_state[id].brake_done = 1;   /* 显式 STOP = 滑行，无需再补刹 */
    }
}

void Bsp_Motor_StopAll(void)
{
    Bsp_Motor_Set(MOTOR_LEFT,  MOTOR_DIR_STOP, MOTOR_SPEED_HIGH);
    Bsp_Motor_Set(MOTOR_RIGHT, MOTOR_DIR_STOP, MOTOR_SPEED_HIGH);
}

void Bsp_Motor_Brake(Bsp_Motor_Id_t id)
{
    if (id > MOTOR_RIGHT) return;
    if (g_state[id].braked || g_state[id].brake_done) return;   /* 本次停止已刹过 */

    uint32_t ch_a, ch_b;
    Motor_Channels(id, &ch_a, &ch_b);
    Motor_Write(ch_a, ch_b, MOTOR_DUTY_100, MOTOR_DUTY_100);    /* 双输入高 = 刹车 */

    g_state[id].dir = MOTOR_DIR_STOP;
    g_state[id].braked = 1;
    g_state[id].brake_start = Bsp_Tick_GetMs();
}

void Bsp_Motor_BrakeAll(void)
{
    Bsp_Motor_Brake(MOTOR_LEFT);
    Bsp_Motor_Brake(MOTOR_RIGHT);
}

void Bsp_Motor_Update(void)
{
    uint32_t now = Bsp_Tick_GetMs();

    for (uint8_t i = 0; i < 2U; i++) {
        if (!g_state[i].braked) continue;
        if ((now - g_state[i].brake_start) < MOTOR_BRAKE_MS) continue;

        uint32_t ch_a, ch_b;
        Motor_Channels((Bsp_Motor_Id_t)i, &ch_a, &ch_b);
        Motor_Write(ch_a, ch_b, 0, 0);   /* 释放为滑行 */

        g_state[i].braked = 0;
        g_state[i].brake_done = 1;
    }
}
