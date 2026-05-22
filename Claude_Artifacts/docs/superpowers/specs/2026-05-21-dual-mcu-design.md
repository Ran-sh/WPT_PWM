# 双脑架构 (Dual-MCU) 重构设计

**日期**: 2026-05-21  
**版本**: V4.0  
**分支**: WAN

## 目标

将系统从"STM32 软解 AT 指令"架构升级为工业界标准的"双脑架构":
- **STM32** (物理脑): 只负责 PWM 发波、ADC 采集、UI 交互、硬件保护
- **ESP8266** (联网脑): 独立 Arduino 固件，负责 WiFi + MQTT 连云
- **通信**: USART2 (115200bps) 纯 JSON 文本透传，无任何 AT 指令

## 架构对比

```
【旧】STM32 软解 AT                    【新】双脑架构
STM32 做所有事:                        STM32 (物理层):
  PWM/ADC/KEY/OLED/UI                    PWM/ADC/KEY/OLED/UI
  AT+CWJAP/CIPSTART/CIPSEND              串口 JSON ↔ ESP8266
  strstr 解析响应
                                       ESP8266 (联网脑):
ESP8266: AT 透传固件                      WiFi + MQTT + 串口转发
```

## 通信协议

```
STM32 → ESP8266:  {"V":12.50,"I":1.23,"F":100000}\n
ESP8266 → STM32:  CMD:ON\n  或  CMD:OFF\n
```

## 变更清单

### STM32 端

| 文件 | 操作 | 说明 |
|:---|:---|:---|
| `PWM/Hardware/ESP8266.c` | 修改 | `ESP8266_Init()` 删 AT+RST 和 "+++", 保留 CH_PD 硬件复位 + USART2 初始化 |
| `PWM/User/App_Net.h` | 重写 | 删除 `NetState_t` + 5 个非阻塞 API; 保留 `App_Net_Init/Task/IsConnected` |
| `PWM/User/App_Net.c` | 重写 | 纯 JSON 透传, 删全部 AT 状态机 + CLOSED 检测 |
| `PWM/User/main.c` | 修改 | 删除 `App_Net_Connect_Task()` 调用 |
| `PWM/Hardware/UI.c` | 修改 | 删除对已移除 API 的调用 |

### ESP8266 端

| 文件 | 操作 | 说明 |
|:---|:---|:---|
| `ArduinoProject/ESP8266_MQTT_Firmware/ESP8266_MQTT_Firmware.ino` | 新建 | MQTT 固件 |

## App_Net.c 新逻辑

```c
// App_Net_Init: ESP8266_Init() + 直接标记成功
// App_Net_Task: 
//   发送: 每 2000ms ESP8266_SendString("{\"V\":...}\n")
//   接收: ESP8266_CopyRxFrame → strstr CMD:ON/OFF → 控制
```

## ESP8266_Init 新逻辑

```
CH_PD 拉低 1000ms → 拉高等 2000ms → USART2 初始化 → 清缓冲区 → 完成
```

## 保留不变的 API

- `App_Net_Init()` — 签名不变，内部简化
- `App_Net_Task()` — 签名不变，内部简化
- `App_Net_IsConnected()` — 不变
