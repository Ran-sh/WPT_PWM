#ifndef __STM32F10x_CONF_H
#define __STM32F10x_CONF_H

/* 标准外设库头文件。 */
/* 取消/注释下面某一行可启用/禁用对应外设的头文件包含 */
#include "stm32f10x_adc.h"
#include "stm32f10x_bkp.h"
#include "stm32f10x_can.h"
#include "stm32f10x_cec.h"
#include "stm32f10x_crc.h"
#include "stm32f10x_dac.h"
#include "stm32f10x_dbgmcu.h"
#include "stm32f10x_dma.h"
#include "stm32f10x_exti.h"
#include "stm32f10x_flash.h"
#include "stm32f10x_fsmc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_i2c.h"
#include "stm32f10x_iwdg.h"
#include "stm32f10x_pwr.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_rtc.h"
#include "stm32f10x_sdio.h"
#include "stm32f10x_spi.h"
#include "stm32f10x_tim.h"
#include "stm32f10x_usart.h"
#include "stm32f10x_wwdg.h"
#include "misc.h" /* 嵌套向量中断控制器和系统滴答的辅助接口。 */

/* 取消下一行注释可启用标准外设库参数断言。 */
/* #define USE_FULL_ASSERT    1 */

/* 参数断言宏。 */
#ifdef  USE_FULL_ASSERT

/**
  * @brief  检查标准外设库函数的输入参数
  * @param  expr 待检查表达式；为假时调用assert_failed报告文件名和行号
  */
  #define assert_param(expr) ((expr) ? (void)0 : assert_failed((uint8_t *)__FILE__, __LINE__))
/* 参数断言失败回调。 */
  void assert_failed(uint8_t* file, uint32_t line);
#else
  #define assert_param(expr) ((void)0)
#endif /* 完整参数断言配置结束 */

#endif /* 标准外设库配置结束 */
