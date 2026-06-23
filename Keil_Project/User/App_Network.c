/**
 ******************************************************************************
 * @file    User/App_Network.c
 * @brief   网络应用层 — 实现 (V4.3.0: 黑匣子日志 + Flash 配置集成)
 * @note    V4.3.0: 离线时写入黑匣子 (循环日志不丢失)
 *          V4.2.1: OFFLINE 双模式 + 5次有限重试
 ******************************************************************************
 */

#include "App_Network.h"
#include "Sys_Core.h"
#include "Esp8266_Driver.h"
#include "Adc_Driver.h"
#include "Pwm_Driver.h"
#include "Inverter_Control.h"
#include "Ui_Controller.h"
#include "Led_Driver.h"
#include "Sys_Timer.h"
#include "App_Storage.h"       /* V4.3.0: 黑匣子日志 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define APP_NETWORK_CONNECT_TIMEOUT_MS   8000
#define APP_NETWORK_MAX_RETRIES            5   /* 被动断开重试上限, 耗尽进 OFFLINE */
#define APP_NETWORK_TELEMETRY_PERIOD_MS  500

static App_Network_Conn_State s_conn_state    = APP_NETWORK_CONN_IDLE;
static uint8_t                s_retry_count   = 0;
static uint32_t               s_connect_start = 0;
static int8_t                 s_rssi          = -100;
static uint32_t               s_last_esp_ms   = 0;

uint8_t App_Network_Start_Connect(void)
{
    s_conn_state    = APP_NETWORK_CONN_WIFI;
    s_retry_count   = 0;
    s_connect_start = Sys_Timer_Get_Tick();
    s_last_esp_ms   = Sys_Timer_Get_Tick();
    Esp8266_Driver_Start_Init();
    Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
    return 0;
}

uint8_t App_Network_Soft_Reset(void)
{
    s_conn_state    = APP_NETWORK_CONN_IDLE;
    s_retry_count   = 0;
    s_rssi          = -100;
    Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
    return 0;
}

void App_Network_Manual_Connect(void)
{
    /* 仅从主动离线恢复: 清除离线标记, 开始连接 */
    if (s_conn_state == APP_NETWORK_CONN_OFFLINE_ACTIVE) {
        s_conn_state    = APP_NETWORK_CONN_WIFI;
        s_retry_count   = 0;
        s_connect_start = Sys_Timer_Get_Tick();
        s_last_esp_ms   = Sys_Timer_Get_Tick();
        Esp8266_Driver_Start_Init();
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
    }
}

void App_Network_Manual_Disconnect(void)
{
    /* 进入主动离线: STM32 侧 OFFLINE_ACTIVE (忽略所有帧, 需手动ON恢复)
     * ESP 侧收到 CMD:WIFI_DISC 后进入 OFFLINE_PASSIVE (持续嗅探 WiFi 恢复)
     * 两个 MCU 状态不同是设计意图: ESP 保持凭证并自动重连 WiFi 层,
     * 但 STM32 的 ACTIVE 门控阻止了应用层重连 — 直到用户手动 ON 才放行 */
    if (s_conn_state == APP_NETWORK_CONN_ONLINE
        || s_conn_state == APP_NETWORK_CONN_WIFI
        || s_conn_state == APP_NETWORK_CONN_MQTT) {
        Esp8266_Driver_Send_String("CMD:WIFI_DISC\n");
    }
    /* 无论当前状态如何, 强制进入主动离线 */
    s_conn_state    = APP_NETWORK_CONN_OFFLINE_ACTIVE;
    s_retry_count   = 0;
    s_rssi          = -100;
    Led_Driver_Set_WiFi(LED_DRIVER_STATE_OFF);
}

/** @brief 从被动离线恢复 — ESP 已在运行, 只切状态不发硬件 RST */
void App_Network_Resume_From_Offline(void)
{
    if (s_conn_state == APP_NETWORK_CONN_OFFLINE_PASSIVE) {
        s_conn_state    = APP_NETWORK_CONN_WIFI;
        s_retry_count   = 0;
        s_connect_start = Sys_Timer_Get_Tick();
        s_last_esp_ms   = Sys_Timer_Get_Tick();
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
    }
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

uint8_t App_Network_Is_Offline(void)
{
    return (s_conn_state == APP_NETWORK_CONN_OFFLINE_PASSIVE
         || s_conn_state == APP_NETWORK_CONN_OFFLINE_ACTIVE);
}

/* ── 指数退避 ── */
static uint32_t App_Network_Get_Retry_Timeout(void)
{
    if (s_retry_count < 3)  return 5000;
    if (s_retry_count < 6)  return 15000;
    if (s_retry_count < 10) return 30000;
    if (s_retry_count < 15) return 60000;
    if (s_retry_count < 25) return 120000;
    if (s_retry_count < 40) return 300000;
    return 1800000;
}

static void App_Network_Check_Retry(void)
{
    if (s_conn_state != APP_NETWORK_CONN_WIFI && s_conn_state != APP_NETWORK_CONN_MQTT)
        return;

    /* 指数退避超时 */
    if (Sys_Timer_Get_Tick() - s_connect_start >= App_Network_Get_Retry_Timeout()) {
        s_retry_count++;

        /* 重试达到上限 → 进入被动离线 (热点断开, 自动嗅探恢复) */
        if (s_retry_count > APP_NETWORK_MAX_RETRIES) {
            s_conn_state  = APP_NETWORK_CONN_OFFLINE_PASSIVE;
            s_retry_count = 0;
            Led_Driver_Set_WiFi(LED_DRIVER_STATE_OFF);
            return;
        }

        s_connect_start = Sys_Timer_Get_Tick();
        s_last_esp_ms   = Sys_Timer_Get_Tick();
        /* 不发硬件 RST: ESP 已在运行, STATUS:DISCONNECTED 后 ESP 会自动 WiFi.begin()
         * 发硬件 RST 反而浪费 4s BOOT_WAIT, 5次重试全耗在等待上 */
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
    }

    /* MQTT 状态额外超时保护: 超过 30s 无进展则回退 WIFI 重试
     * 防止 WiFi 已连但 MQTT broker 不可达时的死锁 */
    if (s_conn_state == APP_NETWORK_CONN_MQTT
        && Sys_Timer_Get_Tick() - s_last_esp_ms >= 30000) {
        s_conn_state    = APP_NETWORK_CONN_WIFI;
        s_retry_count   = 0;
        s_connect_start = Sys_Timer_Get_Tick();
        s_last_esp_ms   = Sys_Timer_Get_Tick();
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
    }
}

/* ── 被动离线嗅探: ESP 上报有效帧 → 热点恢复, 自动重连 ── */
static void App_Network_Check_Offline_Recovery(void)
{
    if (s_conn_state != APP_NETWORK_CONN_OFFLINE_PASSIVE)
        return;

    /* 被动监听: 串口收到任何有效 STATUS 帧表示 ESP 已恢复通信
     * 注意: 仅切换 STM32 状态为 WIFI, 不发硬件 RST — ESP 已存活且在重连中,
     * 发硬件 RST 反而会引入 4s BOOT_WAIT 延迟 */
    {
        char local_buf[512];
        if (!Esp8266_Driver_Try_Copy_Rx_Frame(local_buf, sizeof(local_buf)))
            return;

        /* 仅响应 STATUS 类帧 (排除遥控 CMD 帧) */
        if (strstr(local_buf, "STATUS:")) {
            s_conn_state    = APP_NETWORK_CONN_WIFI;
            s_retry_count   = 0;
            s_connect_start = Sys_Timer_Get_Tick();
            s_last_esp_ms   = Sys_Timer_Get_Tick();
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

    /* 被动离线嗅探: ESP 恢复 → 自动重连 (在 App_Network_Check_Retry 之前, 优先级最高) */
    App_Network_Check_Offline_Recovery();

    App_Network_Check_Retry();

    /* ── 心跳超时检测: 8s 无任何 ESP 帧 → 判定离线, 开始重试 ── */
    if (s_conn_state == APP_NETWORK_CONN_ONLINE
        && Sys_Timer_Get_Tick() - s_last_esp_ms >= APP_NETWORK_CONNECT_TIMEOUT_MS) {
        s_conn_state    = APP_NETWORK_CONN_WIFI;
        s_retry_count   = 0;
        s_connect_start = Sys_Timer_Get_Tick();
        s_last_esp_ms   = Sys_Timer_Get_Tick();
        s_rssi          = -100;
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
        /* 不发硬件 RST: ESP 仍在运行, 自动 WiFi.begin() 尝试重连 */
    }

    /* ── 指令接收 (原子闭环: Try_Copy 一次性完成判定+复制+清零, 防 ISR 抢断丢帧) ── */
    {
        char local_buf[512]; const char* p;
        Inverter_Control_Soft_Start_State ss_cmd; uint8_t conn_cs;

        if (!Esp8266_Driver_Try_Copy_Rx_Frame(local_buf, sizeof(local_buf)))
            goto skip_frame;                             /* 无完整帧 → 跳过 */

        s_last_esp_ms = Sys_Timer_Get_Tick();

        ss_cmd  = Inverter_Control_Soft_Start_Get_State(); conn_cs = s_conn_state; /* 帧内快照 */
        if (s_conn_state == APP_NETWORK_CONN_OFFLINE_PASSIVE
         || s_conn_state == APP_NETWORK_CONN_OFFLINE_ACTIVE)
            goto skip_frame;                             /* 离线态吞帧, 防 STATUS 回显转移状态 */

        if (strstr(local_buf, "STATUS:DISCONNECTED")) {
            s_conn_state    = APP_NETWORK_CONN_WIFI;
            s_retry_count   = 0;
            s_connect_start = Sys_Timer_Get_Tick();
            s_rssi          = -100;
            Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
        }
        else if (strstr(local_buf, "OTA:START")) {
            /* PC → Public MQTT → ESP → STM32: 启动 OTA 字库推送 */
            if (g_sys_state == SYS_STATE_IDLE) App_Storage_OTA_Begin();
        }
        else if (strstr(local_buf, "OTA:DONE")) {
            /* OTA:DONE → 完成校验, 标记 FONT_OK
               必须在 OTA:<seq>, 之前, 否则 strstr("OTA:") 会误匹配 */
            if (g_sys_state == SYS_STATE_IDLE) App_Storage_OTA_End();
        }
        else if (strstr(local_buf, "OTA:") == local_buf) {
            /* OTA:<seq>,<base64> 数据帧 → 写入 Flash (仅 IDLE 态有效)
               必须在 OTA:START/OTA:DONE 之后, 否则 strstr 会误匹配 */
            if (g_sys_state == SYS_STATE_IDLE) App_Storage_OTA_Handler(local_buf);
        }
        else if (strstr(local_buf, "STATUS:MQTT")) {
            if (conn_cs == APP_NETWORK_CONN_WIFI) {
                s_conn_state    = APP_NETWORK_CONN_MQTT;
                s_connect_start = Sys_Timer_Get_Tick();
                Led_Driver_Set_WiFi(LED_DRIVER_STATE_FAST);
            }
        }
        else if (strstr(local_buf, "STATUS:ONLINE")) {
            const char* r = strstr(local_buf, "RSSI=");
            if (r) s_rssi = (int8_t)strtol(r + 5, NULL, 10);
            if (conn_cs != APP_NETWORK_CONN_ONLINE) {
                s_conn_state = APP_NETWORK_CONN_ONLINE;
                s_retry_count = 0;
                Led_Driver_Set_WiFi(LED_DRIVER_STATE_ON);
            }
        }
        else if (strstr(local_buf, "STATUS:RSSI=")) {
            const char* r = strstr(local_buf, "RSSI=");
            if (r) s_rssi = (int8_t)strtol(r + 5, NULL, 10);
        }
        else if ((p = strstr(local_buf, "CMD:OFF")) != 0  && (p[7] == '\0' || p[7] == '\r' || p[7] == '\n')) {
            if (!Ui_Controller_Is_No_WiFi_Mode()) {
                if (ss_cmd == INVERTER_CONTROL_SS_STATE_SWEEP || ss_cmd == INVERTER_CONTROL_SS_STATE_DONE) {
                    Inverter_Control_Soft_Start_Stop();
                    g_sys_state = SYS_STATE_IDLE;
                    Ui_Controller_Force_Page_And_Reset(UI_PAGE_MAIN_MENU);
                }
            }
        }
        else if ((p = strstr(local_buf, "CMD:ON")) != 0   && (p[6] == '\0' || p[6] == '\r' || p[6] == '\n')) {
            if (!Ui_Controller_Is_No_WiFi_Mode()) {
                if (ss_cmd == INVERTER_CONTROL_SS_STATE_IDLE) {
                    Inverter_Control_Soft_Start_Trigger();
                    g_sys_state = SYS_STATE_SWEEP;
                    Ui_Controller_Force_Page_And_Reset(UI_PAGE_SWEEP);
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

    /* ── 遥测发送 ── */
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
                written = snprintf(json_buf, sizeof(json_buf),
                         "{\"V\":%.2f,\"I\":%.3f,\"F\":%lu,\"S\":%d}\n",
                         (double)Sys_Safety_Get_EMA_Voltage(),
                         (double)Sys_Safety_Get_EMA_Current(),
                         (ss == INVERTER_CONTROL_SS_STATE_DONE)
                            ? (unsigned long)Pwm_Driver_Get_Frequency()
                            : 0UL,
                         (int)g_sys_state);  /* 对齐全链路数据一致性: 遥测S字段用系统状态 */
                if (written > 0 && (uint16_t)written < sizeof(json_buf))
                    Esp8266_Driver_Send_String(json_buf);
            }
        }
    }
}

int8_t App_Network_Get_RSSI(void) { return s_rssi; }
