# ESP8266 无线 OTA 字库推送 — 设计文档

> 版本: V1.0.0 | 日期: 2026-06-23

## 1. 目的

通过板载 ESP8266-01S 充当无线下载器，实现在不拆外壳、不接 USB-TTL 的情况下更新 W25Q128 Flash 中的字库数据。

## 2. 方案: 最小可行版 (Phase A)

**只推送当前已存在的 95 ASCII + 76 汉字（~4KB）**，验证 TCP→ESP→USART→STM32→Flash 完整链路。

## 3. 架构

```
┌──────────┐   WiFi TCP    ┌─────────────┐   USART2    ┌──────────┐    SPI1     ┌──────────┐
│ PC 端     │ ────────────→ │ ESP8266      │ ──────────→ │ STM32    │ ──────────→ │ W25Q128  │
│ ota_font  │  port 8266    │ WiFiServer    │  文本帧      │ App_Stor │  Page Prog  │ 字库区    │
│ _push.py  │ ← ─ ─ ─ ─ ─ ─ │ + Serial透传  │ ← ─ ─ ─ ─ ─ │ age OTA  │ ← ─ ─ ─ ─ ─ │ FONT     │
│           │  ACK/进度      │               │  ACK 帧      │ Handler  │             │ 0x000000 │
└──────────┘               └─────────────┘              └──────────┘            └──────────┘
```

数据路径: PC → TCP → ESP Serial.print → USART2 ISR → STM32帧解析 → W25Q_PageProgram
ACK 路径: STM32 → USART2 TX → ESP Serial.read → TCP write → PC

### 3.1 系统前置条件

- 推送前设备必须处于 **IDLE 态**（PWM 关）
- 推送期间 L4 防线（发波禁擦）天然放行
- 推送完成后自动恢复字库状态（重启 TFT 渲染）

## 4. 帧协议

### 4.1 PC → ESP (TCP)

```
OTA:START\n                    — 通知 STM32 进入 OTA 模式
OTA:<seq>,<base64_256b>\n      — 数据页 (seq: 0-15)
OTA:END\n                      — 传输完成，触发 CRC32 校验
```

| 字段 | 说明 |
|:---|:---|
| `seq` | 0-index 页序号，0~15（4KB ÷ 256B） |
| `base64_256b` | 256B 原始二进制 → 344B Base64 编码 |

### 4.2 ESP → PC (TCP 回传)

ESP 仅做串口↔TCP双向透传，不解析帧内容。

### 4.3 为什么选 Base64

| 方案 | 膨胀率 | `\n` 风险 | ESP 实现 |
|:---|:---|:---|:---|
| 原始二进制 | 1× | **致命**: 0x0A 截断帧 | N/A |
| Hex | 2× (512B) | 安全 | 简单 |
| **Base64** | **1.34× (344B)** | **安全** | **64B 查表** |

### 4.4 STM32 → PC (ACK)

```
OTA:ACK:<seq>\n                — 单页写入成功
OTA:ERR:<seq>,<reason>\n       — 单页失败 (reason: CRC/BUSY/TIMEOUT)
OTA:DONE\n                     — 全部完成，CRC32 校验通过
OTA:FAIL:<reason>\n            — 全部完成，CRC32 校验失败
```

## 5. 各模块设计

### 5.1 ESP8266 (Arduino_Project/...ino)

新增 `OTA_Font_Server` 状态机:

```
正常态 → 收到 "OTA:START" → 启动 WiFiServer(8266)
       → TCP client 连接 → 逐行读取 → Serial.print 透传到 STM32
       → 收到 "OTA:DONE" 或 "OTA:FAIL" → 关闭 Server → 恢复正常态
```

关键点:
- `WiFiServer server(8266)` + `WiFiClient client` + `client.available()` 轮询
- 串口侧: `Serial_Parse_Read_Loop()` 现有逻辑不变，新增帧类型识别 `OTA:START` / `OTA:END`
- ACK 回传: `client.print()` 将 STM32 的 ACK 帧原样回传给 PC

### 5.2 STM32 App_Network.c

新增帧分发:

```c
/* 在 skip_frame 之前插入 */
if (strstr(local_buf, "OTA:START")) {
    /* IDLE 态门控: 仅 IDLE 可进入 OTA 模式 */
    if (g_sys_state == SYS_STATE_IDLE) {
        App_Storage_OTA_Begin();
    }
    goto skip_frame;
}
if (strstr(local_buf, "OTA:")) {
    App_Storage_OTA_Handler(local_buf);  /* 委托 App_Storage */
    goto skip_frame;
}
```

关键点:
- **IDLE 态门控**: 非 IDLE 态忽略 OTA 帧，防运行时误触发
- OTA 期间遥测暂停（IDLE 态已跳过遥测）

### 5.3 STM32 App_Storage.c

新增 OTA 处理:

```c
/* OTA 运行时状态 */
static uint8_t  s_ota_active = 0;
static uint32_t s_ota_page_count = 0;
static uint8_t  s_ota_buf[256];  /* 单页缓冲 */

void App_Storage_OTA_Begin(void) {
    s_ota_active = 1;
    s_ota_page_count = 0;
    /* 擦除字库区前 4KB */
    W25Q_Driver_Erase_Sector(W25Q_ADDR_FONT);
    /* Gauge 显示 OTA 进度 */
    Ui_Controller_Show_OTA_Progress(0, 16);
}

void App_Storage_OTA_Handler(const char *frame) {
    /* 解析 OTA:<seq>,<base64> */
    /* Base64 解码 → s_ota_buf */
    /* W25Q_Driver_Write_Page(FONT_BASE + seq*256, s_ota_buf, 256) */
    /* 回 ACK: OTA:ACK:<seq> 或 OTA:ERR:<seq>,<reason> */
    /* 更新 TFT 进度 */
}

void App_Storage_OTA_End(void) {
    /* 全量 CRC32 校验 */
    /* 成功 → OTA:DONE + FONT_OK */
    /* 失败 → OTA:FAIL:crc32 */
    s_ota_active = 0;
}
```

关键点:
- L4 门控天然满足（IDLE 态不发波，W25Q_Driver_Erase_Sector 不会拦截）
- 每页写前 W25Q_Driver_Write_Page 自动发 WREN + 等 Busy
- Base64 解码查表 64B 即可，不依赖系统库

### 5.4 PC 端工具 (Claude_Files/tools/ota_font_push.py)

```python
# 输入: TFT_Font_Data.h (解析 C 数组格式)
# 流程:
#   1. 解析 .h 文件，提取 ASCII(95字) + CN_FONT(76字) → 拼成 4KB bin
#   2. 查找 ESP8266 IP (mDNS 或 ARP 扫描或手动指定)
#   3. TCP connect → 发 OTA:START
#   4. 循环: 读取 bin → Base64编码 → 发帧 → 等待 ACK → 显示进度
#   5. 发 OTA:END → 等待 DONE/FAIL
# 
# 依赖: Python 3.6+ (标准库 only, 无外部 pip)
# 用法: python ota_font_push.py --ip 192.168.1.100 [--font-data path/to/TFT_Font_Data.h]
```

错误处理:
- TCP 连接失败 → 提示"无法连接 ESP8266，确认 WiFi 同网段"
- ACK 超时 (3s) → 重发当前页，3 次失败中止
- Base64 解码长度异常 → 丢弃，NACK 通知重发

## 6. 文件改动清单

| 文件 | 改动类型 | 行数估算 | 说明 |
|:---|:---|:---|:---|
| `Arduino_Project/ESP8266_MQTT_Firmware.ino` | 修改 | +80 | OTA Server 状态机 + Base64 编解码 + TCP 透传 |
| `Keil_Project/User/App_Storage.c` | 修改 | +80 | OTA Handler + Base64 解码 + 逐页写 Flash |
| `Keil_Project/User/App_Storage.h` | 修改 | +10 | 新增 OTA 函数声明 |
| `Keil_Project/User/App_Network.c` | 修改 | +15 | OTA 帧分发 + IDLE 门控 |
| `Keil_Project/Hardware/Esp8266_Driver.c` | 不变 | 0 | 复用现有 USART2 收发 |
| `Claude_Files/tools/ota_font_push.py` | **新建** | ~120 | PC 端推送工具 |

总计 ~305 行新增代码。

## 7. 操作步骤

### 7.1 首次烧录 (已有方式，走 USB-TTL)

1. Keil F8 编译 + 下载
2. STM32 上电自检 → 发现 Flash 字库区为空 → 自动从 `TFT_Font_Data.h` 搬运 76 汉字 + 95 ASCII 到 W25Q128
3. TFT 正常显示中文 — 完成

### 7.2 日后 OTA 更新字库 (新方式，不走 USB-TTL)

1. 确认设备处于 **IDLE 态**（停机，TFT 显示主菜单）
2. PC 连上与 ESP8266 **同一 WiFi**
3. 运行: `python ota_font_push.py --ip <ESP8266_IP>`
4. TFT 屏幕显示进度条: `OTA 3/16`
5. 完成 → TFT 显示"字库更新成功" → 按键返回主菜单
6. 失败 → TFT 显示"更新失败" + 错误原因 → 重新运行脚本重试

### 7.3 获取 ESP8266 IP 地址

- 方式 1: TFT WIFI_SETUP 页面查看当前 IP
- 方式 2: 路由器管理页面 DHCP 客户端列表
- 方式 3: `ping STM32_WPT_Config.local` (mDNS)

## 8. 后续升级: Phase B 全字库

> 详见 [[ota-font-push-phase-b]]

Phase A 验证链路后，升级为 GB2312 全字库 (6763 汉字 + ASCII + 图标，~668KB):
- 协议层完全复用，仅页数变化 (16 → ~2600)
- PC 端需要字模提取工具链 (FreeType + PIL 从系统字体生成)
- TFT 进度条改为两档刷新（避免逐页刷新开销）

## 9. 决断记录

| 项 | 选择 | 理由 |
|:---|:---|:---|
| 传输通道 | TCP WiFiServer | 复用 1.0LAN 分支已验证 TCP 模式，局域网延迟低 |
| 帧编码 | Base64 | 比 hex 膨胀小，安全避开 `\n` 断帧 |
| 推送前状态 | 强制 IDLE | L4 防线天然放行，安全第一 |
| Phase A 数据量 | 4KB (76字+95ASCII) | 最小可行验证，~2s 传完 |
| Phase B | 后续升级 | 协议层复用，6736汉字 668KB |
