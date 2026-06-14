/**
 ******************************************************************************
 * @file    User/App_Network.c
 * @brief   网络应用层 — 实现
 * @note    V9: 显式 App_Network_Conn_State 枚举替代隐式 bool 组合
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

#define APP_NETWORK_CONNECT_TIMEOUT_MS   8000
#define APP_NETWORK_MAX_RETRIES           3
#define APP_NETWORK_TELEMETRY_PERIOD_MS  500

static App_Network_Conn_State s_conn_state    = APP_NETWORK_CONN_IDLE;
static uint8_t                s_retry_count   = 0;
static uint32_t               s_connect_start = 0;
static int8_t                 s_rssi          = -100;  /* WIFI 信号强度 dBm, 默认-100 */

uint8_t App_Network_Start_Connect(void)
{
    s_conn_state    = APP_NETWORK_CONN_WIFI;
    s_retry_count   = 0;
    s_connect_start = Sys_Timer_Get_Tick();
    Esp8266_Driver_Start_Init();
    Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
    return 0;
}

uint8_t App_Network_Soft_Reset(void)
{
    /* 仅重置网络状态为 IDLE, 不重启硬件 — 用于进入无WIFI模式 */
    s_conn_state    = APP_NETWORK_CONN_IDLE;
    s_retry_count   = 0;
    Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
    return 0;
}

uint8_t App_Network_Get_Connect_Status(void)
{
    return (uint8_t)s_conn_state;
}

uint8_t App_Network_Get_Retry_Count(void)  { return s_retry_count; }

uint8_t App_Network_Is_Connected(void)
{
    return Esp8266_Driver_Is_Ready() && (s_conn_state == APP_NETWORK_CONN_ONLINE);
}

/* ── 重试超时检查: 等待 CONNECT_TIMEOUT_MS(8s) 内收到 STATUS:ONLINE, 超时→重新 Start_Init → 最多 MAX_RETRIES(3) 次 → FAILED ── */
static void Check_Retry(void)
{
    if (s_conn_state != APP_NETWORK_CONN_WIFI && s_conn_state != APP_NETWORK_CONN_MQTT) return;
    if (s_retry_count >= APP_NETWORK_MAX_RETRIES) {
        s_conn_state = APP_NETWORK_CONN_FAILED;
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
        return;
    }

    if (Sys_Timer_Get_Tick() - s_connect_start >= APP_NETWORK_CONNECT_TIMEOUT_MS) {
        s_retry_count++;
        if (s_retry_count < APP_NETWORK_MAX_RETRIES) {
            s_connect_start = Sys_Timer_Get_Tick();
            Esp8266_Driver_Start_Init();
            Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
        } else {
            s_conn_state = APP_NETWORK_CONN_FAILED;
            Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
        }
    }
}

uint8_t App_Network_Is_Connecting(void)
{
    return (s_conn_state == APP_NETWORK_CONN_WIFI || s_conn_state == APP_NETWORK_CONN_MQTT);
}

void App_Network_Task(void)
{
    /* 驱动 ESP8266 硬件初始化状态机 (非阻塞) */
    Esp8266_Driver_Init_Task();

    if (!Esp8266_Driver_Is_Ready()) return;

    Check_Retry();

    /* ── 指令接收 ── */
    if (Esp8266_Driver_Get_Rx_Flag()) {
        char local_buf[128];
        const char* p;
        Esp8266_Driver_Copy_Rx_Frame(local_buf, sizeof(local_buf));

        if (strstr(local_buf, "STATUS:DISCONNECTED")) {
            /* 断连 → 重置并回到 WIFI 连接状态, 清 RSSI */
            s_conn_state    = APP_NETWORK_CONN_WIFI;
            s_retry_count   = 0;
            s_connect_start = Sys_Timer_Get_Tick();
            s_rssi          = -100;
            Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
        }
        else if (strstr(local_buf, "STATUS:MQTT")) {
            if (s_conn_state != APP_NETWORK_CONN_ONLINE) {
                s_conn_state = APP_NETWORK_CONN_MQTT;
                Led_Driver_Set_WiFi(LED_DRIVER_STATE_FAST);
            }
        }
        else if (strstr(local_buf, "STATUS:ONLINE")) {
            /* STATUS:ONLINE 可带 RSSI 参数 "STATUS:ONLINE:RSSI=-45" */
            const char* r = strstr(local_buf, "RSSI=");
            if (r) s_rssi = (int8_t)strtol(r + 5, NULL, 10);
            if (s_conn_state != APP_NETWORK_CONN_ONLINE) {
                s_conn_state = APP_NETWORK_CONN_ONLINE;
                Led_Driver_Set_WiFi(LED_DRIVER_STATE_ON);
            }
        }
        else if ((p = strstr(local_buf, "CMD:OFF")) != 0  && (p[7] == '\0' || p[7] == '\r' || p[7] == '\n')  /* 精确匹配: 确保 "CMD:OFF" 后跟结束符, 非更长字符串如 "CMD:OFFSET" */) {
            if (!Ui_Controller_Is_No_WiFi_Mode()) {
                Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
                if (ss == INVERTER_CONTROL_SS_STATE_SWEEP || ss == INVERTER_CONTROL_SS_STATE_DONE)
                    Inverter_Control_Soft_Start_Stop();
            }
        }
        else if ((p = strstr(local_buf, "CMD:ON")) != 0   && (p[6] == '\0' || p[6] == '\r' || p[6] == '\n')) {
            if (!Ui_Controller_Is_No_WiFi_Mode()) {
                if (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_IDLE)
                    Inverter_Control_Soft_Start_Trigger();
            }
        }
        else if ((p = strstr(local_buf, "CMD:SETFREQ:")) != 0) {
            if (!Ui_Controller_Is_No_WiFi_Mode()) {
                if (Inverter_Control_Soft_Start_Get_State() == INVERTER_CONTROL_SS_STATE_DONE) {
                    long f = atol(p + 12);
                    if (f >= PWM_DRIVER_FREQ_MIN_HZ && f <= PWM_DRIVER_FREQ_MAX_HZ)
                        Inverter_Control_Freq_Ramp_Trigger((uint32_t)f);
                }
            }
        }
    }

    /* ── 遥测发送 (门控: 仅 UI >= READY 且系统在线) ── */
    {
        static uint32_t last_telemetry = 0;

        if (Sys_Timer_Get_Tick() - last_telemetry >= APP_NETWORK_TELEMETRY_PERIOD_MS) {
            last_telemetry = Sys_Timer_Get_Tick();

            uint8_t allow_telemetry = 1;

            if (s_conn_state != APP_NETWORK_CONN_ONLINE) allow_telemetry = 0;
            {
                Ui_Page page = Ui_Controller_Get_Page();
                if (page == UI_PAGE_MAIN_MENU || page == UI_PAGE_MONITOR_SUB_MENU
                    || page == UI_PAGE_WIFI_SETUP || page == UI_PAGE_SWEEP
                    || page == UI_PAGE_FAULT)
                    allow_telemetry = 0;
            }

            Inverter_Control_Soft_Start_State ss = Inverter_Control_Soft_Start_Get_State();
            if (ss == INVERTER_CONTROL_SS_STATE_SWEEP)       allow_telemetry = 0;

            if (allow_telemetry) {
                char json_buf[80];
                if (ss == INVERTER_CONTROL_SS_STATE_DONE) {
                    snprintf(json_buf, sizeof(json_buf),
                             "{\"V\":%.2f,\"I\":%.3f,\"F\":%lu,\"S\":%d}\n",
                             Adc_Driver_Get_Voltage(),
                             Adc_Driver_Get_Current(),
                             (unsigned long)Pwm_Driver_Get_Frequency(),
                             (int)ss);
                } else {
                    snprintf(json_buf, sizeof(json_buf),
                             "{\"V\":0.00,\"I\":0.000,\"F\":%lu,\"S\":%d}\n",
                             (unsigned long)Pwm_Driver_Get_Frequency(),
                             (int)ss);
                }
                Esp8266_Driver_Send_String(json_buf);
            }
        }
    }
}

int8_t App_Network_Get_RSSI(void) { return s_rssi; }
