#ifndef ESP8266_DRIVER_H
#define ESP8266_DRIVER_H

#include "stm32f10x.h"

typedef enum {
    ESP8266_DRIVER_TX_OK = 0,
    ESP8266_DRIVER_TX_FULL,
    ESP8266_DRIVER_TX_INVALID
} Esp8266_Driver_Tx_Result;

/** @brief 启动ESP8266非阻塞硬件初始化序列 */
void        Esp8266_Driver_Start_Init(void);
/** @brief 周期驱动 ESP8266 初始化状态机 (主循环调用) */
void        Esp8266_Driver_Init_Task(void);
/** @brief 在临界区内将以零结尾的文本完整加入USART2发送环形缓冲区
 *  @param str 待发送文本；空指针或空字符串视为无效输入
 *  @retval ESP8266_DRIVER_TX_OK 字符串已原子入队
 *  @retval ESP8266_DRIVER_TX_FULL 发送队列剩余空间不足
 *  @retval ESP8266_DRIVER_TX_INVALID 输入参数无效
 */
Esp8266_Driver_Tx_Result Esp8266_Driver_Send_String(const char* str);
/** @brief 获取发送队列累计满次数，供诊断使用 */
uint32_t    Esp8266_Driver_Get_Tx_Full_Count(void);
/** @brief 在同一临界区内检查、复制并消费一帧接收数据
 *  @param dst     目标缓冲区
 *  @param max_len 最大复制长度 (含 \0)
 *  @retval 实际复制字节数，不含字符串结束符；0表示无可用帧
 */
uint16_t    Esp8266_Driver_Try_Copy_Rx_Frame(char* dst, uint16_t max_len);
/** @brief 查询硬件初始化是否完成 */
uint8_t     Esp8266_Driver_Is_Ready(void);

/* 串口字符接收入口，仅供USART2中断处理函数调用。 */
void Esp8266_Driver_Rx_Char(uint8_t ch);
/** @brief USART2发送寄存器空中断入口，每次发送一个字节 */
void Esp8266_Driver_Tx_Ready_ISR(void);

#endif /* ESP8266串口驱动接口结束 */
