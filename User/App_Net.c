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
#include "App_Net.h"
#include <string.h>
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════════
 *              WiFi 和目标服务器配置 (用户须根据实际环境修改)
 * ═══════════════════════════════════════════════════════════════ */
#define WIFI_SSID       "Rss"           /* WiFi 热点名称 */
#define WIFI_PASSWORD   "123456789"        /* WiFi 密码 */
#define SERVER_IP       "10.219.216.212"    /* PC 端 IPv4 地址 (WLAN, 2026-05-16) */
#define SERVER_PORT     8080                /* TCP 监听端口 */

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
static uint8_t s_NetReady      = 0;   /* 0: USART2 未初始化, 屏蔽 Task 操作 */
static uint8_t s_WiFiConnected = 0;   /* WiFi 状态唯一权威源, UI 通过 App_Net_IsConnected 查询 */

/* AT 等待期间点动画回调 (由 ESP8266_WaitResponse 轮询时调用, ~10ms/次) */
static void AT_DotAnim(void)
{
    static uint8_t  dot  = 0;
    static uint32_t last = 0;

    if (SysTimer_GetTick() - last < 200) return;   /* 200ms 换一次 */
    last = SysTimer_GetTick();

    dot = (dot + 1) % 3;
    switch (dot) {
        case 0: OLED_ShowString(3, 1, "AT Init.        "); break;
        case 1: OLED_ShowString(3, 1, "AT Init..       "); break;
        case 2: OLED_ShowString(3, 1, "AT Init...      "); break;
    }
}

static void Net_Remote_Off(void)
{
    Inverter_SoftStart_Stop();
    UI_SetBridgeState(0);
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

    OLED_ShowString(2, 1, "WiFi Connecting");

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
        OLED_ShowString(1, 1, "!!! WiFi Error !!!");
        OLED_ShowString(2, 1, "Err Code:");
        OLED_ShowNum(2, 11, connRet, 1);
        OLED_ShowString(3, 1, "Check:");
        switch (connRet)
        {
            case 1: OLED_ShowString(4, 1, "No AT Response");  break;
            case 2: OLED_ShowString(4, 1, "CWMODE Fail");     break;
            case 3: OLED_ShowString(4, 1, "WiFi Connect Fail"); break;
            case 4: OLED_ShowString(4, 1, "TCP Connect Fail");  break;
            case 5: OLED_ShowString(4, 1, "CIPMODE Fail");    break;
            case 6: OLED_ShowString(4, 1, "CIPSEND Fail");    break;
            default: break;
        }
        SysTimer_DelayMs(3000);  /* 停留 3 秒看清错误码, 然后返回让用户重试 */
        return connRet;
    }

    /* ── 3. 联网成功提示 ── */
    OLED_Clear();
    OLED_ShowString(1, 1, "WiFi Connected!");
    OLED_ShowString(2, 1, "TCP: OK");
    OLED_ShowString(3, 1, "Port:");
    OLED_ShowNum(3, 6, SERVER_PORT, 5);
    SysTimer_DelayMs(2000);  /* 成功画面停留 2s (含 PB3 LED 常亮指示) */
    OLED_Clear();

    s_NetReady      = 1;
    s_WiFiConnected = 1;   /* WiFi 状态唯一权威源 */
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
    if (!s_NetReady) return;   /* USART2 未初始化, 禁止发送/接收 */

    /* ── 子功能 1: 定时遥测上报 (时间戳差值法, 每 1000ms) ── */
    {
        static uint32_t last_telemetry = 0;

        if (SysTimer_GetTick() - last_telemetry >= 1000)
        {
            last_telemetry = SysTimer_GetTick();

            /*
             * 扫频期间跳过遥测: ESP8266_SendString 在 115200bps 下
             * 发送 40 字节耗时 ~3.5ms, 会破坏软启动 10ms 步进节拍
             */
            if (Inverter_SoftStart_GetState() != SS_SWEEP)
            {
                char jsonBuf[128];
                snprintf(jsonBuf, sizeof(jsonBuf),
                        "{\"V\":%.2f,\"I\":%.2f,\"F\":%lu}\r\n",
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
            OLED_ShowString(4, 1, "CMD: Remote ON ");
        }
        else if (cmd_off)
        {
            Net_Remote_Off();
            OLED_ShowString(4, 1, "CMD: Remote OFF");
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
            s_NetReady      = 0;
            s_WiFiConnected = 0;   /* 断线: 权威源清 0 */
        }
    }
}

uint8_t App_Net_IsConnected(void)
{
    return s_WiFiConnected;
}
