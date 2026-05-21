# 巴法云 TCP 创客云接入设计 (V3.4)

**日期**: 2026-05-21
**分支**: WAN
**基线**: V3.3 (ESP8266 静默看门狗 + 非阻塞联网状态机)

---

## 目标

将无线供电系统从局域网 TCP（NetAssist）升级为巴法云 TCP 创客云远程控制，实现广域网接入。

## 协议差异

| 项目 | LAN (master) | WAN (此分支) |
|------|-------------|-------------|
| 服务器 | PC 局域网 IP:8080 | 114.116.142.124:8344 |
| 上线握手 | 无 | `cmd=1&uid=xxx&topic=xxx\r\n` 订阅 |
| 遥测格式 | `{"V":x,"I":x,"F":x}\r\n` | `cmd=2&uid=xxx&topic=xxx&msg={"V":x,...}\r\n` |
| 遥测间隔 | 1000ms | 2000ms (1Hz 限流) |
| 下行指令 | `CMD:ON\r\n` 裸发 | `cmd=2&...&msg=CMD:ON\r\n` 包在信封内 |
| 断线检测 | CLOSED + 15s 静默看门狗 | 仅 CLOSED |

## 设计决策

### 1. 宏配置提升到 header

6 个配置宏从 `App_Net.c` 移至 `App_Net.h`。WAN 分支与 master 分支仅此文件宏值不同，`.c` 逻辑尽量共用。

### 2. 巴法云订阅

透传通道建立后立即发送 `cmd=1` 订阅主题。两条联网路径均需注入：

- 阻塞路径 `App_Net_Init()` — 成功返回前
- 非阻塞路径 `App_Net_Connect_Task()` — `on_success` 标签

抽象为静态函数 `Bemfa_Subscribe()` 消除重复。

### 3. 遥测格式

裸 JSON 外层包 `cmd=2&uid=xxx&topic=xxx&msg=` 前缀。JSON 值全为数字，不含 `&`/`=` 无需转义。

### 4. 静默看门狗移除

巴法云仅在下发指令时推送数据，平时完全静默。15s 看门狗会误触发。删除后仅靠 ESP8266 的 `CLOSED` 帧（TCP RST/FIN 感知）检测断线。

### 5. 指令解析不变

`strstr(localBuf, "CMD:ON")` 在巴法云下发信封 `cmd=2&...&msg=CMD:ON\r\n` 中依然命中，无需改解析逻辑。

## 改动清单

| 文件 | 位置 | 改动 | 行数 |
|------|------|------|------|
| `App_Net.h` | 头部 | 新增 6 个配置宏 | +6 |
| `App_Net.c` | 旧宏区块 | 删除，替为注释 | -7 +2 |
| `App_Net.c` | `Net_Remote_Off` 下方 | 新增 `Bemfa_Subscribe()` | +7 |
| `App_Net.c` | `App_Net_Init` 成功返回前 | 调用 `Bemfa_Subscribe()` | +1 |
| `App_Net.c` | `App_Net_Connect_Task:on_success` | 调用 `Bemfa_Subscribe()` | +1 |
| `App_Net.c` | `App_Net_Task` 遥测间隔 | `1000` → `2000` | 1 |
| `App_Net.c` | `App_Net_Task` 遥测 snprintf | 包 `cmd=2` 信封 | 改 3 行 |
| `App_Net.c` | `App_Net_Task` 静默看门狗 | 删除 | -4 |

**不修改任何已有函数名、变量名。** 新增 `Bemfa_Subscribe` 不触碰已有标识符。

## 测试要点

1. 上电联网后 OLED 能显示 "WiFi Connected!"，巴法云后台可见设备在线
2. 巴法云 APP 下发 "CMD:ON" → PWM 启动扫频，下发 "CMD:OFF" → PWM 关断
3. 巴法云后台可收到 2 秒一条的遥测 JSON
4. ESP8266 断电 → `CLOSED` 帧触发 → PWM 关断，OLED 回待联网界面
5. KEY0 重试联网成功 → 重新订阅 → 遥测恢复
6. 扫频期间不发送遥测（不破坏 10ms 步进节拍）
