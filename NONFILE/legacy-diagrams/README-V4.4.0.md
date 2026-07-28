# System Workflow Diagrams

## Files

| File | Description |
|:---|:---|
| `WPT_PWM_系统工作流-V4.4.0.vsdx` | Visio 4-page system workflow diagram (A4 landscape, Chinese labels) |
| `draw_visio-V4.4.0.py` | Python COM automation script to generate the .vsdx (requires `pywin32` + Microsoft Visio) |

## Pages

| Page | Name | Shapes | Content |
|:---|:---|:---|---:|
| 1 | **1-启动流程** | 30 | 4-stage power-on startup: HW init → System timer → IWDG → ESP networking → main loop |
| 2 | **2-主循环调度** | 28 | 8 non-blocking tasks with period annotations, left-right 2-column layout |
| 3 | **3-中断与安全层** | 13 | ISR handlers + fault processors + 5-layer safety protection + critical section rule |
| 4 | **4-Dual-MCU数据流** | 41 | STM32 ↔ ESP8266 architecture, module mapping, USART2 protocol, pin mapping |

## Regeneration

```bash
python draw_visio-V4.4.0.py
```

**Requirements**: Microsoft Visio installed, `pywin32` Python package.

**Output**: `WPT_PWM_系统工作流-V4.4.0.vsdx` (overwritten each run).

## Coordinate System

- Page size: 297×210mm (A4 landscape)
- All coordinates verified to stay within page bounds
- Top-left coordinate origin converted to Visio's bottom-left origin via `y_visio = PH - y - h`
