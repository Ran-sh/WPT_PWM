/**
 ******************************************************************************
 * @brief   TFT 调试 — 像素逐点横线测试
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "Sys_Timer.h"
#include "Tft_Driver.h"

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    Tft_Driver_Init();
    Sys_Timer_Init();

    Tft_Driver_Clear(TFT_COLOR_BLACK);
    Tft_Driver_Show_String(0, 0, "ABCDEFGHIJ", TFT_COLOR_WHITE, TFT_COLOR_BLACK);
    Tft_Driver_Show_String(1, 0, "KLMNOPQRST", TFT_COLOR_WHITE, TFT_COLOR_BLACK);
    Tft_Driver_Show_String(2, 0, "UVWXYZ0123", TFT_COLOR_GREEN, TFT_COLOR_BLACK);
    Tft_Driver_Show_String(3, 0, "456789!@#$", TFT_COLOR_YELLOW, TFT_COLOR_BLACK);
    Tft_Driver_Set_Backlight(255);

    while (1) __WFI();
}
