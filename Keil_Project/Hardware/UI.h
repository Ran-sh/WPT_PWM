#ifndef __UI_H
#define __UI_H

#include "stm32f10x.h"

/* ── UI 主状态机 (7 个界面) ── */
typedef enum {
    UI_STATE_INIT       = 0,  /* 界面1: 初始 — 按KEY0连接WiFi, 底部无频率 */
    UI_STATE_CONNECTING = 1,  /* 界面2: 连接中 — 自动重连中 */
    UI_STATE_READY      = 2,  /* 界面3: 已连接 — KEY0 Start */
    UI_STATE_SWEEPING   = 3,  /* 界面4: 扫频中 */
    UI_STATE_RUNNING    = 4,  /* 界面5: 运行 — KEY0 STOP, KEY1 + */
    UI_STATE_FAULT      = 5   /* 故障 — 过流保护 */
} UI_State_t;

void      UI_Task(void);
UI_State_t UI_GetState(void);
uint8_t   UI_GetBridgeState(void);

#endif
