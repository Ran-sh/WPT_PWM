# -*- coding: utf-8 -*-
"""
WPT PWM ONENET V3.0 — Visio 系统工作流程图
pywin32 COM 驱动, 坐标全部绑定在 A4 横版 (297×210mm) 内
"""
import win32com.client, os, sys
sys.stdout.reconfigure(encoding='utf-8')

def mm(v): return v / 25.4

PW, PH = 297.0, 210.0  # A4 横版
MX = 6.0                # 左右边距
MY_TOP = 5.0            # 上边距
MY_BOT = 5.0            # 下边距
UW = PW - 2*MX          # 可用宽度 285mm
UH = PH - MY_TOP - MY_BOT  # 可用高度 200mm

# ═══════════════ 工具 ═══════════════
class VShape:
    def __init__(self, shape): self.c = shape
    def _f(self, cell, val): self.c.Cells(cell).Formula = val
    def fill(self, rgb): self._f("FillForegnd", f"RGB({rgb[0]},{rgb[1]},{rgb[2]})")
    def lc(self, rgb, w="1pt"):
        self._f("LineColor", f"RGB({rgb[0]},{rgb[1]},{rgb[2]})"); self._f("LineWeight", w)
    def fnt(self, sz="7pt", rgb=(30,30,30), b=False):
        self._f("Char.Size", sz); self._f("Char.Color", f"RGB({rgb[0]},{rgb[1]},{rgb[2]})")
        if b: self._f("Char.Style", "17")
    def va(self, v="c"):
        d = {"t":"0","c":"1","b":"2"}
        self._f("VerticalAlign", d.get(v,"1"))
    def ha(self, h="c"):
        d = {"l":"0","c":"1","r":"2"}
        self._f("Para.HorzAlign", d.get(h,"1"))
    def rnd(self, r=1.0): self._f("Rounding", f"{mm(r)} in")
    def tr(self, p): self._f("FillForegndTrans", str(p/100))
    def txt(self, t): self.c.Text = t

class V:
    def __init__(self):
        self.a = win32com.client.Dispatch("Visio.Application")
        self.a.Visible = True
        self.d = self.a.Documents.Add("")
    def pg(self, i=1):
        for p in self.d.Pages:
            if p.Name == str(i): return p
        return self.d.Pages.Item(i)
    def ap(self, name):
        p = self.d.Pages.Add(); p.Name = name
        p.PageSheet.Cells("PageWidth").Formula = f"{PW} mm"
        p.PageSheet.Cells("PageHeight").Formula = f"{PH} mm"
        return p

    # x,y = 左上角坐标 (mm), w,h = 宽高 (mm)
    def _vy(self, y, h):
        """将左上角坐标转为 Visio 内部坐标 (左下角原点)"""
        return PH - y - h

    def box(self, page, x, y, w, h, text="", fill=(220,220,220), line=(150,150,150),
            lw="1pt", fs="7pt", fc=(30,30,30), bold=False, rd=None, ha="c", va="c"):
        vy = self._vy(y, h)
        s = VShape(page.DrawRectangle(mm(x), mm(vy), mm(x+w), mm(vy+h)))
        s.fill(fill); s.lc(line, lw); s.fnt(fs, fc, bold); s.va(va); s.ha(ha)
        if rd: s.rnd(rd)
        if text: s.txt(text)
        return s

    def rb(self, page, x, y, w, h, *a, **k):
        return self.box(page, x, y, w, h, *a, rd=1.5, **k)

    def title(self, page, y, text):
        return self.rb(page, MX, y, UW, 9, text, (25,60,140), (25,60,140), fs="10pt", fc=(255,255,255), bold=True)

    def arrow(self, page, x1, y1, x2, y2):
        s = VShape(page.DrawLine(mm(x1), mm(self._vy(y1,0)), mm(x2), mm(self._vy(y2,0))))
        s.lc((130,130,130), "0.8pt"); s.c.Cells("EndArrow").Formula = "13"; s.c.Cells("EndArrowSize").Formula = "2"
        return s
    def dn(self, page, x, y, dy=4): return self.arrow(page, x, y, x, y+dy)
    def rt(self, page, x, y, dx=3): return self.arrow(page, x, y, x+dx, y)

    def row2(self, page, x, y, w, items, fs="5.5pt", clr=None):
        """一行两个并排小框"""
        bw = (w - 4) / 2
        for i, (txt, cl) in enumerate(items):
            c = cl if clr is None else clr
            self.rb(page, x + i*(bw+4), y, bw, 12, txt, c, c, fs=fs)

    def group(self, page, x, y, w, h, label=""):
        s = self.rb(page, x, y, w, h, label, (248,248,252), (180,180,190), fs="6pt", fc=(120,120,140), ha="l", va="t")
        s.tr(60)
        return s

    def save(self, path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        self.d.SaveAs(path)

# ═══════════════ 颜色 ═══════════════
HW  = (210,230,255); SYS = (255,238,215); APP = (215,250,215); UI  = (240,230,255)
ESP = (225,242,205); SAF = (255,195,195); O   = (255,215,155); CY  = (210,235,240)
PU  = (225,210,250); YW  = (255,255,210); WH  = (255,255,255); GY  = (150,150,150)
DKB = (25,60,140);   DKG = (40,100,30);   DKR = (160,30,30);   DKO = (150,90,30)
WHT = (255,255,255); BLK = (30,30,30)

# ═══════════════ 主程序 ═══════════════
v = V()
OUT = r"D:\Claude Code Project\WPT_PWM_ONENET_V3.0\Claude_Files\diagrams\WPT_PWM_系统工作流.vsdx"

# Helper to trace y positions
def trace(pg_name, y_val, label="", max_y=PH-MY_BOT):
    if y_val > max_y:
        print(f"  ⚠ {pg_name}: y={y_val:.0f} > {max_y:.0f} OVERFLOW [{label}]")

# ━━━━━━━ PAGE 1: 启动流程 ━━━━━━━
p1 = v.pg(1); p1.Name = "1-启动流程"
p1.PageSheet.Cells("PageWidth").Formula = f"{PW} mm"
p1.PageSheet.Cells("PageHeight").Formula = f"{PH} mm"

y = MY_TOP
v.title(p1, y, "WPT PWM V6.0 — 上电启动流程 (STM32 + ESP8266 Dual-MCU)")
y += 11

# Phase 1 header
v.rb(p1, MX, y, UW, 10, "阶段1: 硬件初始化 — MOE全关, 全桥零输出", HW, HW, fs="8pt", fc=DKR, bold=True)
y += 12
# 5 modules in a row
mods = ["Pwm_Driver_Init\nTIM1 Up 150kHz\nMOE=OFF", "Oled_Driver_Init\nSSD1315 128x64", "Led_Driver_Init\nJTAG禁用→PB3/4", "Adc_Driver_Init\nADC1+DMA1 双通道", "Key_Driver_Init\nPB12/13 IPU 双键"]
bx, gap = MX, 3
bw = (UW - 4*gap) / 5
for i, txt in enumerate(mods):
    v.rb(p1, bx, y, bw, 14, txt, HW, (150,180,220), fs="5pt")
    if i < 4: v.rt(p1, bx+bw, y+7, gap+1)
    bx += bw + gap
y += 17
v.dn(p1, PW/2, y-3, 4); y += 4

# Phase 2
v.rb(p1, MX, y, UW, 10, "阶段2: 系统时基", SYS, SYS, fs="8pt", fc=DKO, bold=True)
y += 12
v.rb(p1, 70, y, 157, 8, "Sys_Timer_Init: SysTick 1ms + DWT 72MHz 周期计数器", SYS, (200,170,140), fs="6pt")
y += 11
v.dn(p1, PW/2, y-3, 4); y += 4

# Phase 2.5
v.rb(p1, MX, y, UW, 10, "阶段2.5: IWDG 独立看门狗", SAF, SAF, fs="8pt", fc=DKR, bold=True)
y += 12
v.rb(p1, 40, y, 217, 8, "IWDG: LSI 40kHz, 分频64, 重载1000 → 1.6s 超时  |  DBGMCU->CR |= DBG_IWDG_STOP (调试冻结)", SAF, (220,160,160), fs="6pt")
y += 11
v.dn(p1, PW/2, y-3, 4); y += 4

# Phase 3
v.rb(p1, MX, y, UW, 10, "阶段3: 自动联网 — 非阻塞 CH_PD 时序 (总计 3 秒)", ESP, ESP, fs="8pt", fc=DKG, bold=True)
y += 12
esp_steps = [
    "App_Network_Start_Connect\nconn_state=WIFI", "Start_Init: 清空RX缓冲\nGPIO+USART+NVIC, CH_PD=0",
    "Init_Task FSM: RESET_LOW\n1000ms→CH_PD=1→BOOT_WAIT\n2000ms→READY",
    "ESP8266固件启动\nWiFiManager→MQTT\n→STATUS:ONLINE→STM32"
]
bx = MX; gap = 4; bw = (UW - 3*gap) / 4
for i, txt in enumerate(esp_steps):
    v.rb(p1, bx, y, bw, 16, txt, ESP, (180,210,150), fs="5pt")
    if i < 3: v.rt(p1, bx+bw, y+8, gap+1)
    bx += bw + gap
y += 19
v.dn(p1, PW/2, y-3, 4); y += 4

# Phase 4
v.rb(p1, MX, y, UW, 10, "阶段4: 主循环 — while(1) 全非阻塞调度, __WFI 休眠", APP, APP, fs="8pt", fc=DKG, bold=True)
y += 12
v.rb(p1, 15, y, 267, 12,
    "while(1) { Key_Driver_Task | Adc_Driver_Filter_Task | Ui_Controller_Task | App_Network_Task | "
    "Inverter_Control_Soft_Start | Inverter_Control_Freq_Ramp | Led_Driver_Task | IWDG_ReloadCounter | __WFI }",
    APP, (160,210,160), fs="5.5pt", bold=True)
y += 15

# Safety note
v.rb(p1, MX, y, UW, 9, "[安全] 以上阶段 PWM MOE始终=OFF, 全桥零输出  |  OLED 显示 'Wireless Charge / Booting ESP...'", SAF, (240,180,180), fs="6pt", fc=DKR)
trace("P1", y+9, "end")

# ━━━━━━━ PAGE 2: 主循环调度 ━━━━━━━
p2 = v.ap("2-主循环调度")
y = MY_TOP
v.title(p2, y, "主循环任务调度 — 全非阻塞, __WFI 休眠 (<5mA), 零 busy-wait")
y += 12
v.rb(p2, MX, y, UW, 10, "while(1) { ... } — 8 个任务独立周期调度", (245,245,248), (190,190,200), fs="8pt", bold=True)
y += 13

# 4 rows x 2 columns
col_w = 139; gap = 4
LX = MX; RX = MX + col_w + gap
row_h = 33  # 标题 11 + 内容 22

for row_idx, (l_name, l_period, l_detail, l_clr, r_name, r_period, r_detail, r_clr) in enumerate([
    ("Key_Driver_Task", "10ms",
     "双键FSM: IDLE→DEBOUNCE(10ms)→PRESS\n→WAIT_DOUBLE(200ms)→CLICK/DOUBLE_CLICK\n→LONG_HOLD(3s), 事件由UI消费",
     HW,
     "Inverter_Control_Soft_Start", "10ms",
     "FSM: SS_IDLE→SWEEP→DONE/FAULT\n扫频:150k→100kHz, 200Hz/10ms, ~2.5秒\nTrigger/Stop/Fault 原子操作",
     APP),
    ("Adc_Driver_Filter_Task", "~2ms",
     "DMA1循环写 s_adc_raw[2]: PA0=电流, PA1=电压\nDWT 144241周期节拍(与100kHz PWM互质)\n64样本滑动窗口→DC分量提取",
     HW,
     "Inverter_Control_Freq_Ramp", "10ms",
     "CMD:SETFREQ触发→ACTIVE, 1kHz/10ms渐变\n[已修复] |diff|≤1000→收敛→强制Set_Freq\n消除整数分频永不收敛bug",
     APP),
    ("Ui_Controller_Task", "200ms",
     "Calc_Ui_State: 6态FSM (INIT/CONNECTING/READY/\nSWEEPING/RUNNING/FAULT)\nHandle_Keys→逆变器 | Update_Leds→LED | Draw_*:6屏×2页",
     UI,
     "Led_Driver_Task", "实时",
     "PC13心跳:500ms翻转(低电平亮)\nPB3 WiFi:Slow(500ms)/Fast(200ms)/ON\nPB4 PWM:ON(扫频)/OFF | PB5 Ready:ON/OFF",
     HW),
    ("App_Network_Task", "实时",
     "Esp8266_Init_Task: CH_PD时序驱动\nCheck_Retry:15s超时,最多3次→FAILED\nCMD解析+遥测500ms: JSON→USART2→ESP→云",
     ESP,
     "IWDG_ReloadCounter + __WFI", "1.6s",
     "IWDG_ReloadCounter(): 喂狗(1.6s内必须调用)\n__WFI(): 休眠等SysTick唤醒\n空闲电流~30mA→~5mA | 卡死→看门狗复位",
     SAF),
]):
    ry = y + row_idx * (row_h + 3)
    v.rb(p2, LX, ry, col_w, 10, f"{l_name}  [{l_period}]", l_clr, l_clr, fs="6.5pt", bold=True)
    v.rb(p2, LX, ry+10, col_w, row_h-10, l_detail, l_clr, l_clr, fs="5pt")
    v.rb(p2, RX, ry, col_w, 10, f"{r_name}  [{r_period}]", r_clr, r_clr, fs="6.5pt", bold=True)
    v.rb(p2, RX, ry+10, col_w, row_h-10, r_detail, r_clr, r_clr, fs="5pt")
    v.rt(p2, LX+col_w+1, ry+row_h//2, 2)
    if row_idx < 3:
        v.dn(p2, LX+col_w//2, ry+row_h+1, 3)
        v.dn(p2, RX+col_w//2, ry+row_h+1, 3)

trace("P2", y + 4*(row_h+3) + 3, "end")

# ━━━━━━━ PAGE 3: 中断与安全 ━━━━━━━
p3 = v.ap("3-中断与安全层")
y = MY_TOP
v.title(p3, y, "中断服务 & 安全防护层")
y += 12

v.rb(p3, MX, y, UW, 11, "NVIC PriorityGroup_2 (2位抢占+2位子优先级)  |  SysTick=默认  |  USART2=抢占1,子0",
     CY, (160,190,210), fs="7pt")
y += 14

# ISR row
v.rb(p3, MX, y, 140, 26,
    "SysTick_Handler (每1ms)\n━━━━━━━━━━\nSys_Timer_Inc_Tick()\n仅递增 s_sys_tick, 无业务逻辑!",
    (255,215,230), (210,150,170), fs="6.5pt", bold=True)
v.rb(p3, 150, y, 141, 26,
    "USART2_IRQHandler (每个字节)\n━━━━━━━━━━\n1. 先处理ORE溢出(读DR清标志,防锁死)\n2. RXNE→USART_ReceiveData→Rx_Char\n3. 拼接行缓冲, \\r/\\n触发帧标志",
    ESP, (170,200,150), fs="6pt", bold=True)
y += 30

# Fault + WDG row
v.rb(p3, MX, y, 140, 28,
    "故障处理器 ×4\n━━━━━━━━\nHardFault / MemManage / BusFault / UsageFault\n  ↓ 共同动作 ↓\nTIM_CtrlPWMOutputs(TIM1, DISABLE)\n关断桥臂→while(1)等看门狗复位",
    SAF, (240,130,130), fs="6pt", bold=True)
v.rb(p3, 150, y, 141, 28,
    "看门狗 & 调试保护\n━━━━━━━━\nIWDG: LSI 40kHz/64=625Hz, reload=1000→1.6s超时\n主循环喂狗, 任务卡死→硬件复位\nDBGMCU->CR |= DBG_IWDG_STOP: 调试冻结, 避免下载失败",
    SAF, (220,160,160), fs="6pt", bold=True)
y += 33

# Safety layers
v.rb(p3, MX, y, UW, 9, "5 层安全防护体系", SAF, (250,160,160), fs="8pt", fc=DKR, bold=True, ha="l", va="c")
y += 11
for txt in [
    "① 上电安全: Pwm_Driver_Init MOE=OFF, 全桥零输出, 软件可控开通 — 硬件级安全基线",
    "② 故障保护: 4个Fault Handler全部关断MOE, 桥臂无直通风险, CPU跑飞也能保护",
    "③ 看门狗: IWDG 1.6s超时, 任何任务卡死→硬件自动复位, 系统自愈无需干预",
    "④ 状态机容错: FAULT状态不可自动恢复, 须用户按键确认后手动复位, 防止反复重启",
    "⑤ ADC零点校准: 上电50样本取平均(0.5s)+EMA慢速追踪温漂(α=0.05), 保证精度",
]:
    v.rb(p3, MX+2, y, UW-4, 10, txt, (255,242,242), (240,210,210), fs="6pt")
    y += 12

# Critical section
v.rb(p3, MX, y, UW, 8,
    "临界区规范: primask = __get_PRIMASK();  __disable_irq();  ...  __set_PRIMASK(primask);  // 禁止裸 __enable_irq()!",
    YW, (190,190,130), fs="6pt", bold=True)
trace("P3", y+8, "end")

# ━━━━━━━ PAGE 4: Dual-MCU ━━━━━━━
p4 = v.ap("4-Dual-MCU数据流")
y = MY_TOP
v.title(p4, y, "Dual-MCU 架构 & 数据流")
y += 12

# Left: STM32 box
BOX_H = 138
v.rb(p4, MX, y, 138, BOX_H, "", HW, (160,190,220))
v.rb(p4, MX+32, y+3, 74, 6, "STM32F103C8 (物理脑)", DKB, DKB, fs="6pt", fc=WHT, bold=True)

# Right: ESP8266 box
EX = MX + 144
v.rb(p4, EX, y, 141, BOX_H, "", ESP, (170,210,140))
v.rb(p4, EX+34, y+3, 73, 6, "ESP8266 (联网脑)", DKG, DKG, fs="6pt", fc=WHT, bold=True)

# STM32 modules
my = y + 12
for row in [
    [("Pwm_Driver\nTIM1全桥PWM", HW), ("Inverter_Control\n软启动+斜坡", APP)],
    [("Adc_Driver\nV/I采集滤波", HW), ("Ui_Controller\n6态UI+按键", UI)],
    [("Key_Driver\n双键FSM", HW), ("Led_Driver\n4灯LED驱动", HW)],
    [("App_Network\n联网+遥测", APP), ("Esp8266_Driver\nUSART2 115200", ESP)],
    [("Sys_Timer\nSysTick+DWT", SYS), ("IWDG 看门狗\n1.6s超时", SAF)],
]:
    for j, (txt, clr) in enumerate(row):
        v.rb(p4, MX+3 + j*67, my, 64, 13, txt, clr, clr, fs="5pt")
    my += 15

# ESP8266 modules
ey = y + 12
for txt, clr, h in [
    ("WiFiManager 网页配网Portal", ESP, 11),
    ("MQTT Client 双Broker连接 (OneNET+备用)", ESP, 11),
    ("Serial_Parse 行读取+前缀匹配 Str_Starts_With", ESP, 11),
    ("Conn_State FSM: IDLE→WIFI→MQTT→ONLINE→FAILED", ESP, 13),
    ("OneNET 物模型: 属性上报+指令下发+OTA", ESP, 13),
    ("断线自动重连: 掉线→WiFiManager→MQTT重连", ESP, 11),
]:
    v.rb(p4, EX+3, ey, 135, h, txt, clr, clr, fs="5pt")
    ey += h + 2

# USART2
mid_y = y + BOX_H + 6
v.rb(p4, 100, mid_y, 94, 9, "USART2 115200bps, 纯文本JSON, 零AT指令", O, (200,170,110), fs="6pt", bold=True)
v.rt(p4, 100, mid_y+4, -6)
v.rt(p4, 194, mid_y+4, -6)
mid_y += 12

# Iron Rule
v.rb(p4, MX, mid_y, UW, 7, "Iron Rule: STM32 绝不发 AT指令 | ESP8266 绝不碰 PWM/ADC | 纯文本 JSON | Str_Starts_With 前缀匹配 防误触发", O, (210,170,90), fs="5.5pt", bold=True)
mid_y += 10

# Up/Down data
v.rb(p4, MX, mid_y, 90, 14,
    "▲ 上行遥测 500ms\n{\"V\":12.50,\"I\":1.23,\"F\":100000,\"S\":2}\nSTM32→USART2→ESP→MQTT→OneNET云",
    ESP, (160,200,140), fs="4.5pt")
v.rb(p4, 201, mid_y, 90, 14,
    "▼ 下行指令\nSTATUS:ONLINE | CMD:ON/OFF\nCMD:SETFREQ:100000\nOneNET云→MQTT→ESP→USART2→STM32",
    HW, (160,190,220), fs="4.5pt")

cld_y = y + BOX_H - 8
# Cloud
v.rb(p4, 185, cld_y-22, 100, 14,
    "OneNET Studio 云平台\nMQTT 物模型: 属性上报+指令下发+OTA", CY, (110,170,210), fs="5pt")
v.dn(p4, 235, cld_y-8, 8)
# Web console
v.rb(p4, 15, cld_y-22, 100, 14,
    "ONENETapp 网页控制台 (Cloudflare Pages)\n远程监控 + 一键开/关 + 频率设置", PU, (170,140,220), fs="5pt")
v.rt(p4, 115, cld_y-15, 70)
v.rt(p4, 185, cld_y-11, -70)

# Pin map
py = PH - MY_BOT - 11
v.rb(p4, MX, py, UW, 10, "引脚映射 (STM32F103C8T6 LQFP-48)", (242,242,238), (210,210,220), fs="6pt", fc=(100,100,120), ha="l", va="c")
for row, yoff in [
    (["PA0:ADC_CH0→CC6920-10A", "PA8/PA7:TIM1_CH1/CH1N→桥左", "PA11/PA12:OLED I2C(模拟)", "PB3/4/5:WiFi/PWM/Ready LED"], 6),
    (["PA1:ADC_CH1→分压20:1", "PA9/PB0:TIM1_CH2/CH2N→桥右", "PB1:GPIO→ESP CH_PD/EN", "PB12/PB13:IPU→KEY0/KEY1"], 2),
]:
    for j, txt in enumerate(row):
        v.rb(p4, MX+3 + j*70, py + yoff, 67, 3, txt, (252,252,248), (225,225,220), fs="3.5pt")

trace("P4", py+10, "end")

# ── 保存 ──
v.save(OUT)
print("全部完成!")
for pg in v.d.Pages:
    print(f"  {pg.Name}: {len(pg.Shapes)} shapes")
print(f"文件: {OUT}")
