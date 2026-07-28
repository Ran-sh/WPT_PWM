# V5.0 GPIO 引脚重映射 + 按键系统重构 — 设计文档

**版本**: V5.0  
**日期**: 2026-07-11  
**状态**: 已批准  
**上承**: V4.5.2 (4.0TFT 分支)

---

## 1. 目标

将 STM32F103C8 引脚全面重映射，配合新 PCB 硬件布局，同时重构按键为 5 键系统 + 三灯系统。

---

## 2. 完整引脚对照表

| Pin | V4.5.2 | V5.0 | 变更 |
|-----|--------|------|------|
| PA0 | TFT_RST | TFT_RST | 不变 |
| PA1 | ESP8266 RST | ESP8266 RST | 不变 |
| PA2 | USART2_TX | USART2_TX (ESP RX) | 不变 |
| PA3 | USART2_RX | USART2_RX (ESP TX) | 不变 |
| PA4 | TFT_CS | TFT_CS | 不变 |
| PA5 | SPI1_SCK | SPI1_SCK (TFT+W25Q) | 不变 |
| PA6 | TFT_DC/Flash MISO | TFT_DC / W25Q DO | 不变 |
| PA7 | SPI1_MOSI | SPI1_MOSI (TFT+W25Q) | 不变 |
| PA8 | TIM1_CH1 (HINA) | TIM1_CH1 (HINA) | 不变 |
| PA9 | TIM1_CH2 (HINB) | TIM1_CH2 (HINB) | 不变 |
| **PA10** | **LED_COM** | **移除** | LED 删减 |
| **PA11** | **LED_POWER** | **移除** | LED 删减 |
| **PA12** | **W25Q128_CS** | **TFT_BL** (GPIO 开关) | 重映射 |
| **PA15** | **LED_SYSTEM (心跳)** | **STATUS LED** | 行为变更 |
| PB0 | ADC_CH8 (电流) | ADC_CH8 (电流) | 不变 |
| PB1 | ADC_CH9 (电压) | ADC_CH9 (电压) | 不变 |
| **PB3** | **LED_PWM** | **POWER LED** | 重映射 |
| PB4 | LED_WIFI | LED_WIFI | 不变 |
| PB5 | KEY_PAGE (确定) | **KEY4 (确定/启停)** | ID 变更 |
| **PB6** | **TFT_BL (TIM4_CH1)** | **KEY3 (DOWN/减)** | 重映射 |
| **PB7** | **KEY_F_DOWN** | **KEY2 (UP/加)** | 功能反转 |
| **PB8** | **KEY_F_UP** | **KEY1 (返回)** | 功能变更 |
| **PB9** | **KEY_ON (返回)** | **KEY0 (电源开关)** | 功能变更 |
| PB10 | PowerCtrl (自动) | PowerCtrl (KEY0 手动) | 控制源变更 |
| PB11 | ESP8266 CH_PD | ESP8266 EN | 不变 |
| **PB12** | **(未用)** | **W25Q128_CS** | 新增 |
| PB13 | TIM1_CH1N (LINB) | TIM1_CH2N (LINA) | 不变 |
| PB14 | TIM1_CH2N (LINA) | TIM1_CH1N (LINB) | 不变 |
| PB15 | Buzzer | Buzzer | 不变 |

---

## 3. 三灯系统

| LED | Pin | IDLE | SWEEP | RUNNING | FAULT |
|-----|-----|------|-------|---------|-------|
| WIFI | PB4 | ON=在线 / SLOW=重连 / OFF=离线 (独立于系统状态) |
| POWER | PB3 | 跟随 PB10: HIGH=ON, LOW=OFF |
| STATUS | PA15 | OFF | SLOW (500ms) | ON | OFF |

**移除的 LED 接口**:
- `Led_Driver_Set_Com()` — PA10 已移除
- `Led_Driver_Set_Power()` — 原 PA11, 现由 POWER LED 直接跟随 PB10
- `Led_Driver_Set_Temp()` — 早已废弃 (PA12 让给 Flash CS)
- `Led_Driver_Set_Pwm()` — 原 PB3, 功能并入 STATUS LED

**新增接口**: `Led_Driver_Set_Status(Led_Driver_State state)` — 控制 PA15 STATUS LED

**POWER LED (PB3)** 不经过 Led_Driver 状态机, 由 `Sys_Power_Control_Handle()` 直接 GPIO 控制:
```c
if (pwr_on) GPIO_SetBits(GPIOB, GPIO_Pin_3);
else        GPIO_ResetBits(GPIOB, GPIO_Pin_3);
```

---

## 4. 五键系统

### 4.1 键位定义

| 代码 ID | 物理标签 | Pin | 功能 | 双击 | 长按 |
|---------|---------|-----|------|------|------|
| KEY_DRIVER_ID_POWER (0) | KEY0 | PB9 | 电源开关 | 无 | 无 |
| KEY_DRIVER_ID_BACK (1) | KEY1 | PB8 | 返回 | 回主菜单 | 无 |
| KEY_DRIVER_ID_UP (2) | KEY2 | PB7 | UP/加 | 标准双击 | 无 |
| KEY_DRIVER_ID_DOWN (3) | KEY3 | PB6 | DOWN/减 | 标准双击 | 无 |
| KEY_DRIVER_ID_CONFIRM (4) | KEY4 | PB5 | 确定/启停 | 禁止(CLICK_ONLY) | 仅WiFi配置页 |

### 4.2 KEY0 电源协调逻辑

KEY0 不进入 UI 层，由 `Sys_Core` 中专用的 `Sys_Power_Control_Handle()` 处理：

```c
void Sys_Power_Control_Handle(Key_Driver_Event k0_event)
{
    if (k0_event != KEY_DRIVER_EVENT_CLICK) return;

    if (PB10 当前为 LOW) {
        /* 开机: 使能 12V + POWER LED ON + STATUS LED 恢复 */
        GPIO_SetBits(GPIOB, GPIO_Pin_10);
        GPIO_SetBits(GPIOB, GPIO_Pin_3);    /* POWER LED */
    } else {
        /* 关机: 强制停 PWM + 关 12V + POWER LED OFF */
        Inverter_Control_Soft_Start_Stop();  /* 关 PWM+MOE */
        if (g_sys_state == SYS_STATE_RUNNING || g_sys_state == SYS_STATE_SWEEP)
            g_sys_state = SYS_STATE_IDLE;
        GPIO_ResetBits(GPIOB, GPIO_Pin_10);
        GPIO_ResetBits(GPIOB, GPIO_Pin_3);  /* POWER LED */
    }
}
```

**铁律**: KEY0 只控制 12V 电源 + 强制停 PWM，PWM 的启动仍由 KEY4 (确定) 独立控制。

### 4.3 KEY1 双击回主菜单

Key_Driver 中 KEY1 启用双击检测 (`WITH_DOUBLE` 模式):
- 单击 → `KEY_DRIVER_EVENT_CLICK` → UI 返回上一页
- 双击 → `KEY_DRIVER_EVENT_DOUBLE_CLICK` → UI 直接跳主菜单

### 4.4 Key_Driver 配置标志重构

旧方案: `no_double` bit flag (0=允许双击, 1=跳过双击)  
新方案: `config` 字段, 支持两种模式:

```c
#define KEY_DRIVER_CFG_CLICK_ONLY   0x01  /* 跳过双击, 释放即 CLICK (KEY0/KEY4) */
#define KEY_DRIVER_CFG_WITH_DOUBLE  0x00  /* 允许双击检测 (KEY1/KEY2/KEY3) */

/* 初始化后配置 */
Key_Driver_Configure(KEY_DRIVER_ID_POWER,   KEY_DRIVER_CFG_CLICK_ONLY);
Key_Driver_Configure(KEY_DRIVER_ID_BACK,    KEY_DRIVER_CFG_WITH_DOUBLE);
Key_Driver_Configure(KEY_DRIVER_ID_UP,      KEY_DRIVER_CFG_WITH_DOUBLE);
Key_Driver_Configure(KEY_DRIVER_ID_DOWN,    KEY_DRIVER_CFG_WITH_DOUBLE);
Key_Driver_Configure(KEY_DRIVER_ID_CONFIRM, KEY_DRIVER_CFG_CLICK_ONLY);
```

---

## 5. TFT 背光 (PA12 GPIO 开关)

- `Tft_Driver_Set_Backlight(uint8_t brightness)` 接口保留, 内部逻辑改为 `brightness > 0 ? HIGH : LOW`
- TIM4_CH1 PWM 完全移除, TIM4 停用
- `Sys_Post_Init()` 中背光恢复逻辑不变 (接口兼容)

---

## 6. W25Q128 CS 迁移 (PA12 → PB12)

仅改宏定义:
```c
#define FLASH_CS_PIN    GPIO_Pin_12
#define FLASH_CS_PORT   GPIOB
```
其余 Flash 驱动逻辑零改动。`W25Q_Enter_Mode`/`W25Q_Leave_Mode` 中的 PA6 动态切换不变。

---

## 7. Sys_Safety 变更

- **移除** PB10 电压自动阈值控制 (`V > 12V → HIGH, V ≤ 12V → LOW`)
- EMA 滤波和过流检测保留不变
- POWER LED 不再由 Sys_Safety 管理

---

## 8. 影响文件清单

| 文件 | 变更程度 | 说明 |
|------|----------|------|
| `main.c` | 小幅 | 接线表注释 |
| `Sys_Core.c/h` | **中等** | 新增 `Sys_Power_Control_Handle()`, 移除 PB10 自动逻辑 |
| `Key_Driver.c/h` | **中等** | 4→5 键, ID 枚举重命名, CFG 标志 |
| `Led_Driver.c/h` | **中等** | 引脚重定义, 移除 4 个废弃接口, 新增 STATUS LED |
| `Tft_Driver.c/h` | 小幅 | TFT_BL PA12 GPIO, 去 TIM4 PWM |
| `W25Q_Driver.c/h` | 小幅 | CS PA12→PB12 |
| `Ui_Controller.c` | **中等** | 键 ID 适配 (k0=POWER→不处理, k1=BACK, k2=UP, k3=DOWN, k4=CONFIRM), KEY1 双击主菜单 |
| `Esp8266_Driver.c` | 零改动 | — |
| `Adc_Driver.c` | 零改动 | — |
| `Pwm_Driver.c` | 零改动 | — |
| `Inverter_Control.c` | 零改动 | — |
| `Buzzer_Driver.c` | 零改动 | — |
| `App_Network.c` | 小幅 | 移除 `Led_Driver_Set_Com()` 调用 |

---

## 9. 不变项

- 系统状态机 (INIT→IDLE→SWEEP→RUNNING→FAULT)
- EMA 双级滤波链
- SPI1 分时复用 (TFT + W25Q128)
- ESP8266 通信协议 (JSON 透传)
- PWM 全桥参数 (95-150kHz, 死区 1000ns, 50% 占空)
- ADC 双通道 + 64 样本滑动窗口
- IWDG 看门狗
- W25Q128 分区布局
- 全平台数据格式 (Telemetry JSON)
- 网页端 + 小程序 (不变)
