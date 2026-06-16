/**
 ******************************************************************************
 * @file    User/App_Network.c
 * @brief   网络应用层 — 实现
 * @note    V9: 显式 App_Network_Conn_State 枚举替代隐式 bool 组合
 ******************************************************************************
 */

#include "App_Network.h"
#include "Sys_Core.h"  /* V14: 远程指令需同步 g_sys_state */
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
static uint32_t               s_last_esp_ms   = 0;     /* 最后一次收到 ESP 有效帧的时间戳, 心跳超时用 */

uint8_t App_Network_Start_Connect(void)
{
    s_conn_state    = APP_NETWORK_CONN_WIFI;
    s_retry_count   = 0;
    s_connect_start = Sys_Timer_Get_Tick();
    s_last_esp_ms   = Sys_Timer_Get_Tick();  /* 刚启动, 给予初始超时窗口 */
    Esp8266_Driver_Start_Init();
    Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
    return 0;
}

uint8_t App_Network_Soft_Reset(void)
{
    /* 仅重置网络状态为 IDLE, 不重启硬件 — 用于进入无WIFI模式 */
    s_conn_state    = APP_NETWORK_CONN_IDLE;
    s_retry_count   = 0;
    s_rssi          = -100;
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
    return (s_conn_state == APP_NETWORK_CONN_ONLINE);
}

/* ── 指数退避: 前两级 ≥5s (覆盖 ESP 4s 启动), 不永久 FAILED ── */
static uint32_t App_Network_Get_Retry_Timeout(void)
{
    if (s_retry_count < 3)  return 5000;     /* 0-2:    5s (必须 >4s ESP 启动) */
    if (s_retry_count < 8)  return 15000;    /* 3-7:   15s */
    if (s_retry_count < 14) return 30000;    /* 8-13:  30s */
    if (s_retry_count < 22) return 60000;    /* 14-21: 60s */
    if (s_retry_count < 32) return 120000;   /* 22-31: 2min */
    if (s_retry_count < 47) return 300000;   /* 32-46: 5min */
    return 1800000;                           /* 47+:   30min */
}

/** @brief 判断设备是否热点打开可配网 (RSSI 极强, 通常在 -30 以内) */
static uint8_t App_Network_Is_Hotspot_Nearby(void)
{
    return (s_rssi >= -35);
}

static void App_Network_Check_Retry(void)
{
    if (s_conn_state != APP_NETWORK_CONN_WIFI && s_conn_state != APP_NETWORK_CONN_MQTT)
        return;

    /* 侦测到强信号热点 → 加速重试 (直接重置为 3s 级) */
    if (App_Network_Is_Hotspot_Nearby()) {
        s_retry_count   = 0;
        s_connect_start = Sys_Timer_Get_Tick();
    }

    /* 指数退避超时, 自动重试, 无限循环 */
    if (Sys_Timer_Get_Tick() - s_connect_start >= App_Network_Get_Retry_Timeout()) {
        s_retry_count++;
        s_connect_start = Sys_Timer_Get_Tick();
        s_last_esp_ms   = Sys_Timer_Get_Tick();  /* 重置心跳, 给新 init 完整超时窗口 */
        Esp8266_Driver_Start_Init();
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
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

    App_Network_Check_Retry();

    /* ── 心跳超时检测: 8s 无任何 ESP 帧 → 判定离线, 强制重连 ── */
    if (s_conn_state != APP_NETWORK_CONN_IDLE
        && Sys_Timer_Get_Tick() - s_last_esp_ms >= APP_NETWORK_CONNECT_TIMEOUT_MS) {
        s_conn_state    = APP_NETWORK_CONN_WIFI;
        s_retry_count   = 0;
        s_connect_start = Sys_Timer_Get_Tick();
        s_last_esp_ms   = Sys_Timer_Get_Tick();  /* 给予新 init 完整超时窗口 */
        s_rssi          = -100;
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
        Esp8266_Driver_Start_Init();
    }

    /* ── 指令接收 ── */
    {
        char local_buf[128];
        const char* p;
        Inverter_Control_Soft_Start_State ss_cmd;  /* 帧内快照: 防 ELSE-IF 链间 TOCTOU */
        uint8_t conn_cs;

        if (!Esp8266_Driver_Try_Copy_Rx_Frame(local_buf, sizeof(local_buf)))
            goto skip_frame;

        s_last_esp_ms = Sys_Timer_Get_Tick();  /* 任何有效帧都刷新心跳时间戳 */

        ss_cmd  = Inverter_Control_Soft_Start_Get_State();
        conn_cs = s_conn_state;

        if (strstr(local_buf, "STATUS:DISCONNECTED")) {
            /* 断连 → 回到 WIFI 等待, 重置重试参数 */
            s_conn_state    = APP_NETWORK_CONN_WIFI;
            s_retry_count   = 0;
            s_connect_start = Sys_Timer_Get_Tick();
            s_rssi          = -100;
            Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
        }
        else if (strstr(local_buf, "STATUS:MQTT")) {
            /* MQTT 中间状态: 保留 WIFI_CONN→MQTT_CONN 转移, 但不从 ONLINE 降级 */
            if (conn_cs == APP_NETWORK_CONN_WIFI) {
                s_conn_state    = APP_NETWORK_CONN_MQTT;
                s_connect_start = Sys_Timer_Get_Tick();  /* 进入 MQTT 时给新超时窗口, 仅一次 */
                Led_Driver_Set_WiFi(LED_DRIVER_STATE_FAST);
            }
        }
        else if (strstr(local_buf, "STATUS:ONLINE")) {
            /* ONLINE: 任何非 ONLINE 状态→ONLINE, 包括从 FAILED 恢复 */
            const char* r = strstr(local_buf, "RSSI=");
            if (r) s_rssi = (int8_t)strtol(r + 5, NULL, 10);
            if (conn_cs != APP_NETWORK_CONN_ONLINE) {
                s_conn_state = APP_NETWORK_CONN_ONLINE;
                s_retry_count = 0;
                Led_Driver_Set_WiFi(LED_DRIVER_STATE_ON);
            }
        }
        else if (strstr(local_buf, "STATUS:RSSI=")) {
            /* 独立 RSSI 心跳帧: 仅更新信号强度, 不触发状态转移 (在线状态下 ESP 每 2s 发送) */
            const char* r = strstr(local_buf, "RSSI=");
            if (r) s_rssi = (int8_t)strtol(r + 5, NULL, 10);
        }
        else if ((p = strstr(local_buf, "CMD:OFF")) != 0  && (p[7] == '\0' || p[7] == '\r' || p[7] == '\n')) {
            if (!Ui_Controller_Is_No_WiFi_Mode()) {
                if (ss_cmd == INVERTER_CONTROL_SS_STATE_SWEEP || ss_cmd == INVERTER_CONTROL_SS_STATE_DONE) {
                    Inverter_Control_Soft_Start_Stop();
                    g_sys_state = SYS_STATE_IDLE;  /* V14 状态机同步: 远程关断必须重置全局状态 */
                    Ui_Controller_Force_Page(UI_PAGE_MAIN_MENU);  /* 多端同步: 远程关断后回到主菜单 */
                }
            }
        }
        else if ((p = strstr(local_buf, "CMD:ON")) != 0   && (p[6] == '\0' || p[6] == '\r' || p[6] == '\n')) {
            if (!Ui_Controller_Is_No_WiFi_Mode()) {
                if (ss_cmd == INVERTER_CONTROL_SS_STATE_IDLE) {
                    Inverter_Control_Soft_Start_Trigger();
                    g_sys_state = SYS_STATE_SWEEP;  /* V14 状态机同步: 远程开机必须告知主循环 */
                    Ui_Controller_Force_Page(UI_PAGE_SWEEP);  /* 防 UI 滞留菜单页导致遥测误门控 */
                }
            }
        }
        else if ((p = strstr(local_buf, "CMD:SETFREQ:")) != 0) {
            if (!Ui_Controller_Is_No_WiFi_Mode()) {
                if (ss_cmd == INVERTER_CONTROL_SS_STATE_DONE) {
                    const char* f_str = p + 12;
                    if (*f_str >= '0' && *f_str <= '9') {
                        long f = atol(f_str);
                        if (f >= PWM_DRIVER_FREQ_MIN_HZ && f <= PWM_DRIVER_FREQ_MAX_HZ)
                            Inverter_Control_Freq_Ramp_Trigger((uint32_t)f);
                    }
                }
            }
        }
    }
    skip_frame:

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
                int written;
                /* 始终上报真实 V/I: 物理量在任何状态下均可采集, 仅 F 在 PWM 未运行时报 0 */
                written = snprintf(json_buf, sizeof(json_buf),
                         "{\"V\":%.2f,\"I\":%.3f,\"F\":%lu,\"S\":%d}\n",
                         (double)Sys_Safety_Get_EMA_Voltage(),
                         (double)Sys_Safety_Get_EMA_Current(),
                         (ss == INVERTER_CONTROL_SS_STATE_DONE)
                            ? (unsigned long)Pwm_Driver_Get_Frequency()
                            : 0UL,
                         (int)ss);
                if (written > 0 && (uint16_t)written < sizeof(json_buf))
                    Esp8266_Driver_Send_String(json_buf);
            }
        }
    }
}

int8_t App_Network_Get_RSSI(void) { return s_rssi; }
