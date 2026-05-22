#ifndef __UI_H
#define __UI_H

void UI_Task(void);

/* WiFi 远程控制接口: 确保远程 ON/OFF 与本地按键状态同步 */
void    UI_SetBridgeState(uint8_t on_off);
uint8_t UI_GetBridgeState(void);
void    UI_SetWiFiConnected(uint8_t on_off);   /* 供 App_Net 感知断线重置 */

#endif
