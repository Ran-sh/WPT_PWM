/* 桥接服务器地址 — 二选一 */
/* ngrok 本地隧道 */
// const BRIDGE = 'https://kindred-whisking-varnish.ngrok-free.dev';
/* Railway 云端 */
const BRIDGE = 'https://wptrailway-production.up.railway.app';
const POLL_MS = 2000;
const TIM1_CLK = 72000000;

function buildFreqMap() {
  /* 映射: 显示 kHz → 发往 STM32 的 Hz 值 (使 OLED 实际显示 = 显示 kHz) */
  const map = new Map();
  for (let hz = 95000; hz <= 150000; hz += 1000) {
    let ticks = Math.floor(TIM1_CLK / hz);
    if (ticks % 2 !== 0) ticks += 1;
    const displayKHz = Math.floor(TIM1_CLK / ticks / 1000);
    if (displayKHz < 95) continue;  /* 最低显示 95kHz */
    if (!map.has(displayKHz)) map.set(displayKHz, hz);
  }
  const entries = Array.from(map.entries()).sort((a, b) => a[0] - b[0]);
  return {
    list: entries.map(e => e[0]),
    hzMap: entries.map(e => e[1])
  };
}
const FREQ = buildFreqMap();
const FREQ_LIST = FREQ.list;
const FREQ_HZ   = FREQ.hzMap;
const INIT_IDX  = FREQ_LIST.indexOf(100);

Page({
  data: {
    voltage: '--',
    current: '--',
    frequency: '--',
    systemState: 'IDLE',
    stateLabel: '待机',
    isOn: false,
    isFault: false,
    connected: false,
    freqList: FREQ_LIST,
    selectedFreq: 100,
    freqIdx: INIT_IDX,
    currentTheme: 'theme-dark'
  },

  onLoad() {
    const saved = wx.getStorageSync('wpt_theme');
    if (saved) this.setData({ currentTheme: saved });
    this.fetchData();
    this._timer = setInterval(() => this.fetchData(), POLL_MS);
  },

  onToggleTheme() {
    const next = this.data.currentTheme === 'theme-dark' ? 'theme-light' : 'theme-dark';
    this.setData({ currentTheme: next });
    wx.setStorageSync('wpt_theme', next);
  },

  onUnload() {
    if (this._timer) clearInterval(this._timer);
  },

  fetchData() {
    const that = this;
    wx.request({
      url: BRIDGE + '/data',
      method: 'GET',
      header: { 'ngrok-skip-browser-warning': 'true' },
      success(res) {
        if (res.statusCode === 200) {
          const d = res.data;
          const fresh = !d.stale;

          if (!fresh) {
            that.setData({ connected: false });
            return;
          }

          const v = d.voltage !== undefined ? Number(d.voltage).toFixed(2) : that.data.voltage;
          const c = d.current !== undefined ? Number(d.current).toFixed(2) : that.data.current;
          const f = d.frequency !== undefined ? Math.floor(d.frequency / 1000) : that.data.frequency;
          const s = d.state;  /* 0=IDLE, 1=SWEEP, 2=DONE, 3=FAULT */

          const fNum = Number(f);
          let state, label;

          if (s === 3)      { state = 'FAULT'; label = '故障'; }
          else if (s === 2) { state = 'DONE';  label = '运行中'; }
          else if (s === 1) { state = 'SWEEP'; label = '扫频中'; }
          else              { state = 'IDLE';  label = '待机'; }

          that.setData({
            voltage: v,
            current: c,
            frequency: fNum,
            systemState: state,
            stateLabel: label,
            isOn: s === 1 || s === 2,
            isFault: s === 3,
            connected: true
          });
        }
      },
      fail(err) {
        console.log('[HTTP] request fail:', err);
        that.setData({ connected: false });
      }
    });
  },

  /* ── 控制指令 ── */
  sendCmd(cmd) {
    wx.request({
      url: BRIDGE + '/cmd',
      method: 'POST',
      header: {
        'content-type': 'application/json',
        'ngrok-skip-browser-warning': 'true'
      },
      data: { cmd: cmd },
      success(res) { console.log('[CMD] sent ok:', cmd); },
      fail(err) { wx.showToast({ title: '发送失败', icon: 'none' }); }
    });
  },

  _debounce(ts) {
    const now = Date.now();
    if (now - (this._lastTap || 0) < ts) return true;
    this._lastTap = now;
    return false;
  },

  onSwitch() {
    if (this._debounce(800)) return;
    if (this.data.isFault) return;
    const on = !this.data.isOn;
    this.sendCmd(on ? 'CMD:ON' : 'CMD:OFF');
    this.setData({
      isOn: on,
      systemState: on ? 'SWEEP' : 'IDLE',
      stateLabel: on ? '扫频中' : '待机'
    });
  },

  /* ── 滑动选频 ── */
  onSwiperChange(e) {
    const idx = e.detail.current;
    if (idx !== this.data.freqIdx) {
      this.setData({ freqIdx: idx, selectedFreq: FREQ_LIST[idx] });
    }
  },

  /* ── 确认设置 ── */
  onSetFreq() {
    if (!this.data.isOn) {
      wx.showToast({ title: '请先启动设备', icon: 'none' });
      return;
    }
    const kHz = this.data.selectedFreq;
    const hz  = FREQ_HZ[this.data.freqIdx];  /* 用映射的 Hz, 保证 OLED 显示 = kHz */
    this.sendCmd('CMD:SETFREQ:' + hz);
    wx.showToast({ title: 'Set ' + kHz + 'kHz', icon: 'none', duration: 800 });
  }
});
