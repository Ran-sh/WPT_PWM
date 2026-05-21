/**
 ******************************************************************************
 * @file    User/App_Net.c
 * @brief   网络应用层 —— 实现
 * @note    存放路径: 项目根目录\User\
 *
 *          模块职责:
 *            1. 管理 WiFi / TCP 服务端连接参数 (通过宏配置)
 *            2. App_Net_Init() — 阻塞式联网初始化
 *               先调用 ESP8266_Init() 配置 USART2 硬件,
 *               再调用 ESP8266_ConnectToServer() 执行 AT 联网状态机。
 *               若联网失败则冻结在 OLED 错误码页面。
 *            3. App_Net_Task() — 非阻塞周期任务 (由 main.c 主循环高频调用)
 *               - 每 1000ms: 采集电压/频率 → 封装 JSON → ESP8266_SendString 发送
 *               - 实时轮询: 检查 ESP8266 接收标志 → 解析 ON/OFF 指令 → 执行控制
 *
 *          时间调度: 采用 SysTimer 时间戳差值法 (无标志位, 无阻塞)
 *          状态同步: 调用 UI_SetBridgeState() 确保远程/本地状态一致
 *
 *          依赖: Hardware/ESP8266, Hardware/ADC, Hardware/PWM, Hardware/UI,
 *               Hardware/OLED, System/SysTimer
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "ESP8266.h"
#include "ADC.h"
#include "PWM.h"
#include "UI.h"
#include "OLED.h"
#include "SysTimer.h"
#include "LED.h"
#include "App_Net.h"
#include <string.h>
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════════
 *   WiFi/巴法云配置宏已移至 User/App_Net.h, 方便多分支差异化
 * ═══════════════════════════════════════════════════════════════ */

/* ═══════════════════════════════════════════════════════════════
 *                    本地辅助函数
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief  WiFi 远程开机 (通过 PWM 模块安全使能 + 同步 UI 状态)
 */
static void Net_Remote_On(void)
{
    if (Inverter_SoftStart_GetState() == SS_IDLE) {
        Inverter_SoftStart_Trigger();
        UI_SetBridgeState(1);
    }
}

/**
 * @brief  WiFi 远程关机 (通过 PWM 模块安全关断 + 同步 UI 状态)
 */
static uint8_t s_WiFiConnected = 0;  /* 联网状态唯一权威源, 同时用作 USART2 就绪门禁 */

/* ── V3.2 非阻塞联网状态机 ── */
static NetState_t s_net_state   = NET_IDLE;
static uint8_t    s_net_retry   = 0;
static uint32_t   s_net_tstart  = 0;
static uint8_t    s_net_error   = 0;
static uint8_t    s_net_cancel  = 0;
static uint8_t    s_net_sending  = 0;   /* 1=需发送指令, 0=等响应 */
static char       s_net_cmdbuf[128];

/* AT 等待期间点动画回调 (由 ESP8266_WaitResponse 轮询时调用, ~10ms/次) */
static void AT_DotAnim(void)
{
    static uint8_t  dot  = 0;
    static uint32_t last = 0;

    if (SysTimer_GetTick() - last < 200) return;   /* 200ms 换一次 */
    last = SysTimer_GetTick();

    /* 点循环: 1→2→3→4→5→1→2→... 连续跳动直到联网结束 */
    dot = (dot + 1) % 5;
    switch (dot) {
        case 0: OLED_ShowString(3, 1, "Connecting.    "); break;
        case 1: OLED_ShowString(3, 1, "Connecting..   "); break;
        case 2: OLED_ShowString(3, 1, "Connecting...  "); break;
        case 3: OLED_ShowString(3, 1, "Connecting.... "); break;
        case 4: OLED_ShowString(3, 1, "Connecting..... "); break;
    }
}

static void Net_Remote_Off(void)
{
    Inverter_SoftStart_Stop();
    UI_SetBridgeState(0);
}

/**
 * @brief  巴法云订阅 — 透传通道就绪后发送 cmd=1 订阅主题
 * @note   必须在 s_WiFiConnected=1 之后调用 (ESP8266_SendString 依赖已初始化的 USART2)
 */
static void Bemfa_Subscribe(void)
{
    char subBuf[64];
    snprintf(subBuf, sizeof(subBuf),
             "cmd=1&uid=%s&topic=%s\r\n", BEMFA_UID, BEMFA_TOPIC);
    ESP8266_SendString(subBuf);
}

/* ═══════════════════════════════════════════════════════════════
 *                    公开接口实现
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief  网络应用层初始化 (阻塞执行, 约需 20~30 秒)
 * @retval 0=成功, 1~6=失败 (错误码对应 AT 状态机步骤)
 * @note   失败后显示错误码 3 秒, 然后返回让 UI 重新进入待联网界面,
 *         用户可按 KEY0 重试联网, 无需复位 MCU
 */
uint8_t App_Net_Init(void)
{
    uint8_t connRet;

    /* ── 1. USART2 硬件初始化 (含 PB1 EN 引脚上电时序, 内部已延时 2s) ── */
    ESP8266_Init();

    OLED_ShowString(2, 1, "WiFi Connecting ");

    /* AT Init 点跳动: .→..→...→.→..→... 循环 */
    OLED_ShowString(3, 1, "AT Init.        "); SysTimer_DelayMs(200);
    OLED_ShowString(3, 1, "AT Init..       "); SysTimer_DelayMs(200);
    OLED_ShowString(3, 1, "AT Init...      "); SysTimer_DelayMs(200);
    OLED_ShowString(3, 1, "AT Init.        "); SysTimer_DelayMs(200);
    OLED_ShowString(3, 1, "AT Init..       "); SysTimer_DelayMs(200);
    OLED_ShowString(3, 1, "AT Init...      "); SysTimer_DelayMs(200);

    /* ── 2. 执行联网状态机 (阻塞, AT_DotAnim 在等待期间持续跳动) ── */
    ESP8266_SetWaitCallback(AT_DotAnim);
    connRet = ESP8266_ConnectToServer(WIFI_SSID, WIFI_PASSWORD,
                                       SERVER_IP, SERVER_PORT);
    ESP8266_SetWaitCallback(NULL);   /* 注销回调 */
    if (connRet != 0)
    {
        /* 联网失败: 显示错误码 3 秒后返回, 让用户重试 */
        OLED_Clear();
        OLED_ShowString(1, 1, "!! WiFi Error !! ");
        OLED_ShowString(2, 1, "Err Code:       ");
        OLED_ShowNum(2, 11, connRet, 1);
        OLED_ShowString(3, 1, "Check:          ");
        switch (connRet)
        {
            case 1: OLED_ShowString(4, 1, "No AT Response  "); break;
            case 2: OLED_ShowString(4, 1, "CWMODE Fail     "); break;
            case 3: OLED_ShowString(4, 1, "WiFi Conn Fail  "); break;
            case 4: OLED_ShowString(4, 1, "TCP Conn Fail   "); break;
            case 5: OLED_ShowString(4, 1, "CIPMODE Fail    "); break;
            case 6: OLED_ShowString(4, 1, "CIPSEND Fail    "); break;
            default: break;
        }
        SysTimer_DelayMs(3000);  /* 停留 3 秒看清错误码, 然后返回让用户重试 */
        return connRet;
    }

    /* ── 3. 联网成功提示 ── */
    OLED_Clear();
    OLED_ShowString(1, 1, "WiFi Connected! ");
    OLED_ShowString(2, 1, "TCP: OK         ");
    OLED_ShowString(3, 1, "Port:           ");
    OLED_ShowNum(3, 6, SERVER_PORT, 5);
    SysTimer_DelayMs(2000);  /* 成功画面停留 2s (含 PB3 LED 常亮指示) */
    OLED_Clear();

    s_WiFiConnected      = 1;
    s_WiFiConnected = 1;   /* WiFi 状态唯一权威源 */

    /* V3.4: 透传通道就绪 → 巴法云订阅主题 */
    Bemfa_Subscribe();

    return 0;
}

/**
 * @brief  网络应用层周期任务 (非阻塞, 由 main.c 主循环高频调用)
 * @note   两个子功能均由时间戳差值法调度:
 *          - JSON 遥测: 每 1000ms 执行一次, 格式 {"V":电压,"I":电流,"F":频率}
 *          - 指令解析: 每次调用都检查一次 (无时间约束, 依赖 ESP8266 接收标志)
 */
void App_Net_Task(void)
{
    if (!s_WiFiConnected) return;   /* USART2 未初始化, 禁止发送/接收 */

    /* ── 子功能 1: 定时遥测上报 (时间戳差值法, 每 2000ms, 巴法云 cmd=2 信封) ── */
    {
        static uint32_t last_telemetry = 0;

        if (SysTimer_GetTick() - last_telemetry >= 2000)
        {
            last_telemetry = SysTimer_GetTick();

            /*
             * 扫频期间跳过遥测: ESP8266_SendString 在 115200bps 下
             * 发送 40 字节耗时 ~3.5ms, 会破坏软启动 10ms 步进节拍
             */
            if (Inverter_SoftStart_GetState() != SS_SWEEP)
            {
                char jsonBuf[160];
                snprintf(jsonBuf, sizeof(jsonBuf),
                        "cmd=2&uid=%s&topic=%s&msg={\"V\":%.2f,\"I\":%.2f,\"F\":%lu}\r\n",
                        BEMFA_UID, BEMFA_TOPIC,
                        Get_Real_Voltage(),
                        Get_Real_Current(),
                        (unsigned long)PWM_GetFrequency());
                ESP8266_SendString(jsonBuf);
            }
        }
    }

    /* ── 子功能 2: 远程指令监听 (实时轮询帧标志) ── */
    if (ESP8266_GetRxFlag())
    {
        char localBuf[128];

        /* 原子读取: 单一临界区内拷贝+清空, 无嵌套开中断风险 */
        ESP8266_CopyRxFrame(localBuf, sizeof(localBuf));

        uint8_t cmd_on    = (strstr(localBuf, "CMD:ON")  != NULL);
        uint8_t cmd_off   = (strstr(localBuf, "CMD:OFF") != NULL);
        uint8_t cmd_closed = (strstr(localBuf, "CLOSED") != NULL);

        if (cmd_on)
        {
            Net_Remote_On();
            OLED_ShowString(4, 1, "CMD: Remote ON  ");
        }
        else if (cmd_off)
        {
            Net_Remote_Off();
            OLED_ShowString(4, 1, "CMD: Remote OFF ");
        }

        /*
         * ESP8266 物理断线: 复位联网状态, UI 回到待联网界面
         */
        if (cmd_closed)
        {
            /*
             * 立即关断逆变器 → 通知 UI 掉线 → 复位网络就绪标志
             * 全程非阻塞, 不调用任何 DelayMs
             * UI 下一帧检测到 wifi_connected=0 后自动显示重连界面
             */
            Inverter_SoftStart_Stop();
            UI_SetBridgeState(0);
            s_WiFiConnected      = 0;
            s_WiFiConnected = 0;
        }
    }
}

uint8_t App_Net_IsConnected(void)
{
    return s_WiFiConnected;
}

/* ═══════════════════════════════════════════════════════════════
 *              V3.2 非阻塞联网状态机
 * ═══════════════════════════════════════════════════════════════ */

void App_Net_Connect_Trigger(void)
{
    if (s_net_state != NET_IDLE) return;

    /*
     * 每次联网前: 关逆变器 + ESP8266 双重复位 (~3.5s),
     * 确保模块从完全干净状态启动
     */
    Inverter_SoftStart_Stop();
    ESP8266_Init();

    s_net_state   = NET_STEP_AT;
    s_net_retry   = 0;
    s_net_cancel  = 0;
    s_net_error   = 0;
    s_net_sending = 1;
    s_net_tstart  = SysTimer_GetTick();

    ESP8266_ClearRxBuffer();
}

void App_Net_Connect_Cancel(void)
{
    if (s_net_state > NET_IDLE && s_net_state < NET_SUCCESS)
        s_net_cancel = 1;
}

NetState_t App_Net_GetConnectState(void) { return s_net_state; }
uint8_t    App_Net_GetErrorCode(void)    { return s_net_error; }

void App_Net_Connect_Task(void)
{
    uint16_t tmo;
    char localBuf[64];
    uint8_t has_err, has_ok;

    /* ── NET_FAIL 自动恢复 (UI_Task 处理显示) ── */
    if (s_net_state == NET_FAIL) {
        if (SysTimer_GetTick() - s_net_tstart >= 3000) {
            s_net_state = NET_IDLE;
        }
        return;
    }

    if (s_net_state <= NET_IDLE || s_net_state >= NET_SUCCESS)
        return;

    /*
     * 点动画: 每 200ms 在 Line 3 跳动, 替代 WaitResponse 回调
     * 异步版不走 WaitResponse, 必须在此处手动驱动
     */
    AT_DotAnim();

    /* ── KEY1 取消 ── */
    if (s_net_cancel) {
        s_net_cancel = 0;
        s_net_state  = NET_IDLE;
        ESP8266_SetWaitCallback(NULL);
        LED_Update_WiFi(LED_OFF);
        return;
    }

    /* ═══════ 阶段 A: 发送指令 ═══════ */
    if (s_net_sending) {
        s_net_sending = 0;
        s_net_tstart  = SysTimer_GetTick();
        ESP8266_ClearRxBuffer();

        switch (s_net_state) {
            case NET_STEP_AT:
                ESP8266_SendString("AT\r\n"); break;
            case NET_STEP_CWMODE:
                ESP8266_SendString("AT+CWMODE=1\r\n"); break;
            case NET_STEP_CWJAP:
                snprintf(s_net_cmdbuf, sizeof(s_net_cmdbuf),
                    "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);
                ESP8266_SendString(s_net_cmdbuf); break;
            case NET_STEP_CIPSTART:
                snprintf(s_net_cmdbuf, sizeof(s_net_cmdbuf),
                    "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n", SERVER_IP, SERVER_PORT);
                ESP8266_SendString(s_net_cmdbuf); break;
            case NET_STEP_CIPMODE:
                ESP8266_SendString("AT+CIPMODE=1\r\n"); break;
            case NET_STEP_CIPSEND:
                ESP8266_SendString("AT+CIPSEND\r\n"); break;
            default: break;
        }
        return;
    }

    /* ═══════ 阶段 B: 轮询响应 ═══════ */

    /* CIPSEND: 应答是 ">" 不带 \r\n */
    if (s_net_state == NET_STEP_CIPSEND) {
        if (ESP8266_BufferContains(">")) { s_net_state = NET_SUCCESS; goto on_success; }
        if (SysTimer_GetTick() - s_net_tstart >= ESP8266_CMD_TIMEOUT) goto on_timeout;
        return;
    }

    /* 其余步骤: 等帧标志 */
    if (!ESP8266_GetRxFlag()) {
        tmo = (s_net_state == NET_STEP_CWJAP)  ? ESP8266_WIFI_TIMEOUT
            : (s_net_state == NET_STEP_CIPSTART) ? ESP8266_TCP_TIMEOUT
            :                                      ESP8266_CMD_TIMEOUT;
        if (SysTimer_GetTick() - s_net_tstart < tmo) return;

on_timeout:
on_fail:
        s_net_retry++;
        if (s_net_retry >= 3) {
            s_net_error = (uint8_t)(s_net_state - NET_STEP_AT + 1);
            s_net_state = NET_FAIL;
            s_net_tstart = SysTimer_GetTick();
            ESP8266_SetWaitCallback(NULL);
            LED_Update_WiFi(LED_OFF);
            return;
        }
        s_net_sending = 1;
        return;
    }

    /* OK/ERROR 判断 */
    ESP8266_CopyRxFrame(localBuf, sizeof(localBuf));
    has_err = (strstr(localBuf, "ERROR") || strstr(localBuf, "FAIL"));
    has_ok  = (strstr(localBuf, "OK") != NULL);

    if (has_err) goto on_fail;

    if (!has_ok) return;

    /* OK → 下一步 */
    s_net_retry = 0;
    s_net_state = (NetState_t)((uint8_t)s_net_state + 1);

    if (s_net_state == NET_SUCCESS) {
on_success:
        ESP8266_SetWaitCallback(NULL);
        s_WiFiConnected      = 1;
        s_WiFiConnected = 1;
        Bemfa_Subscribe();   /* V3.4: 透传通道就绪 → 巴法云订阅主题 */
        LED_Update_WiFi(LED_OFF);
        return;
    }
    s_net_sending = 1;
}
