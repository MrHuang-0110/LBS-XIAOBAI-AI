#include "main.h"
#include "Bsp.h"
#include "App_System.h"
#include "App_Main.h"

int main(void)
{
    HAL_Init();
    App_System_Init();
    BSP_Init();
    App_Init();
    App_Loop();   /* 不返回 */
}
