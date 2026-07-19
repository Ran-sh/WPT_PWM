/**
 ******************************************************************************
 * @file    Hardware/Esp8266_Driver.h
 * @brief   ESP8266 串口通信驱动 — V5.0.1
 * @note    Dual-MCU 架构: USART2 115200, 纯 JSON 透传
 *          CH_PD=PB11 (EN), RST=PA1
 ******************************************************************************
 */

#ifndef ESP8266_DRIVER_H
#define ESP8266_DRIVER_H

#include "stm32f10x.h"

typedef enum {
    ESP8266_DRIVER_TX_OK = 0,
    ESP8266_DRIVER_TX_FULL,
    ESP8266_DRIVER_TX_INVALID
} Esp8266_Driver_Tx_Result;

/** @brief 启动 ESP8266 硬件初始化序列 (非阻塞, 复位脉冲+等待启动) */
void        Esp8266_Driver_Start_Init(void);
/** @brief 周期驱动 ESP8266 初始化状态机 (主循环调用) */
void        Esp8266_Driver_Init_Task(void);
/** @brief 将字符串原子加入 USART2 中断发送队列
 *  @param str 以 \0 结尾的字符串
 *  @return OK=已完整入队, FULL=空间不足, INVALID=参数无效
 */
Esp8266_Driver_Tx_Result Esp8266_Driver_Send_String(const char* str);
/** @brief 获取发送队列累计满次数，供诊断使用 */
uint32_t    Esp8266_Driver_Get_Tx_Full_Count(void);
/** @brief 获取接收帧完成标志 (非阻塞)
 *  @return 1=有新帧, 0=无
 */
uint8_t     Esp8266_Driver_Get_Rx_Flag(void);
/** @brief 获取原始接收缓冲区只读指针 (配合 Get_Rx_Flag 使用) */
const char* Esp8266_Driver_Get_Rx_Buffer(void);
/** @brief 清空接收缓冲 (临界区保护) */
void        Esp8266_Driver_Clear_Rx_Buffer(void);
/** @brief 原子复制接收帧到用户缓冲区 (临界区保护)
 *  @param dst     目标缓冲区
 *  @param max_len 最大复制长度 (含 \0)
 *  @return 实际复制字节数 (不含 \0)
 */
uint16_t    Esp8266_Driver_Copy_Rx_Frame(char* dst, uint16_t max_len);
/** @brief 原子检查并复制接收帧 (check-flag+copy+clear 在同一临界区内, 消除 TOCTOU)
 *  @param dst     目标缓冲区
 *  @param max_len 最大复制长度 (含 \0)
 *  @return 实际复制字节数 (不含 \0), 0 表示无可用帧
 */
uint16_t    Esp8266_Driver_Try_Copy_Rx_Frame(char* dst, uint16_t max_len);
/** @brief 查询硬件初始化是否完成 */
uint8_t     Esp8266_Driver_Is_Ready(void);

/* 串口字符接收 (仅供 USART2_IRQHandler 调用, 不对外) */
void Esp8266_Driver_Rx_Char(uint8_t ch);
/** @brief USART2 TXE 中断单字节发送入口 */
void Esp8266_Driver_Tx_Ready_ISR(void);

#endif /* ESP8266_DRIVER_H */
