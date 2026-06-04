/**
 ******************************************************************************
 * @file    User/App_Network.c
 * @brief   网络应用层 — 实现
 * @note    V6.0: 显式 App_Network_Conn_State 枚举替代隐式 bool 组合
 ******************************************************************************
 */

#include "App_Network.h"
#include "Esp8266_Driver.h"
#include "Adc_Driver.h"
#include "Pwm_Driver.h"
#include "Inverter_Control.h"
#include "Ui_Controller.h"
#include "Led_Driver.h"
#include "Sys_Timer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define APP_NETWORK_CONNECT_TIMEOUT_MS  15000
#define APP_NETWORK_MAX_RETRIES           3
#define APP_NETWORK_TELEMETRY_PERIOD_MS  500

#define PROTO_STATUS_ONLINE   "STATUS:ONLINE"
#define PROTO_CMD_ON          "CMD:ON"
#define PROTO_CMD_OFF         "CMD:OFF"
#define PROTO_CMD_SETFREQ     "CMD:SETFREQ:"
#define PROTO_CMD_SETFREQ_LEN (sizeof(PROTO_CMD_SETFREQ) - 1)

static App_Network_Conn_State s_conn_state    = APP_NETWORK_CONN_IDLE;
static uint8_t                s_retry_count   = 0;
static uint32_t               s_connect_start = 0;

/* Reset_Connect_State — 消除 3 处连接重置样板代码重复 */
static void Reset_Connect_State(void)
{
    s_conn_state    = APP_NETWORK_CONN_WIFI;
    s_retry_count   = 0;
    s_connect_start = Sys_Timer_Get_Tick();
    Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
}

uint8_t App_Network_Start_Connect(void)
{
    Reset_Connect_State();
    Esp8266_Driver_Start_Init();
    return 0;
}

uint8_t App_Network_Soft_Reset(void)
{
    Reset_Connect_State();
    return 0;
}

uint8_t App_Network_Get_Connect_Status(void)
{
    return (uint8_t)s_conn_state;
}

uint8_t App_Network_Get_Retry_Count(void)  { return s_retry_count; }
uint8_t App_Network_Get_Max_Retries(void)  { return APP_NETWORK_MAX_RETRIES; }

uint8_t App_Network_Is_Connected(void)
{
    return Esp8266_Driver_Is_Ready() && (s_conn_state == APP_NETWORK_CONN_ONLINE);
}

/* ── 内部: 重试超时检查 ── */
static void Check_Retry(void)
{
    if (s_conn_state != APP_NETWORK_CONN_WIFI) return;
    if (s_retry_count >= APP_NETWORK_MAX_RETRIES) {
        s_conn_state = APP_NETWORK_CONN_FAILED;
        return;
    }

    if (Sys_Timer_Get_Tick() - s_connect_start >= APP_NETWORK_CONNECT_TIMEOUT_MS) {
        s_retry_count++;
        if (s_retry_count < APP_NETWORK_MAX_RETRIES) {
            Esp8266_Driver_Start_Init();
            Reset_Connect_State();
        }
    }
}

void App_Network_Task(void)
{
    /* 驱动 ESP8266 硬件初始化状态机 (非阻塞) */
    Esp8266_Driver_Init_Task();

    if (!Esp8266_Driver_Is_Ready()) return;

    Check_Retry();

    /* ── 指令接收 ── */
    if (Esp8266_Driver_Get_Rx_Flag()) {
        char local_buf[64];
        Esp8266_Driver_Copy_Rx_Frame(local_buf, sizeof(local_buf));

        if (strstr(local_buf, PROTO_STATUS_ONLINE)) {
            s_conn_state = APP_NETWORK_CONN_ONLINE;
            Led_Driver_Set_WiFi(LED_DRIVER_STATE_ON);
        }
        else if (strstr(local_buf, PROTO_CMD_ON)) {
            if (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_IDLE)
                Inverter_Control_Soft_Start_Trigger();
        }
        else if (strstr(local_buf, PROTO_CMD_OFF)) {
            Inverter_Control_Soft_Start_Stop();
        }
        else if (strstr(local_buf, PROTO_CMD_SETFREQ)) {
            if (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE) {
                long f = atol(local_buf + PROTO_CMD_SETFREQ_LEN);
                if (f >= PWM_DRIVER_FREQ_MIN_HZ && f <= PWM_DRIVER_FREQ_MAX_HZ)
                    Inverter_Control_Freq_Ramp_Trigger((uint32_t)f);
            }
        }
    }

    /* ── 遥测发送 (门控: 仅 UI >= READY 且系统在线) ── */
    {
        static uint32_t last_telemetry = 0;

        if (Sys_Timer_Get_Tick() - last_telemetry >= APP_NETWORK_TELEMETRY_PERIOD_MS) {
            last_telemetry = Sys_Timer_Get_Tick();

            uint8_t allow_telemetry = 1;

            if (s_conn_state != APP_NETWORK_CONN_ONLINE)     allow_telemetry = 0;
            if (Ui_Controller_Get_State() < UI_CONTROLLER_STATE_READY) allow_telemetry = 0;

            Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
            if (ss == INVERTER_CONTROL_SS_STATE_SWEEP)       allow_telemetry = 0;

            if (allow_telemetry) {
                char json_buf[80];
                if (ss == INVERTER_CONTROL_SS_STATE_DONE) {
                    snprintf(json_buf, sizeof(json_buf),
                             "{\"V\":%.2f,\"I\":%.2f,\"F\":%lu,\"S\":%d}\n",
                             Adc_Driver_Get_Voltage(),
                             Adc_Driver_Get_Current(),
                             (unsigned long)Pwm_Driver_Get_Frequency(),
                             (int)ss);
                } else {
                    snprintf(json_buf, sizeof(json_buf),
                             "{\"V\":0.00,\"I\":0.00,\"F\":%lu,\"S\":%d}\n",
                             (unsigned long)Pwm_Driver_Get_Frequency(),
                             (int)ss);
                }
                Esp8266_Driver_Send_String(json_buf);
            }
        }
    }
}
