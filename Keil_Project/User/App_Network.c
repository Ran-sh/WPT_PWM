/**
 ******************************************************************************
 * @file    User/App_Network.c
 * @brief   ESP8266连接管理、远程命令与遥测应用层 — V5.1.1
 *
 *  硬件连接与通信协议:
 *  +------------------------------------------------------------+
 *  |    STM32                            ESP8266                 |
 *  |    PA2 (USART2_TX) ------------------>  RXD                 |
 *  |    PA3 (USART2_RX) <------------------  TXD                 |
 *  |    PA1 (推挽输出)  ------------------>  RST                  |
 *  |    PB11(推挽输出)  ------------------>  CH_PD / EN           |
 *  |                                                             |
 *  |    串口参数：115200位/秒，8位数据，无校验，1位停止位       |
 *  |    遥测上报：{"V":xx,"I":xx,"F":xx,"S":x}\n          |
 *  |    命令下发：CMD:ON、CMD:OFF、CMD:SETFREQ:xxx               |
 *  |                                                             |
 *  |    连接流程：空闲 -> 无线连接 -> 消息连接 -> 在线          |
 *  |    被动离线会自动监听恢复，主动离线需要用户手动恢复        |
 *  |                                                             |
 *  |    重试间隔按5s、15s、30s、60s、2min逐步增加，最多5次      |
 *  |    8s未收到ESP数据判定离线；消息连接30s无进展则回退重试    |
 *  |    ESP串口一旦收到启动数据，可提前结束上电等待             |
 *  |                                                             |
 *  |    接收帧在临界区内一次性检查、复制并消费                  |
 *  |    命令状态与连接状态按帧快照，避免解析期间发生竞争        |
 *  +------------------------------------------------------------+
 *
 * @note    网络任务不依赖当前显示页面，在线时持续处理指令和遥测。
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
#include "App_Storage.h"       /* 黑匣子日志 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define APP_NETWORK_CONNECT_TIMEOUT_MS   8000
#define APP_NETWORK_MAX_RETRIES            5   /* 被动断开达到上限后进入被动离线。 */
#define APP_NETWORK_TELEMETRY_PERIOD_MS  500

static App_Network_Conn_State s_conn_state    = APP_NETWORK_CONN_IDLE;
static uint8_t                s_retry_count   = 0;
static uint32_t               s_connect_start = 0;
static int8_t                 s_rssi          = -100;
static uint32_t               s_last_esp_ms   = 0;

static uint8_t App_Network_Map_Telemetry_State(Sys_State state)
{
    switch (state) {
        case SYS_STATE_SWEEP:
            return 1U;
        case SYS_STATE_RUNNING:
            return 2U;
        case SYS_STATE_FAULT:
            return 3U;
        case SYS_STATE_INIT:
        case SYS_STATE_IDLE:
        default:
            return 0U;
    }
}

static uint8_t App_Network_Is_Exact_Frame(const char *frame,
                                          const char *expected)
{
    if (frame == 0 || expected == 0) return 0U;
    return (strcmp(frame, expected) == 0) ? 1U : 0U;
}

static uint8_t App_Network_Is_Canonical_Number(const char *text,
                                               uint8_t allow_negative)
{
    const char *cursor;

    if (text == 0 || *text == '\0') return 0U;
    cursor = text;
    if (*cursor == '-') {
        if (allow_negative == 0U) return 0U;
        cursor++;
    }
    if (*cursor < '0' || *cursor > '9') return 0U;
    while (*cursor >= '0' && *cursor <= '9') cursor++;
    return (*cursor == '\0') ? 1U : 0U;
}

static uint8_t App_Network_Parse_Number_Frame(const char *frame,
                                              const char *prefix,
                                              long minimum, long maximum,
                                              long *value)
{
    const char *number_text;
    char *endp;
    long parsed;
    size_t prefix_len;

    if (frame == 0 || prefix == 0 || value == 0) return 0U;
    prefix_len = strlen(prefix);
    if (strncmp(frame, prefix, prefix_len) != 0) return 0U;
    number_text = frame + prefix_len;
    if (App_Network_Is_Canonical_Number(number_text,
            (minimum < 0L) ? 1U : 0U) == 0U) return 0U;
    parsed = strtol(number_text, &endp, 10);
    if (endp == number_text || *endp != '\0' ||
        parsed < minimum || parsed > maximum) return 0U;
    *value = parsed;
    return 1U;
}

static uint8_t App_Network_Is_Recovery_Frame(const char *frame)
{
    long value;

    if (App_Network_Is_Exact_Frame(frame, "STATUS:MQTT") != 0U) return 1U;
    if (App_Network_Parse_Number_Frame(frame, "STATUS:ONLINE:RSSI=",
                                       -127L, 0L, &value) != 0U) return 1U;
    if (App_Network_Parse_Number_Frame(frame, "STATUS:RSSI=",
                                       -127L, 0L, &value) != 0U) return 1U;
    return 0U;
}

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

void App_Network_Manual_Connect(void)
{
    /* 仅允许从主动离线恢复，清除门控后重新开始连接。 */
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
    /* 主动离线后，STM32忽略连接状态帧，必须由用户手动恢复。
     * ESP收到断开命令后保留凭证并继续尝试恢复无线链路。
     * 两个控制器状态不同属于有意设计：ESP维持底层连接能力，
     * STM32则通过主动离线门控阻止应用层自动恢复。 */
    if (s_conn_state == APP_NETWORK_CONN_ONLINE
        || s_conn_state == APP_NETWORK_CONN_WIFI
        || s_conn_state == APP_NETWORK_CONN_MQTT) {
        if (Esp8266_Driver_Send_String("CMD:WIFI_DISC\n") !=
            ESP8266_DRIVER_TX_OK) {
            return;
        }
    }
    /* 无论当前状态如何, 强制进入主动离线 */
    s_conn_state    = APP_NETWORK_CONN_OFFLINE_ACTIVE;
    s_retry_count   = 0;
    s_rssi          = -100;
    Led_Driver_Set_WiFi(LED_DRIVER_STATE_OFF);
}

/** @brief 从被动离线恢复；ESP仍在运行，因此只切换状态而不硬件复位 */
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

uint8_t App_Network_Is_Offline(void)
{
    return (s_conn_state == APP_NETWORK_CONN_OFFLINE_PASSIVE
         || s_conn_state == APP_NETWORK_CONN_OFFLINE_ACTIVE);
}

/* 连接重试与指数退避。 */
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

        /* 重试达到上限后进入被动离线，并继续监听热点恢复。 */
        if (s_retry_count >= APP_NETWORK_MAX_RETRIES) {
            s_conn_state  = APP_NETWORK_CONN_OFFLINE_PASSIVE;
            s_retry_count = 0;
            Led_Driver_Set_WiFi(LED_DRIVER_STATE_OFF);
            return;
        }

        s_connect_start = Sys_Timer_Get_Tick();
        s_last_esp_ms   = Sys_Timer_Get_Tick();
        /* ESP仍在运行且会自行尝试恢复无线连接，不需要硬件复位。
         * 每次复位都会额外等待4s，反而降低有限重试的有效性。 */
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
    }

    /* 消息连接超过30s无进展时回退到无线连接阶段，
     * 防止无线已连接但消息服务器不可达时长期停滞。 */
    if (s_conn_state == APP_NETWORK_CONN_MQTT
        && Sys_Timer_Get_Tick() - s_last_esp_ms >= 30000) {
        s_conn_state    = APP_NETWORK_CONN_WIFI;
        s_retry_count   = 0;
        s_connect_start = Sys_Timer_Get_Tick();
        s_last_esp_ms   = Sys_Timer_Get_Tick();
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
    }
}

/* 被动离线监听：收到ESP有效状态帧后恢复自动连接。 */
static void App_Network_Check_Offline_Recovery(void)
{
    if (s_conn_state != APP_NETWORK_CONN_OFFLINE_PASSIVE)
        return;

    /* 被动监听期间，收到有效状态帧说明ESP已经恢复通信。
     * 此时只把STM32切回无线连接阶段，不复位仍在运行和重连的ESP。 */
    {
        char local_buf[256];
        if (!Esp8266_Driver_Try_Copy_Rx_Frame(local_buf, sizeof(local_buf)))
            return;

        /* 只响应表示连接进展的状态帧，忽略断开帧以避免反复切换。 */
        if (App_Network_Is_Recovery_Frame(local_buf) != 0U) {
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
    /* 推进ESP8266非阻塞硬件初始化状态机。 */
    Esp8266_Driver_Init_Task();

    if (!Esp8266_Driver_Is_Ready()) return;

    /* 被动离线恢复优先于常规重试检查。 */
    App_Network_Check_Offline_Recovery();

    App_Network_Check_Retry();

    /* 在线状态连续8s未收到ESP数据帧时，判定离线并开始重试。 */
    if (s_conn_state == APP_NETWORK_CONN_ONLINE
        && Sys_Timer_Get_Tick() - s_last_esp_ms >= APP_NETWORK_CONNECT_TIMEOUT_MS) {
        s_conn_state    = APP_NETWORK_CONN_WIFI;
        s_retry_count   = 0;
        s_connect_start = Sys_Timer_Get_Tick();
        s_last_esp_ms   = Sys_Timer_Get_Tick();
        s_rssi          = -100;
        Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
        /* ESP仍在运行并会自行恢复无线连接，因此不执行硬件复位。 */
    }

    /* 接收帧在一个临界区内完成判断、复制和消费，避免中断竞争造成丢帧。 */
    {
        char local_buf[256];
        Inverter_Control_Soft_Start_State ss_cmd;
        uint8_t conn_cs;
        uint8_t frame_valid;
        long parsed_value;

        if (!Esp8266_Driver_Try_Copy_Rx_Frame(local_buf, sizeof(local_buf)))
            goto skip_frame;                             /* 没有完整帧时跳过解析。 */

        frame_valid = 0U;
        parsed_value = 0L;
        ss_cmd  = Inverter_Control_Soft_Start_Get_State();
        conn_cs = s_conn_state; /* 固定本帧解析所用状态。 */
        if (s_conn_state == APP_NETWORK_CONN_OFFLINE_PASSIVE
         || s_conn_state == APP_NETWORK_CONN_OFFLINE_ACTIVE)
            goto skip_frame;                             /* 主动离线时丢弃帧，防止状态回显解除门控。 */

        if (App_Network_Is_Exact_Frame(local_buf,
                                       "STATUS:DISCONNECTED") != 0U) {
            frame_valid = 1U;
            s_conn_state    = APP_NETWORK_CONN_WIFI;
            s_retry_count   = 0;
            s_connect_start = Sys_Timer_Get_Tick();
            s_rssi          = -100;
            Led_Driver_Set_WiFi(LED_DRIVER_STATE_SLOW);
        }
        else if (App_Network_Is_Exact_Frame(local_buf,
                                            "STATUS:MQTT") != 0U) {
            frame_valid = 1U;
            if (conn_cs == APP_NETWORK_CONN_WIFI) {
                s_conn_state    = APP_NETWORK_CONN_MQTT;
                s_connect_start = Sys_Timer_Get_Tick();
                Led_Driver_Set_WiFi(LED_DRIVER_STATE_FAST);
            }
        }
        else if (App_Network_Parse_Number_Frame(
                     local_buf, "STATUS:ONLINE:RSSI=", -127L, 0L,
                     &parsed_value) != 0U) {
            frame_valid = 1U;
            s_rssi = (int8_t)parsed_value;
            if (conn_cs != APP_NETWORK_CONN_ONLINE) {
                s_conn_state = APP_NETWORK_CONN_ONLINE;
                s_retry_count = 0;
                Led_Driver_Set_WiFi(LED_DRIVER_STATE_ON);
            }
        }
        else if (App_Network_Parse_Number_Frame(
                     local_buf, "STATUS:RSSI=", -127L, 0L,
                     &parsed_value) != 0U) {
            frame_valid = 1U;
            s_rssi = (int8_t)parsed_value;
        }
        else if (App_Network_Is_Exact_Frame(local_buf, "CMD:OFF") != 0U) {
            frame_valid = 1U;
            if (!Ui_Controller_Is_No_WiFi_Mode()) {
                if (ss_cmd == INVERTER_CONTROL_SS_STATE_SWEEP || ss_cmd == INVERTER_CONTROL_SS_STATE_DONE) {
                    if (Sys_Core_Request_Stop() == SYS_CONTROL_RESULT_OK) {
                        Ui_Controller_Force_Page_And_Reset(UI_PAGE_MAIN_MENU);
                    }
                }
            }
        }
        else if (App_Network_Is_Exact_Frame(local_buf, "CMD:ON") != 0U) {
            frame_valid = 1U;
            if (!Ui_Controller_Is_No_WiFi_Mode()) {
                if (ss_cmd == INVERTER_CONTROL_SS_STATE_IDLE) {
                    if (Sys_Core_Request_Start() == SYS_CONTROL_RESULT_OK) {
                        Ui_Controller_Force_Page_And_Reset(UI_PAGE_SWEEP);
                    }
                }
            }
        }
        else if (App_Network_Parse_Number_Frame(
                     local_buf, "CMD:SETFREQ:",
                     (long)PWM_DRIVER_FREQ_MIN_HZ,
                     (long)PWM_DRIVER_FREQ_MAX_HZ,
                     &parsed_value) != 0U) {
            frame_valid = 1U;
            if (!Ui_Controller_Is_No_WiFi_Mode()) {
                if (ss_cmd == INVERTER_CONTROL_SS_STATE_DONE) {
                    Inverter_Control_Freq_Ramp_Trigger(
                        (uint32_t)parsed_value);
                }
            }
        }
        if (frame_valid != 0U) s_last_esp_ms = Sys_Timer_Get_Tick();
    }
    skip_frame:

    /* 按系统状态生成并发送遥测数据。 */
    {
        static uint32_t last_telemetry = 0;
        uint32_t now;
        uint32_t frequency_hz;
        Sys_State state;
        uint8_t protocol_state;
        char json_buf[80];
        int written;

        now = Sys_Timer_Get_Tick();
        if ((s_conn_state == APP_NETWORK_CONN_ONLINE) &&
            (now - last_telemetry >= APP_NETWORK_TELEMETRY_PERIOD_MS)) {
            state = Sys_Core_Get_State();
            frequency_hz = 0U;
            if ((state == SYS_STATE_SWEEP) ||
                (state == SYS_STATE_RUNNING)) {
                frequency_hz = Pwm_Driver_Get_Frequency();
            }
            protocol_state = App_Network_Map_Telemetry_State(state);
            written = snprintf(json_buf, sizeof(json_buf),
                     "{\"V\":%.2f,\"I\":%.3f,\"F\":%lu,\"S\":%u}\n",
                     (double)Adc_Driver_Get_Display_Voltage(),
                     (double)Adc_Driver_Get_Display_Current(),
                     (unsigned long)frequency_hz,
                     (unsigned int)protocol_state);
            if ((written > 0) && ((uint16_t)written < sizeof(json_buf))) {
                if (Esp8266_Driver_Send_String(json_buf) ==
                    ESP8266_DRIVER_TX_OK) {
                    last_telemetry = now;
                }
            } else {
                last_telemetry = now;
            }
        }
    }
}

int8_t App_Network_Get_RSSI(void) { return s_rssi; }
